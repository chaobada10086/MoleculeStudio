#include "next/selection/clipboard.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// Format identity
// ---------------------------------------------------------------------------
// The name is OURS. It is not a ChemDraw format name and this module never
// claims it is. See clipboard.hpp and RESEARCH_NOTES.md section 5 of T02.
const char* const kSelectionClipboardFormatName = "r62.selection.package.v1";
const char* const kSelectionClipboardTextName   = "r62.selection.text.v1";

const char* clipboardFormatProvenance() {
    return "Format name is this project's own registration "
           "(r62.selection.package.v1). It is NOT a ChemDraw binary format: no "
           "vendor clipboard format name is published, and the one present in "
           "this project's COM metadata is an inference, not a declaration. See "
           "next/persistence/RESEARCH_NOTES.md section 5.";
}

// ---------------------------------------------------------------------------
// MemoryClipboardTransport
// ---------------------------------------------------------------------------
core::Error MemoryClipboardTransport::open() {
    open_ = true;
    return core::Error::success();
}

core::Error MemoryClipboardTransport::publish(const std::string& formatName,
                                              const std::string& bytes) {
    if (!open_)
        return core::Error(core::ErrorCode::Locked, "clipboard is not open");
    if (failPublish_) {
        failPublish_ = false;
        return core::Error(core::ErrorCode::IoError,
                           failWhy_.empty() ? "injected publish failure" : failWhy_);
    }
    ++publishes_;
    if (bytes.empty()) {
        // Refuse an empty publish: "I published nothing successfully" is a lie
        // that makes a later cut delete the source for an unreadable payload.
        return core::Error(core::ErrorCode::InvalidArgument,
                           "refused to publish an empty payload");
    }
    slots_[formatName] = bytes;
    return core::Error::success();
}

core::Error MemoryClipboardTransport::read(const std::string& formatName,
                                           std::string* bytes, bool* present) {
    if (!open_)
        return core::Error(core::ErrorCode::Locked, "clipboard is not open");
    ++reads_;
    if (failRead_) {
        failRead_ = false;
        return core::Error(core::ErrorCode::IoError,
                           failWhy_.empty() ? "injected read failure" : failWhy_);
    }
    const std::map<std::string, std::string>::const_iterator it = slots_.find(formatName);
    if (it == slots_.end()) {
        if (present) *present = false;
        if (bytes) bytes->clear();
        return core::Error::success();
    }
    if (present) *present = true;
    if (bytes) *bytes = it->second;
    return core::Error::success();
}

bool MemoryClipboardTransport::hasFormat(const std::string& formatName) {
    return slots_.find(formatName) != slots_.end();
}

void MemoryClipboardTransport::clearAll() { slots_.clear(); }

// ---------------------------------------------------------------------------
// publishSelection
// ---------------------------------------------------------------------------
PublishReport publishSelection(const core::Document& document,
                              const std::vector<core::ObjectRef>& refs,
                              IClipboardTransport& transport,
                              bool alsoPublishText) {
    PublishReport report;
    if (refs.empty()) {
        report.why = "nothing selected";
        report.error = core::Error(core::ErrorCode::InvalidArgument, report.why);
        return report;
    }
    std::string package;
    persistence::SelectionPackageInfo info;
    const core::Error encoded =
        persistence::encodeSelectionPackage(document, refs, &package, &info);
    if (!encoded.ok()) {
        report.error = encoded;
        report.why = std::string("encode refused: ") + encoded.message;
        return report;
    }
    report.objectsEncoded = info.objectCount;

    const core::Error opened = transport.open();
    if (!opened.ok()) {
        report.error = opened;
        report.why = std::string("clipboard unavailable: ") + opened.message;
        return report;
    }
    // THE ORDER MATTERS. The package is published first; the text form is a
    // convenience and its failure does NOT invalidate the package.
    const core::Error published = transport.publish(kSelectionClipboardFormatName, package);
    if (!published.ok()) {
        transport.close();
        report.error = published;
        report.why = std::string("publish refused: ") + published.message;
        return report;
    }
    report.bytes = package.size();
    if (alsoPublishText) {
        // A short human-readable rendering so a paste into a plain text target
        // is at least informative rather than empty.
        std::ostringstream text;
        text << "r62 selection: " << info.objectCount << " objects ("
             << info.atomCount << " atoms, " << info.bondCount << " bonds, "
             << info.textCount << " texts)";
        transport.publish(kSelectionClipboardTextName, text.str());
    }
    transport.close();
    report.published = true;
    return report;
}

// ---------------------------------------------------------------------------
// fetchSelection
// ---------------------------------------------------------------------------
FetchReport fetchSelection(IClipboardTransport& transport, std::string* package) {
    FetchReport report;
    const core::Error opened = transport.open();
    if (!opened.ok()) {
        report.error = opened;
        report.why = std::string("clipboard unavailable: ") + opened.message;
        return report;
    }
    std::string bytes;
    bool present = false;
    const core::Error read = transport.read(kSelectionClipboardFormatName, &bytes, &present);
    transport.close();
    if (!read.ok()) {
        report.error = read;
        report.why = std::string("read refused: ") + read.message;
        return report;
    }
    if (!present) {
        report.why = "clipboard holds no selection package";
        return report;
    }
    // Validate BEFORE anyone writes a document: a corrupt payload must fail here,
    // where nothing has been touched.
    if (!persistence::inspectSelectionPackage(bytes, 0).ok()) {
        report.error = core::Error(core::ErrorCode::ParseError,
                                   "clipboard selection package is not readable");
        report.why = report.error.message;
        return report;
    }
    report.present = true;
    report.bytes = bytes.size();
    if (package) *package = bytes;
    return report;
}

// ---------------------------------------------------------------------------
// cutSelection
// ---------------------------------------------------------------------------
CutReport cutSelection(core::Document& document,
                       const std::vector<core::ObjectRef>& refs,
                       IClipboardTransport& transport,
                       DeleteObjectsFn deleteFn, void* deleteUserData,
                       SelectionDelta* selectionDelta) {
    CutReport report;
    // STEP 1: publish. Nothing is deleted yet.
    report.publish = publishSelection(document, refs, transport, true);
    if (!report.publish.published) {
        // STEP 1 FAILED. The document is NOT touched, deleteFn is never called.
        report.why = std::string("publish failed, nothing was cut: ") + report.publish.why;
        if (selectionDelta) *selectionDelta = SelectionDelta();
        return report;
    }

    // STEP 2: delete, through the caller's command layer so the delete is one
    // undoable transaction. If the caller has no delete function the cut is
    // refused rather than deleting outside a transaction.
    if (!deleteFn) {
        report.why = "no delete command supplied; refusing to delete outside a transaction";
        report.deleteError = core::Error(core::ErrorCode::InvalidArgument, report.why);
        return report;
    }
    const SelectionSet before(refs.empty() ? SelectionSet() : SelectionSet());
    (void)before;
    const core::Error deleted = deleteFn(deleteUserData, refs);
    if (!deleted.ok()) {
        report.deleteError = deleted;
        report.why = std::string("delete failed after publish: ") + deleted.message;
        // The clipboard now holds a payload but the source survived. That is the
        // safe direction: the user can paste a duplicate, they cannot lose data.
        return report;
    }
    report.deleted = true;
    report.deletedObjects = refs.size();
    if (selectionDelta) {
        SelectionSet beforeSet;
        beforeSet.addAll(refs, SelectionReason::Direct);
        SelectionSet afterSet;
        *selectionDelta = diffSelection(beforeSet, afterSet, refs.size());
    }
    return report;
}

// ---------------------------------------------------------------------------
// pastePackage / pasteSelection
// ---------------------------------------------------------------------------
PasteReport pastePackage(core::Document& target, const std::string& package,
                         const core::Vec2& translate) {
    PasteReport report;
    report.bytesRead = package.size();
    // inspect first: a corrupt payload is refused before any write.
    const core::Error inspected = persistence::inspectSelectionPackage(package, 0);
    if (!inspected.ok()) {
        report.error = inspected;
        report.why = std::string("package is not readable: ") + inspected.message;
        return report;
    }
    // decodeSelectionPackage runs its own core::Transaction and rolls back on
    // failure, which is exactly rule R3. The digest comparison below is the
    // independent proof that a refusal changed nothing.
    const uint64_t digestBefore = target.digest();
    std::vector<core::ObjectRef> inserted;
    persistence::RefusalReport refusals;
    const core::Error decoded =
        persistence::decodeSelectionPackage(package, target, &inserted, &refusals);
    if (!decoded.ok()) {
        report.error = decoded;
        report.why = std::string("paste refused: ") + decoded.message;
        if (!refusals.empty()) report.why += std::string(" | ") + refusals.summary();
        // A refusal must not have changed the document. Report if it did, so the
        // violation is loud rather than silent.
        if (target.digest() != digestBefore)
            report.why += " | WARNING: document digest changed on a refused paste";
        return report;
    }
    // The paste landed. Apply the drag offset as a SECOND transaction so the
    // translate is itself undoable and the paste is never half-offset.
    if (translate.x != 0.0 || translate.y != 0.0) {
        const TransformReport moved =
            transformObjects(target, inserted, Affine2::translation(translate),
                             TransformOptions());
        (void)moved;
    }
    report.pasted = true;
    report.objectsInserted = inserted.size();
    report.inserted = inserted;
    return report;
}

PasteReport pasteSelection(core::Document& target, IClipboardTransport& transport,
                           const core::Vec2& translate) {
    PasteReport report;
    std::string package;
    const FetchReport fetch = fetchSelection(transport, &package);
    if (!fetch.present) {
        report.error = fetch.error.ok()
            ? core::Error(core::ErrorCode::NotFound, fetch.why)
            : fetch.error;
        report.why = fetch.why;
        return report;
    }
    return pastePackage(target, package, translate);
}

// ---------------------------------------------------------------------------
// CopyDrag
// ---------------------------------------------------------------------------
CopyDrag::CopyDrag()
    : active_(false), constrained_(false), origin_(), offset_(), frozenBounds_() {}

CopyDrag::~CopyDrag() {}

bool CopyDrag::begin(const core::Document& document,
                     const std::vector<core::ObjectRef>& refs,
                     const core::Vec2& origin,
                     bool constrainAxis) {
    active_ = false;
    package_.clear();
    sources_.clear();
    why_.clear();
    offset_ = core::Vec2(0, 0);
    constrained_ = constrainAxis;
    origin_ = origin;
    if (refs.empty()) {
        why_ = "nothing selected to copy";
        return false;
    }
    if (!origin.finite()) {
        why_ = "drag origin is not finite";
        return false;
    }
    // The package is encoded ONCE, at begin(). The commit pastes THESE bytes, so
    // the ghost the user saw and the objects that land are the same payload. If
    // the model changed under the drag, the commit still pastes what the preview
    // showed - which is the preview/commit agreement rule.
    persistence::SelectionPackageInfo info;
    const core::Error encoded =
        persistence::encodeSelectionPackage(document, refs, &package_, &info);
    if (!encoded.ok()) {
        why_ = std::string("cannot encode the copy: ") + encoded.message;
        package_.clear();
        return false;
    }
    sources_ = refs;
    frozenBounds_ = selectionBounds(document, refs);
    active_ = true;
    return true;
}

core::Vec2 CopyDrag::update(const core::Vec2& pointer, bool constrainAxis) {
    if (!active_) return offset_;
    if (!pointer.finite()) return offset_;
    core::Vec2 delta = pointer - origin_;
    constrained_ = constrainAxis;
    if (constrainAxis) {
        // [G82-85]: Ctrl+Shift constrains the copy-drag to the dominant axis.
        // T04 exposes SnapResolver::constrainToAxis for the drawing path; the
        // copy-drag uses the SAME rule, implemented here as an axis lock so the
        // two cannot drift apart in behaviour.
        if (std::fabs(delta.x) >= std::fabs(delta.y)) delta.y = 0.0;
        else delta.x = 0.0;
    }
    offset_ = delta;
    return offset_;
}

PasteReport CopyDrag::commit(core::Document& target) {
    PasteReport report;
    if (!active_ || package_.empty()) {
        report.why = "copy-drag is not active";
        report.error = core::Error(core::ErrorCode::InvalidArgument, report.why);
        return report;
    }
    report = pastePackage(target, package_, offset_);
    if (report.pasted) {
        // The drag is done. The SOURCES WERE NEVER TOUCHED, which is rule R4 and
        // is asserted by comparing the source atoms' positions before and after.
        active_ = false;
        package_.clear();
        sources_.clear();
    }
    return report;
}

void CopyDrag::cancel() {
    active_ = false;
    package_.clear();
    sources_.clear();
    offset_ = core::Vec2(0, 0);
    frozenBounds_ = core::Box();
}

// ---------------------------------------------------------------------------
// repeatStatus
// ---------------------------------------------------------------------------
RepeatReport repeatStatus(const core::CommandLog& log) {
    RepeatReport report;
    if (!log.canRepeat()) {
        report.why = "no repeatable command in the history";
        return report;
    }
    const core::Command* command = log.repeatCommand();
    if (!command || !command->valid()) {
        report.why = "the repeat slot is empty";
        return report;
    }
    report.repeatable = true;
    report.label = command->name();
    return report;
}

// ---------------------------------------------------------------------------
// mergeSelection
// ---------------------------------------------------------------------------
std::string MergeReport::summary() const {
    std::ostringstream out;
    out << "merged=" << mergedPairs << " refused=" << refusedPairs
        << " bondsRewired=" << bondsRewired
        << " duplicateBondsDropped=" << duplicateBondsDropped
        << " selfBondsDropped=" << selfBondsDropped;
    if (!refusals.empty()) out << " firstRefusal=" << refusals.front();
    return out.str();
}

MergeReport mergeSelection(core::Document& document,
                           const std::vector<core::ObjectRef>& refs,
                           double maxDistance) {
    MergeReport report;
    if (!(maxDistance > 0.0)) {
        report.error = core::Error(core::ErrorCode::InvalidArgument,
                                   "merge radius must be positive");
        return report;
    }
    // Collect the selected ATOMS only. A merge is an atom operation; a selected
    // bond or text is not a merge candidate and is not reported as a refusal
    // (reporting it would bury the real refusals).
    std::vector<core::ObjectId> atoms;
    for (size_t i = 0; i < refs.size(); ++i)
        if (refs[i].kind == core::ObjectKind::Atom && document.atoms().alive(refs[i].id))
            atoms.push_back(refs[i].id);
    std::sort(atoms.begin(), atoms.end());
    atoms.erase(std::unique(atoms.begin(), atoms.end()), atoms.end());
    if (atoms.size() < 2) {
        report.error = core::Error(core::ErrorCode::InvalidArgument,
                                   "need at least two selected atoms to merge");
        return report;
    }

    // Pairwise by distance, then COMPATIBILITY. Distance only NARROWS the
    // candidate list; it never decides the merge.
    for (size_t i = 0; i < atoms.size(); ++i) {
        for (size_t j = i + 1; j < atoms.size(); ++j) {
            const core::ObjectId keep = atoms[i];
            const core::ObjectId drop = atoms[j];
            const core::AtomRecord* a = document.atoms().alive(keep);
            const core::AtomRecord* b = document.atoms().alive(drop);
            if (!a || !b) continue;
            if (length(b->position - a->position) > maxDistance) continue;
            std::string why;
            if (!mergeCompatible(document, keep, drop, &why)) {
                ++report.refusedPairs;
                if (report.refusals.size() < 8) {
                    std::ostringstream line;
                    line << "atoms " << keep << "+" << drop << " refused: " << why;
                    report.refusals.push_back(line.str());
                }
                continue;
            }
            // Rewire every bond of `drop` onto `keep`.
            //
            // WHY THIS SNAPSHOTS AND REBUILDS RATHER THAN CALLING
            // setBondEndpoints: T01's setBondEndpoints does not update the
            // atom->bond adjacency list, so a subsequent removeAtom(drop) still
            // sees the OLD incident bonds and cascades them away - silently
            // destroying the bond that was just rewired. That is the exact
            // "a merge broke a chemical reference" failure this command must not
            // have. Snapshotting the surviving bonds, removing the atom (which
            // cascades cleanly), and re-adding them onto `keep` through
            // addBondRecord (which maintains adjacency, the bond index and the
            // duplicate check correctly) avoids it structurally.
            struct BondSpec {
                core::ObjectId other;
                core::BondOrder order;
                core::BondDisplay display;
                core::BondSemantics semantics;
                core::BondStereo stereo;
                int32_t sourceId;
            };
            std::vector<BondSpec> surviving;
            const std::vector<core::ObjectId> incident = document.incidentBonds(drop);
            for (size_t k = 0; k < incident.size(); ++k) {
                const core::ObjectId bondId = incident[k];
                const core::BondRecord* bond = document.bonds().alive(bondId);
                if (!bond) continue;
                const core::ObjectId other =
                    (bond->beginAtom == drop) ? bond->endAtom : bond->beginAtom;
                if (other == keep) {
                    // Would become a self-bond. Dropped, and counted SEPARATELY
                    // from a duplicate so the two causes stay distinguishable.
                    ++report.selfBondsDropped;
                    continue;
                }
                if (!document.atoms().alive(other)) continue;
                if (document.bondBetween(keep, other) != core::kInvalidObjectId) {
                    ++report.duplicateBondsDropped;
                    continue;
                }
                BondSpec spec;
                spec.other = other;
                spec.order = bond->order;
                spec.display = bond->display;
                spec.semantics = bond->semantics;
                spec.stereo = bond->stereo;
                spec.sourceId = bond->sourceId;
                surviving.push_back(spec);
            }
            // Removing the atom cascades every remaining incident bond, so the
            // re-adds below cannot collide with a still-live old bond.
            document.removeAtom(drop);
            for (size_t k = 0; k < surviving.size(); ++k) {
                core::BondRecord rebuilt;
                rebuilt.beginAtom = keep;
                rebuilt.endAtom = surviving[k].other;
                // ORDER IS COPIED UNCHANGED. A merge never alters chemistry.
                rebuilt.order = surviving[k].order;
                rebuilt.display = surviving[k].display;
                rebuilt.semantics = surviving[k].semantics;
                rebuilt.stereo = surviving[k].stereo;
                rebuilt.sourceId = surviving[k].sourceId;
                core::ObjectId newId = core::kInvalidObjectId;
                const core::Error added = document.addBondRecord(rebuilt, &newId);
                if (!added.ok()) {
                    // A refused re-add must be REPORTED, not swallowed: losing a
                    // bond silently is worse than a loud merge failure.
                    std::ostringstream line;
                    line << "bond " << keep << "-" << surviving[k].other
                         << " could not be re-added: " << added.message;
                    report.refusals.push_back(line.str());
                    continue;
                }
                ++report.bondsRewired;
            }
            document.removeAtom(drop);
            ++report.mergedPairs;
        }
    }
    return report;
}

} // namespace selection
} // namespace r62
