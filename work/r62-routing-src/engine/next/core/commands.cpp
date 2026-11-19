#include "commands.hpp"
#include "tool_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace r62 {
namespace core {

// ===========================================================================
// Record comparison helpers
// ===========================================================================
namespace {

// Field-by-field comparison. Deliberately NOT memcmp: the records carry reserved
// padding bytes that are not part of the logical state, and a memcmp-based diff
// would both report spurious changes and copy uninitialised padding into the
// patch, which would make the document digest unstable across runs.
inline bool sameRecord(const AtomRecord& a, const AtomRecord& b) {
    return a.id == b.id && a.x() == b.x() && a.y() == b.y() &&
           a.element == b.element && a.alive == b.alive &&
           a.sourceId == b.sourceId && a.semantics == b.semantics;
}
inline bool sameRecord(const BondRecord& a, const BondRecord& b) {
    return a.id == b.id && a.beginAtom == b.beginAtom && a.endAtom == b.endAtom &&
           a.order == b.order && a.display == b.display &&
           a.semantics == b.semantics && a.stereo == b.stereo &&
           a.alive == b.alive && a.sourceId == b.sourceId;
}
inline bool sameRecord(const TextRecord& a, const TextRecord& b) {
    return a.id == b.id && a.x() == b.x() && a.y() == b.y() && a.size == b.size &&
           a.role == b.role && a.alive == b.alive && a.sourceId == b.sourceId &&
           a.format == b.format;
}
inline bool sameRecord(const PathRecord& a, const PathRecord& b) {
    return a.id == b.id && a.alive == b.alive && a.closed == b.closed &&
           a.sourceId == b.sourceId && a.style == b.style &&
           a.fillRGBA == b.fillRGBA && a.fillEnabled == b.fillEnabled &&
           a.points == b.points && a.pointKind == b.pointKind;
}
inline bool sameRecord(const AnnotationRecord& a, const AnnotationRecord& b) {
    if (a.id != b.id || a.alive != b.alive || a.kind != b.kind ||
        a.presetId != b.presetId || a.sourceId != b.sourceId) return false;
    if (a.scale != b.scale || a.rotation != b.rotation) return false;
    if (a.style != b.style || a.fillRGBA != b.fillRGBA) return false;
    for (int i = 0; i < 4; ++i) {
        if (a.anchors[i] != b.anchors[i]) return false;
        if (a.geometry[i].x != b.geometry[i].x ||
            a.geometry[i].y != b.geometry[i].y) return false;
    }
    return true;
}
inline bool sameRecord(const TableRecord& a, const TableRecord& b) {
    return a.id == b.id && a.alive == b.alive && a.sourceId == b.sourceId &&
           a.rows == b.rows && a.columns == b.columns &&
           a.rowHeight == b.rowHeight && a.cells == b.cells &&
           a.frame.min.x == b.frame.min.x && a.frame.min.y == b.frame.min.y &&
           a.frame.max.x == b.frame.max.x && a.frame.max.y == b.frame.max.y;
}
inline bool sameRecord(const GroupRecord& a, const GroupRecord& b) {
    return a.id == b.id && a.alive == b.alive && a.sourceId == b.sourceId &&
           a.zIndex == b.zIndex && a.members == b.members && a.label == b.label;
}

// A record that does not exist on one side of a change. The surviving side's
// structural fields are preserved so the dead placeholder stays a faithful
// description of "the same object, now gone" rather than a fresh default object.
template <typename T>
T deadLike(const T* source, ObjectId id) {
    T dead;
    if (source) dead = *source;
    dead.id = id;
    dead.alive = 0;
    return dead;
}

// Build contiguous runs for every id that actually changed. Two directions are
// produced from one comparison pass:
//   before -> the record as it was in the session baseline
//   after  -> the record as it is now
// Runs are the point: moving 5,000 consecutive atoms yields a handful of
// spans instead of 5,000 individually allocated record objects.
template <typename T, typename Slab>
void buildRuns(const std::vector<ObjectId>& ids,
               const Slab& slab,
               const std::vector<T>& baseline,
               std::vector<RecordRun<T> >* before,
               std::vector<RecordRun<T> >* after,
               size_t* maxRunLength) {
    if (ids.empty()) return;
    std::vector<ObjectId> sorted(ids);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    std::vector<ObjectId> changed;
    changed.reserve(sorted.size());
    for (size_t i = 0; i < sorted.size(); ++i) {
        const ObjectId id = sorted[i];
        const T* now = slab.get(id);          // null when the slab never grew
        const bool hasBefore = id < baseline.size();
        if (!now) {
            if (hasBefore && baseline[id].isAlive()) changed.push_back(id);
            continue;
        }
        if (!hasBefore) { changed.push_back(id); continue; }
        if (!sameRecord(baseline[id], *now)) changed.push_back(id);
    }
    if (changed.empty()) return;

    size_t i = 0;
    while (i < changed.size()) {
        size_t j = i + 1;
        while (j < changed.size() && changed[j] == changed[j - 1] + 1) ++j;
        RecordRun<T> runBefore;
        RecordRun<T> runAfter;
        runBefore.offset = changed[i];
        runAfter.offset = changed[i];
        // A run is emitted per slab, so both directions must cover the SAME ids.
        // For an id that exists on only one side, the other side carries a dead
        // placeholder built from the surviving record, so re-applying the patch
        // cannot fabricate structural fields (a bond's endpoints, a table's
        // dimensions) out of a default-constructed object.
        for (size_t k = i; k < j; ++k) {
            const ObjectId id = changed[k];
            const T* now = slab.get(id);
            const T* was = id < baseline.size() ? &baseline[id] : 0;
            runAfter.records.push_back(now ? *now : deadLike(was, id));
            runBefore.records.push_back(was ? *was : deadLike(now, id));
        }
        before->push_back(runBefore);
        after->push_back(runAfter);
        if (maxRunLength && runAfter.records.size() > *maxRunLength)
            *maxRunLength = runAfter.records.size();
        i = j;
    }
}

// Resize a slab to the length recorded for this side, then write the runs.
// Resizing FIRST is what makes undo remove a slot the command created, so the
// document returns to its exact pre-command shape rather than to "same objects
// plus a few dead slots".
// Apply one direction of a slab patch.
//
// Two rules make the result exactly reproduce the recorded side:
//   1. The slab is resized to the side's recorded length first. This drops the
//      slots the other direction created.
//   2. A record whose id falls OUTSIDE the recorded length is not written at
//      all. A run may span ids that only exist in the other direction, and
//      writing them here would re-grow the slab and undo rule 1. Ids at or past
//      the recorded length simply do not exist on this side.
template <typename T>
void writeRunsResized(const std::vector<RecordRun<T> >& runs, std::vector<T>& records,
                      uint32_t size) {
    records.resize(static_cast<size_t>(size));
    for (size_t i = 0; i < runs.size(); ++i) {
        const RecordRun<T>& run = runs[i];
        for (size_t k = 0; k < run.records.size(); ++k) {
            const size_t index = static_cast<size_t>(run.offset) + k;
            if (index >= records.size()) continue;   // does not exist on this side
            records[index] = run.records[k];
            records[index].id = static_cast<ObjectId>(index);
        }
    }
}

template <typename T>
size_t countRunRecords(const std::vector<RecordRun<T> >& runs) {
    size_t n = 0;
    for (size_t i = 0; i < runs.size(); ++i) n += runs[i].records.size();
    return n;
}

template <typename T>
uint64_t runBytes(const std::vector<RecordRun<T> >& runs) {
    uint64_t n = 0;
    for (size_t i = 0; i < runs.size(); ++i) n += runs[i].bytes();
    return n;
}

} // namespace

// ===========================================================================
// SlabPatch
// ===========================================================================
size_t SlabPatch::runCount() const {
    size_t n = 0;
    n += atomsBefore.size() + atomsAfter.size();
    n += bondsBefore.size() + bondsAfter.size();
    n += textsBefore.size() + textsAfter.size();
    n += pathsBefore.size() + pathsAfter.size();
    n += arrowsBefore.size() + arrowsAfter.size();
    n += bracketsBefore.size() + bracketsAfter.size();
    n += orbitalsBefore.size() + orbitalsAfter.size();
    n += symbolsBefore.size() + symbolsAfter.size();
    n += bioShapesBefore.size() + bioShapesAfter.size();
    n += tablesBefore.size() + tablesAfter.size();
    n += groupsBefore.size() + groupsAfter.size();
    n += textRunsBefore.size() + textRunsAfter.size();
    return n;
}

size_t SlabPatch::recordCount() const {
    size_t n = 0;
    n += countRunRecords(atomsAfter) + countRunRecords(atomsBefore);
    n += countRunRecords(bondsAfter) + countRunRecords(bondsBefore);
    n += countRunRecords(textsAfter) + countRunRecords(textsBefore);
    n += countRunRecords(pathsAfter) + countRunRecords(pathsBefore);
    n += countRunRecords(arrowsAfter) + countRunRecords(arrowsBefore);
    n += countRunRecords(bracketsAfter) + countRunRecords(bracketsBefore);
    n += countRunRecords(orbitalsAfter) + countRunRecords(orbitalsBefore);
    n += countRunRecords(symbolsAfter) + countRunRecords(symbolsBefore);
    n += countRunRecords(bioShapesAfter) + countRunRecords(bioShapesBefore);
    n += countRunRecords(tablesAfter) + countRunRecords(tablesBefore);
    n += countRunRecords(groupsAfter) + countRunRecords(groupsBefore);
    n += countRunRecords(textRunsAfter) + countRunRecords(textRunsBefore);
    return n;
}

// ===========================================================================
// ChangeSet
// ===========================================================================
bool ChangeSet::empty() const {
    return forward.runCount() == 0 && reverse.runCount() == 0 &&
           extensions.empty() && !defaults.present;
}

uint64_t ChangeSet::byteSize() const {
    uint64_t n = 0;
    n += runBytes(forward.atomsBefore) + runBytes(forward.atomsAfter);
    n += runBytes(forward.bondsBefore) + runBytes(forward.bondsAfter);
    n += runBytes(forward.textsBefore) + runBytes(forward.textsAfter);
    n += runBytes(forward.pathsBefore) + runBytes(forward.pathsAfter);
    n += runBytes(forward.arrowsBefore) + runBytes(forward.arrowsAfter);
    n += runBytes(forward.bracketsBefore) + runBytes(forward.bracketsAfter);
    n += runBytes(forward.orbitalsBefore) + runBytes(forward.orbitalsAfter);
    n += runBytes(forward.symbolsBefore) + runBytes(forward.symbolsAfter);
    n += runBytes(forward.bioShapesBefore) + runBytes(forward.bioShapesAfter);
    n += runBytes(forward.tablesBefore) + runBytes(forward.tablesAfter);
    n += runBytes(forward.groupsBefore) + runBytes(forward.groupsAfter);
    for (size_t i = 0; i < forward.textRunsAfter.size(); ++i) {
        n += sizeof(uint32_t);
        for (size_t k = 0; k < forward.textRunsAfter[i].records.size(); ++k) {
            n += sizeof(TextRun);
            n += forward.textRunsAfter[i].records[k].utf8.size();
            n += forward.textRunsAfter[i].records[k].fontName.size();
        }
    }
    for (size_t i = 0; i < extensions.size(); ++i) {
        const std::vector<ExtensionEntry>& b = extensions[i].before.entries();
        const std::vector<ExtensionEntry>& a = extensions[i].after.entries();
        for (size_t k = 0; k < b.size(); ++k) n += b[k].key.size() + b[k].value.size() + 16;
        for (size_t k = 0; k < a.size(); ++k) n += a[k].key.size() + a[k].value.size() + 16;
    }
    return n;
}

uint64_t ChangeSet::recordCount() const {
    return forward.recordCount();
}

size_t ChangeSet::runCount() const {
    return forward.runCount() + extensions.size();
}

Error ChangeSet::apply(Document& document, bool after) {
    const SlabPatch& patch = after ? forward : reverse;
    // Order matters: atoms before bonds so a bond never briefly references an
    // atom that does not exist yet. Slabs are resized first so a slot created by
    // the command disappears again on reverse.
    writeRunsResized(patch.atomsAfter, document.atoms().mutableRecords(),
                     patch.atomsSize);
    writeRunsResized(patch.textsAfter, document.texts().mutableRecords(),
                     patch.textsSize);
    writeRunsResized(patch.bondsAfter, document.bonds().mutableRecords(),
                     patch.bondsSize);
    writeRunsResized(patch.pathsAfter, document.paths().mutableRecords(),
                     patch.pathsSize);
    writeRunsResized(patch.arrowsAfter, document.arrows().mutableRecords(),
                     patch.annotationSizes[0]);
    writeRunsResized(patch.bracketsAfter, document.brackets().mutableRecords(),
                     patch.annotationSizes[1]);
    writeRunsResized(patch.orbitalsAfter, document.orbitals().mutableRecords(),
                     patch.annotationSizes[2]);
    writeRunsResized(patch.symbolsAfter, document.symbols().mutableRecords(),
                     patch.annotationSizes[3]);
    writeRunsResized(patch.bioShapesAfter, document.bioShapes().mutableRecords(),
                     patch.annotationSizes[4]);
    writeRunsResized(patch.tablesAfter, document.tables().mutableRecords(),
                     patch.tablesSize);
    writeRunsResized(patch.groupsAfter, document.groups().mutableRecords(),
                     patch.groupsSize);

    for (size_t i = 0; i < patch.textRunsAfter.size(); ++i) {
        const RecordRun<TextRun>& run = patch.textRunsAfter[i];
        for (size_t k = 0; k < run.records.size(); ++k) {
            const ObjectId id = static_cast<ObjectId>(run.offset) +
                                static_cast<ObjectId>(k);
            std::vector<TextRun> slice;
            slice.push_back(run.records[k]);
            document.setRunsQuiet(id, slice);
        }
    }
    if (defaults.present)
        document.setDefaultsQuiet(after ? defaults.after : defaults.before);

    for (size_t i = 0; i < extensions.size(); ++i) {
        const ExtensionPatch& ext = extensions[i];
        const bool present = after ? ext.afterPresent : ext.beforePresent;
        const ExtensionBag& bag = after ? ext.after : ext.before;
        const bool documentLevel =
            ext.ref.kind == ObjectKind::Unknown && ext.ref.id == kInvalidObjectId;
        if (documentLevel) {
            document.mutableObjectExtensions().erase(ext.ref);
            if (present) document.documentExtensions() = bag;
            else document.documentExtensions().clear();
        } else if (present) {
            document.extensions(ext.ref) = bag;
        } else {
            document.mutableObjectExtensions().erase(ext.ref);
        }
    }
    document.rebuildDerived();
    return document.validate();
}
// ===========================================================================
// DocumentSession
// ===========================================================================
DocumentSession::DocumentSession()
    : document_(0), tag_(""), defaultsDirty_(false), extensionsDirty_(false),
      structureDirty_(false) {}

DocumentSession::DocumentSession(Document& document, const std::string& tag)
    : document_(0), tag_(tag), defaultsDirty_(false), extensionsDirty_(false),
      structureDirty_(false) {
    Error r = attach(document, tag);
    (void)r;
}

DocumentSession::~DocumentSession() { detach(); }

DocumentSession::DocumentSession(DocumentSession&& other)
    : document_(0), tag_(other.tag_), dirty_(other.dirty_),
      extensionRefs_(other.extensionRefs_), defaultsDirty_(other.defaultsDirty_),
      extensionsDirty_(other.extensionsDirty_), structureDirty_(other.structureDirty_),
      baseline_(other.baseline_) {
    if (other.document_) {
        document_ = other.document_;
        document_->setSession(this);
        other.document_ = 0;
    }
}

DocumentSession& DocumentSession::operator=(DocumentSession&& other) {
    if (this == &other) return *this;
    detach();
    tag_ = other.tag_;
    dirty_ = other.dirty_;
    extensionRefs_ = other.extensionRefs_;
    defaultsDirty_ = other.defaultsDirty_;
    extensionsDirty_ = other.extensionsDirty_;
    structureDirty_ = other.structureDirty_;
    baseline_ = other.baseline_;
    if (other.document_) {
        document_ = other.document_;
        document_->setSession(this);
        other.document_ = 0;
    }
    return *this;
}

Error DocumentSession::attach(Document& document, const std::string& tag) {
    if (document_) return Error(ErrorCode::AlreadyExists, "A session is already attached");
    if (document.session() != 0)
        return Error(ErrorCode::AlreadyExists, "The document already has a recording session");
    document_ = &document;
    tag_ = tag;
    dirty_.clear();
    extensionRefs_.clear();
    defaultsDirty_ = false;
    extensionsDirty_ = false;
    structureDirty_ = false;
    document.captureAll(&baseline_);
    document.setSession(this);
    return Error::success();
}

void DocumentSession::detach() {
    if (!document_) return;
    if (document_->session() == this) document_->setSession(0);
    document_ = 0;
}

void DocumentSession::buildChangeSet(ChangeSet* out, size_t* maxRunLength) const {
    *out = ChangeSet();
    size_t localMax = 0;
    if (!document_) {
        if (maxRunLength) *maxRunLength = 0;
        return;
    }
    const Document& doc = *document_;
    SlabPatch& forward = out->forward;
    SlabPatch& reverse = out->reverse;

    // Forward = the shape after the command; reverse = the shape before it.
    forward.atomsSize = static_cast<uint32_t>(doc.atoms().size());
    forward.bondsSize = static_cast<uint32_t>(doc.bonds().size());
    forward.textsSize = static_cast<uint32_t>(doc.texts().size());
    forward.pathsSize = static_cast<uint32_t>(doc.paths().size());
    forward.tablesSize = static_cast<uint32_t>(doc.tables().size());
    forward.groupsSize = static_cast<uint32_t>(doc.groups().size());
    forward.annotationSizes[0] = static_cast<uint32_t>(doc.arrows().size());
    forward.annotationSizes[1] = static_cast<uint32_t>(doc.brackets().size());
    forward.annotationSizes[2] = static_cast<uint32_t>(doc.orbitals().size());
    forward.annotationSizes[3] = static_cast<uint32_t>(doc.symbols().size());
    forward.annotationSizes[4] = static_cast<uint32_t>(doc.bioShapes().size());
    reverse.atomsSize = static_cast<uint32_t>(baseline_.atoms.size());
    reverse.bondsSize = static_cast<uint32_t>(baseline_.bonds.size());
    reverse.textsSize = static_cast<uint32_t>(baseline_.texts.size());
    reverse.pathsSize = static_cast<uint32_t>(baseline_.paths.size());
    reverse.tablesSize = static_cast<uint32_t>(baseline_.tables.size());
    reverse.groupsSize = static_cast<uint32_t>(baseline_.groups.size());
    for (int s = 0; s < 5; ++s)
        reverse.annotationSizes[s] =
            static_cast<uint32_t>(baseline_.annotations[s].size());

    std::vector<ObjectId> atomIds, bondIds, textIds, pathIds;
    std::vector<ObjectId> arrowIds, bracketIds, orbitalIds, symbolIds, bioIds;
    std::vector<ObjectId> tableIds, groupIds;
    for (size_t i = 0; i < dirty_.size(); ++i) {
        switch (dirty_[i].kind) {
        case ObjectKind::Atom: atomIds.push_back(dirty_[i].id); break;
        case ObjectKind::Bond: bondIds.push_back(dirty_[i].id); break;
        case ObjectKind::Text: textIds.push_back(dirty_[i].id); break;
        case ObjectKind::Path: pathIds.push_back(dirty_[i].id); break;
        case ObjectKind::Arrow: arrowIds.push_back(dirty_[i].id); break;
        case ObjectKind::Bracket: bracketIds.push_back(dirty_[i].id); break;
        case ObjectKind::Orbital: orbitalIds.push_back(dirty_[i].id); break;
        case ObjectKind::Symbol: symbolIds.push_back(dirty_[i].id); break;
        case ObjectKind::BioShape: bioIds.push_back(dirty_[i].id); break;
        case ObjectKind::Table: tableIds.push_back(dirty_[i].id); break;
        case ObjectKind::Group: groupIds.push_back(dirty_[i].id); break;
        default: break;
        }
    }

    // Forward runs are the records as they are now; reverse runs are the same
    // ids as they were in the session baseline. Both come from one pass.
    buildRuns(atomIds, doc.atoms(), baseline_.atoms,
              &reverse.atomsAfter, &forward.atomsAfter, &localMax);
    buildRuns(bondIds, doc.bonds(), baseline_.bonds,
              &reverse.bondsAfter, &forward.bondsAfter, &localMax);
    buildRuns(textIds, doc.texts(), baseline_.texts,
              &reverse.textsAfter, &forward.textsAfter, &localMax);
    buildRuns(pathIds, doc.paths(), baseline_.paths,
              &reverse.pathsAfter, &forward.pathsAfter, &localMax);
    buildRuns(arrowIds, doc.arrows(), baseline_.annotations[0],
              &reverse.arrowsAfter, &forward.arrowsAfter, &localMax);
    buildRuns(bracketIds, doc.brackets(), baseline_.annotations[1],
              &reverse.bracketsAfter, &forward.bracketsAfter, &localMax);
    buildRuns(orbitalIds, doc.orbitals(), baseline_.annotations[2],
              &reverse.orbitalsAfter, &forward.orbitalsAfter, &localMax);
    buildRuns(symbolIds, doc.symbols(), baseline_.annotations[3],
              &reverse.symbolsAfter, &forward.symbolsAfter, &localMax);
    buildRuns(bioIds, doc.bioShapes(), baseline_.annotations[4],
              &reverse.bioShapesAfter, &forward.bioShapesAfter, &localMax);
    buildRuns(tableIds, doc.tables(), baseline_.tables,
              &reverse.tablesAfter, &forward.tablesAfter, &localMax);
    buildRuns(groupIds, doc.groups(), baseline_.groups,
              &reverse.groupsAfter, &forward.groupsAfter, &localMax);

    // Styled text runs are diffed per text id, in both directions.
    {
        std::vector<ObjectId> runIds(textIds);
        std::sort(runIds.begin(), runIds.end());
        runIds.erase(std::unique(runIds.begin(), runIds.end()), runIds.end());
        for (size_t i = 0; i < runIds.size(); ++i) {
            const ObjectId id = runIds[i];
            const std::vector<TextRun> now = doc.runs(id);
            std::map< ObjectId, std::vector<TextRun> >::const_iterator it =
                baseline_.runs.find(id);
            const bool hadBefore = it != baseline_.runs.end();
            const std::vector<TextRun> was = hadBefore
                ? it->second : std::vector<TextRun>();
            if (hadBefore && was == now) continue;
            RecordRun<TextRun> afterRun;
            afterRun.offset = id;
            afterRun.records = now;
            forward.textRunsAfter.push_back(afterRun);
            RecordRun<TextRun> beforeRun;
            beforeRun.offset = id;
            beforeRun.records = was;
            reverse.textRunsAfter.push_back(beforeRun);
        }
    }

    if (defaultsDirty_) {
        DefaultsPatch patch;
        patch.present = true;
        patch.before = baseline_.documentDefaults;
        patch.after.bondLength = doc.defaultBondLength();
        patch.after.lineWidth = doc.defaultLineWidth();
        patch.after.textSize = doc.defaultTextSize();
        patch.after.font = doc.defaultFont();
        if (patch.before != patch.after) out->defaults = patch;
    }

    if (extensionsDirty_) {
        std::vector<ObjectRef> refs(extensionRefs_);
        std::sort(refs.begin(), refs.end());
        refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
        for (size_t i = 0; i < refs.size(); ++i) {
            const ObjectRef& ref = refs[i];
            ExtensionPatch patch;
            patch.ref = ref;
            const bool documentLevel =
                ref.kind == ObjectKind::Unknown && ref.id == kInvalidObjectId;
            if (documentLevel) {
                patch.beforePresent = !baseline_.documentExtensions.empty();
                patch.before = baseline_.documentExtensions;
                patch.afterPresent = !doc.documentExtensions().empty();
                patch.after = doc.documentExtensions();
            } else {
                std::map<ObjectRef, ExtensionBag>::const_iterator it =
                    baseline_.extensions.find(ref);
                patch.beforePresent = it != baseline_.extensions.end();
                if (patch.beforePresent) patch.before = it->second;
                const ExtensionBag& now = doc.extensions(ref);
                patch.afterPresent = !now.empty();
                patch.after = now;
            }
            if (patch.beforePresent != patch.afterPresent || patch.before != patch.after)
                out->extensions.push_back(patch);
        }
    }
    if (maxRunLength) *maxRunLength = localMax;
}

void DocumentSession::resetBaseline() {
    if (!document_) return;
    document_->captureAll(&baseline_);
    dirty_.clear();
    extensionRefs_.clear();
    defaultsDirty_ = false;
    extensionsDirty_ = false;
    structureDirty_ = false;
}

DocumentSession* documentSession(const Document* document) {
    return document ? document->session() : 0;
}

} // namespace core
} // namespace r62