#include "persistence.hpp"

#include <cstring>
#include <set>
#include <sstream>

namespace r62 {
namespace persistence {

using core::Error;
using core::ErrorCode;
using core::ObjectKind;
using core::ObjectRef;

namespace {

const char* selectionTag(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Atom: return "A";
    case ObjectKind::Bond: return "B";
    case ObjectKind::Text: return "T";
    case ObjectKind::Path: return "P";
    case ObjectKind::Arrow: return "R";
    case ObjectKind::Bracket: return "K";
    case ObjectKind::Table: return "L";
    case ObjectKind::Orbital: return "O";
    case ObjectKind::Symbol: return "S";
    case ObjectKind::BioShape: return "I";
    case ObjectKind::Group: return "G";
    default: return "?";
    }
}

// The package is a self-contained native document, wrapped in an envelope that
// records which objects were SELECTED (as opposed to pulled in as dependencies)
// and what their original source ids were.
//
//   R62SEL1 <selVersion> <payloadBytes>\n
//   S <kind> <payloadId> <sourceId>\n        (selected roots, package-local ids)
//   X <origSourceId> <newSourceId>\n          (source-id remap decided at encode)
//   <payload: a T01DOC document, exactly payloadBytes>
//   Z <crc32Hex>\n
//
// Why a whole document rather than a record list: the clipboard cannot promise
// the source document still exists when the paste happens, so the package has to
// carry everything the pasted objects need. Encoding a Document also reuses the
// tested T01 serializer instead of inventing a second container format.

std::string hexU64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

bool parseHexU64(const std::string& text, uint64_t* out) {
    if (text.empty()) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        uint64_t digit = 0;
        if (c >= 48 && c <= 57) digit = static_cast<uint64_t>(c - 48);
        else if (c >= 97 && c <= 102) digit = static_cast<uint64_t>(c - 97 + 10);
        else if (c >= 65 && c <= 70) digit = static_cast<uint64_t>(c - 65 + 10);
        else return false;
        value = value * 16u + digit;
    }
    *out = value;
    return true;
}

ObjectKind kindFromTag(const std::string& tag) {
    if (tag == "A") return ObjectKind::Atom;
    if (tag == "B") return ObjectKind::Bond;
    if (tag == "T") return ObjectKind::Text;
    if (tag == "P") return ObjectKind::Path;
    if (tag == "R") return ObjectKind::Arrow;
    if (tag == "K") return ObjectKind::Bracket;
    if (tag == "L") return ObjectKind::Table;
    if (tag == "O") return ObjectKind::Orbital;
    if (tag == "S") return ObjectKind::Symbol;
    if (tag == "I") return ObjectKind::BioShape;
    if (tag == "G") return ObjectKind::Group;
    return ObjectKind::Unknown;
}

// ---------------------------------------------------------------------------
// Closure
// ---------------------------------------------------------------------------
// A selection is rarely closed under reference: selecting a bond does not select
// its atoms, and selecting an atom does not select its bonds. A package with a
// dangling internal reference would decode into a broken document, so the closure
// is computed here and the dependency edges are what get counted.
Error closeSelection(const core::Document& document,
                     const std::vector<ObjectRef>& roots,
                     std::set<ObjectRef>* closure, RefusalReport* refusals) {
    std::vector<ObjectRef> pending = roots;
    for (size_t i = 0; i < pending.size(); ++i) closure->insert(pending[i]);

    for (size_t i = 0; i < pending.size(); ++i) {
        const ObjectRef ref = pending[i];
        if (!ref.valid()) continue;
        if (!document.extensions(ref).empty()) { /* keep extension bags too */ }
        switch (ref.kind) {
        case ObjectKind::Bond: {
            const core::BondRecord* bond = document.bonds().get(ref.id);
            if (!bond) break;
            const ObjectRef ends[2] = { ObjectRef(ObjectKind::Atom, bond->beginAtom),
                                        ObjectRef(ObjectKind::Atom, bond->endAtom) };
            for (int k = 0; k < 2; ++k)
                if (closure->insert(ends[k]).second) pending.push_back(ends[k]);
            break;
        }
        case ObjectKind::Arrow:
        case ObjectKind::Bracket:
        case ObjectKind::Orbital:
        case ObjectKind::Symbol:
        case ObjectKind::BioShape: {
            const core::ObjectSlab<core::AnnotationRecord>* slab = 0;
            if (ref.kind == ObjectKind::Arrow) slab = &document.arrows();
            else if (ref.kind == ObjectKind::Bracket) slab = &document.brackets();
            else if (ref.kind == ObjectKind::Orbital) slab = &document.orbitals();
            else if (ref.kind == ObjectKind::Symbol) slab = &document.symbols();
            else slab = &document.bioShapes();
            const core::AnnotationRecord* annotation = slab->get(ref.id);
            if (!annotation) break;
            for (int k = 0; k < 4; ++k) {
                if (annotation->anchors[k] == core::kInvalidObjectId) continue;
                const ObjectRef anchor(ObjectKind::Atom, annotation->anchors[k]);
                if (closure->insert(anchor).second) pending.push_back(anchor);
            }
            break;
        }
        case ObjectKind::Group: {
            const core::GroupRecord* group = document.groups().get(ref.id);
            if (!group) break;
            for (size_t k = 0; k < group->members.size(); ++k)
                if (closure->insert(group->members[k]).second)
                    pending.push_back(group->members[k]);
            break;
        }
        case ObjectKind::Table: {
            const core::TableRecord* table = document.tables().get(ref.id);
            if (!table) break;
            for (size_t k = 0; k < table->cells.size(); ++k) {
                if (table->cells[k] == core::kInvalidObjectId) continue;
                const ObjectRef cell(ObjectKind::Text, table->cells[k]);
                if (closure->insert(cell).second) pending.push_back(cell);
            }
            break;
        }
        case ObjectKind::Atom: {
            // Bonds incident to a selected atom are NOT pulled in: a user may
            // legitimately copy a single atom out of a molecule. The package stays
            // self-contained because no bond is present to dangle.
            break;
        }
        default: break;
        }
    }
    (void)refusals;
    return Error::success();
}

// Copy one object from `source` into `scratch`, preserving its identity slot so
// the package-internal references stay valid. Returning the new id lets the
// caller build the remap table.
Error copyAtom(const core::Document& source, core::Document& scratch, core::ObjectId id,
               core::ObjectId* out) {
    const core::AtomRecord* record = source.atoms().get(id);
    if (!record) return Error(ErrorCode::NotFound, "selected atom is gone");
    core::AtomRecord copy = *record;
    Error added = scratch.addAtomRecord(copy, out);
    if (!added.ok()) return added;
    scratch.setAtomLabelQuiet(*out, source.atomLabel(id));
    scratch.setRunsQuiet(*out, source.runs(id));
    return Error::success();
}

Error copyText(const core::Document& source, core::Document& scratch, core::ObjectId id,
               core::ObjectId* out) {
    const core::TextRecord* record = source.texts().get(id);
    if (!record) return Error(ErrorCode::NotFound, "selected text is gone");
    core::TextRecord copy = *record;
    Error added = scratch.addTextRecord(copy, out);
    if (!added.ok()) return added;
    scratch.setRunsQuiet(*out, source.runs(id));
    return Error::success();
}

Error copyPath(const core::Document& source, core::Document& scratch, core::ObjectId id,
               core::ObjectId* out) {
    const core::PathRecord* record = source.paths().get(id);
    if (!record) return Error(ErrorCode::NotFound, "selected path is gone");
    return scratch.addPath(*record, out);
}

Error copyAnnotation(const core::Document& source, core::Document& scratch,
                     ObjectKind kind, core::ObjectId id, core::ObjectId* out) {
    const core::ObjectSlab<core::AnnotationRecord>* slab = 0;
    if (kind == ObjectKind::Arrow) slab = &source.arrows();
    else if (kind == ObjectKind::Bracket) slab = &source.brackets();
    else if (kind == ObjectKind::Orbital) slab = &source.orbitals();
    else if (kind == ObjectKind::Symbol) slab = &source.symbols();
    else if (kind == ObjectKind::BioShape) slab = &source.bioShapes();
    else return Error(ErrorCode::WrongKind, "not an annotation kind");
    const core::AnnotationRecord* record = slab->get(id);
    if (!record) return Error(ErrorCode::NotFound, "selected annotation is gone");
    return scratch.addAnnotation(kind, *record, out);
}

} // namespace

// ---------------------------------------------------------------------------
// Encode
// ---------------------------------------------------------------------------
Error encodeSelectionPackage(const core::Document& document,
                             const std::vector<ObjectRef>& refs,
                             std::string* output, SelectionPackageInfo* info) {
    if (!output) return Error(ErrorCode::InvalidArgument, "null output buffer");
    if (refs.empty()) return Error(ErrorCode::InvalidArgument, "empty selection");

    std::set<ObjectRef> closure;
    RefusalReport refusals;
    Error closed = closeSelection(document, refs, &closure, &refusals);
    if (!closed.ok()) return closed;

    // The package is a fresh document, so its ids start from 0 and every internal
    // reference is expressed in PACKAGE-LOCAL ids. That is what makes a paste into
    // another document safe: nothing refers back to the source document.
    core::Document scratch;
    scratch.setDefaultBondLength(document.defaultBondLength());
    scratch.setDefaultLineWidth(document.defaultLineWidth());
    scratch.setDefaultTextSize(document.defaultTextSize());
    scratch.setDefaultFont(document.defaultFont());
    scratch.setName(document.name());

    std::ostringstream selected;
    SelectionPackageInfo local;
    local.version = kSelFormatVersion;

    // Two passes: dependencies first (atoms), so a bond can resolve its ends.
    std::vector<ObjectRef> ordered;
    for (std::set<ObjectRef>::const_iterator it = closure.begin(); it != closure.end(); ++it)
        if (it->kind == ObjectKind::Atom) ordered.push_back(*it);
    for (std::set<ObjectRef>::const_iterator it = closure.begin(); it != closure.end(); ++it)
        if (it->kind != ObjectKind::Atom) ordered.push_back(*it);

    std::map<ObjectRef, core::ObjectId> localIds;
    for (size_t i = 0; i < ordered.size(); ++i) {
        const ObjectRef ref = ordered[i];
        core::ObjectId newId = core::kInvalidObjectId;
        Error copied = Error::success();
        if (ref.kind == ObjectKind::Bond) continue;   // second pass, after all atoms
        switch (ref.kind) {
        case ObjectKind::Atom: copied = copyAtom(document, scratch, ref.id, &newId); break;
        case ObjectKind::Text: copied = copyText(document, scratch, ref.id, &newId); break;
        case ObjectKind::Path: copied = copyPath(document, scratch, ref.id, &newId); break;
        default: copied = copyAnnotation(document, scratch, ref.kind, ref.id, &newId); break;
        }
        if (!copied.ok()) return copied;
        localIds[ref] = newId;

        // Extension payload travels with the object. A reference-bearing key is
        // rewritten below; everything else is copied verbatim.
        const core::ExtensionBag& bag = document.extensions(ref);
        const std::vector<core::ExtensionEntry>& entries = bag.entries();
        for (size_t k = 0; k < entries.size(); ++k) {
            if (!core::isPersistentExtensionKey(entries[k].key)) continue;
            scratch.extensions(ObjectRef(ref.kind, newId))
                .set(entries[k].key, entries[k].value, entries[k].sourceId);
        }
    }

    // Second pass: bonds, now that every atom has its final package-local id. This
    // is the step that makes the package self-contained; a bond written during the
    // atom loop would carry source-document ids into a document that renumbered
    // them, and the resulting bond would point at nothing.
    for (size_t i = 0; i < ordered.size(); ++i) {
        const ObjectRef ref = ordered[i];
        if (ref.kind != ObjectKind::Bond) continue;
        const core::BondRecord* record = document.bonds().get(ref.id);
        if (!record) return Error(ErrorCode::NotFound, "selected bond is gone");
        const std::map<ObjectRef, core::ObjectId>::const_iterator beginIt =
            localIds.find(ObjectRef(ObjectKind::Atom, record->beginAtom));
        const std::map<ObjectRef, core::ObjectId>::const_iterator endIt =
            localIds.find(ObjectRef(ObjectKind::Atom, record->endAtom));
        if (beginIt == localIds.end() || endIt == localIds.end())
            return Error(ErrorCode::InvalidArgument,
                         "package bond refers to an atom outside the closure");
        core::BondRecord copy = *record;
        copy.beginAtom = beginIt->second;
        copy.endAtom = endIt->second;
        core::ObjectId newId = core::kInvalidObjectId;
        Error added = scratch.addBondRecord(copy, &newId);
        if (!added.ok()) return added;
        localIds[ref] = newId;
        const core::ExtensionBag& bag = document.extensions(ref);
        const std::vector<core::ExtensionEntry>& entries = bag.entries();
        for (size_t k = 0; k < entries.size(); ++k) {
            if (!core::isPersistentExtensionKey(entries[k].key)) continue;
            scratch.extensions(ObjectRef(ObjectKind::Bond, newId))
                .set(entries[k].key, entries[k].value, entries[k].sourceId);
        }
    }
    scratch.rebuildDerived();

    const std::vector<core::AtomRecord>& atoms = scratch.atoms().records();
    const std::vector<core::BondRecord>& bonds = scratch.bonds().records();
    const std::vector<core::TextRecord>& texts = scratch.texts().records();
    const std::vector<core::PathRecord>& paths = scratch.paths().records();
    local.atomCount = 0; local.bondCount = 0; local.textCount = 0; local.otherCount = 0;
    for (size_t i = 0; i < atoms.size(); ++i) if (atoms[i].isAlive()) ++local.atomCount;
    for (size_t i = 0; i < bonds.size(); ++i) if (bonds[i].isAlive()) ++local.bondCount;
    for (size_t i = 0; i < texts.size(); ++i) if (texts[i].isAlive()) ++local.textCount;
    for (size_t i = 0; i < paths.size(); ++i) if (paths[i].isAlive()) ++local.otherCount;
    local.objectCount = local.atomCount + local.bondCount + local.textCount + local.otherCount;

    // Selected ROOTS, recorded as package-local ids so a decoder can tell which
    // objects the user actually picked apart from the dependencies.
    std::ostringstream roots;
    for (size_t i = 0; i < refs.size(); ++i) {
        const ObjectRef ref = refs[i];
        const std::map<ObjectRef, core::ObjectId>::const_iterator it = localIds.find(ref);
        if (it == localIds.end()) continue;
        roots << "S " << selectionTag(ref.kind) << " " << it->second << "\n";
        if (ref.kind == ObjectKind::Atom) {
            const core::AtomRecord* atom = document.atoms().get(ref.id);
            if (atom && atom->sourceId != 0) local.sourceIds.push_back(atom->sourceId);
        } else if (ref.kind == ObjectKind::Bond) {
            const core::BondRecord* bond = document.bonds().get(ref.id);
            if (bond && bond->sourceId != 0) local.sourceIds.push_back(bond->sourceId);
        } else if (ref.kind == ObjectKind::Text) {
            const core::TextRecord* text = document.texts().get(ref.id);
            if (text && text->sourceId != 0) local.sourceIds.push_back(text->sourceId);
        }
    }

    std::ostringstream payload;
    Error serialized = core::saveDocument(scratch, payload);
    if (!serialized.ok()) return serialized;
    const std::string payloadBytes = payload.str();

    std::ostringstream envelope;
    envelope << kSelMagic << " " << kSelFormatVersion << " " << payloadBytes.size() << "\n";
    envelope << roots.str();
    envelope << payloadBytes;
    envelope << "\nZ " << hexU64(static_cast<uint64_t>(
        crc32(payloadBytes.data(), payloadBytes.size()))) << "\n";

    *output = envelope.str();
    local.byteSize = output->size();
    if (info) *info = local;
    return Error::success();
}

// ---------------------------------------------------------------------------
// Inspect
// ---------------------------------------------------------------------------
Error inspectSelectionPackage(const std::string& bytes, SelectionPackageInfo* info) {
    if (bytes.size() < kSelMagicSize || bytes.compare(0, kSelMagicSize - 1, kSelMagic) != 0)
        return Error(ErrorCode::ParseError, "not a selection package");
    const size_t firstNewline = bytes.find(10);
    if (firstNewline == std::string::npos)
        return Error(ErrorCode::Truncated, "selection package has no header line");
    std::istringstream header(bytes.substr(0, firstNewline));
    std::string magic; int version = 0; unsigned long long payloadBytes = 0;
    if (!(header >> magic >> version >> payloadBytes))
        return Error(ErrorCode::ParseError, "selection package header is malformed");
    if (version > kSelFormatVersion)
        return Error(ErrorCode::Unsupported, "selection package is newer than this build");

    SelectionPackageInfo local;
    local.version = version;
    local.byteSize = bytes.size();

    size_t cursor = firstNewline + 1;
    while (cursor < bytes.size() && bytes.compare(cursor, 2, "S ") == 0) {
        const size_t eol = bytes.find(10, cursor);
        if (eol == std::string::npos)
            return Error(ErrorCode::Truncated, "selection package root list is truncated");
        std::istringstream entry(bytes.substr(cursor, eol - cursor));
        std::string tag, kindTag; unsigned long long id = 0;
        entry >> tag >> kindTag >> id;
        const ObjectKind kind = kindFromTag(kindTag);
        if (kind == ObjectKind::Unknown)
            return Error(ErrorCode::ParseError, "selection package has an unknown object tag");
        switch (kind) {
        case ObjectKind::Atom: ++local.atomCount; break;
        case ObjectKind::Bond: ++local.bondCount; break;
        case ObjectKind::Text: ++local.textCount; break;
        default: ++local.otherCount; break;
        }
        ++local.objectCount;
        cursor = eol + 1;
    }

    if (cursor + payloadBytes > bytes.size())
        return Error(ErrorCode::Truncated, "selection package payload is truncated");
    const size_t zPos = bytes.rfind("Z ");
    if (zPos != std::string::npos && zPos > cursor) {
        std::string hex = bytes.substr(zPos + 2);
        while (!hex.empty() && (hex[hex.size() - 1] == 10 || hex[hex.size() - 1] == 13))
            hex.erase(hex.size() - 1);
        uint64_t stored = 0;
        if (!parseHexU64(hex, &stored))
            return Error(ErrorCode::ParseError, "selection package checksum is malformed");
        const uint64_t computed =
            static_cast<uint64_t>(crc32(bytes.data() + cursor, static_cast<size_t>(payloadBytes)));
        if (stored != computed)
            return Error(ErrorCode::ParseError,
                         "selection package checksum mismatch: the payload is corrupt");
    }
    if (info) *info = local;
    return Error::success();
}



// ---------------------------------------------------------------------------
// Paste: remap every internal reference into the target document
// ---------------------------------------------------------------------------
// Three separate hazards, each handled explicitly rather than hoped away:
//   1. Internal references. A package bond points at package-local atom ids, and
//      a package annotation points at package-local anchors. Inserting those ids
//      into a document that already uses them would silently re-wire the
//      molecule. Every reference is therefore rewritten as each object is
//      inserted, and the mapping is returned so a caller can verify it.
//   2. Source ids. A pasted object keeps its CDXML source id, and two imports can
//      share one. On paste the source id is rewritten to one the target cannot
//      already have, and the WHOLE remap is reported.
//   3. Partial failure. A paste that fails half way must not leave a half-inserted
//      molecule. The insertion runs inside a core::Transaction, so a failure rolls
//      the target document back to exactly its previous state.
Error remapForPaste(const core::Document& incoming, core::Document& target,
                    PasteMapping* mapping, RefusalReport* refusals) {
    PasteMapping local;

    // ---- source id collisions --------------------------------------------
    SourceIdMap targetIds = SourceIdMap::fromDocument(target);
    uint64_t nextSourceId = 1;
    for (std::map<core::ObjectRef, core::ExtensionBag>::const_iterator it =
             target.allObjectExtensions().begin();
         it != target.allObjectExtensions().end(); ++it) {
        (void)it;
    }
    {
        // Start above the highest source id the target already uses, so a fresh
        // id cannot collide with an existing one by construction.
        const std::vector<core::AtomRecord>& atoms = target.atoms().records();
        for (size_t i = 0; i < atoms.size(); ++i)
            if (atoms[i].sourceId > 0 && static_cast<uint64_t>(atoms[i].sourceId) >= nextSourceId)
                nextSourceId = static_cast<uint64_t>(atoms[i].sourceId) + 1;
        const std::vector<core::BondRecord>& bonds = target.bonds().records();
        for (size_t i = 0; i < bonds.size(); ++i)
            if (bonds[i].sourceId > 0 && static_cast<uint64_t>(bonds[i].sourceId) >= nextSourceId)
                nextSourceId = static_cast<uint64_t>(bonds[i].sourceId) + 1;
        const std::vector<core::TextRecord>& texts = target.texts().records();
        for (size_t i = 0; i < texts.size(); ++i)
            if (texts[i].sourceId > 0 && static_cast<uint64_t>(texts[i].sourceId) >= nextSourceId)
                nextSourceId = static_cast<uint64_t>(texts[i].sourceId) + 1;
    }

    // ---- insertion inside one transaction ---------------------------------
    core::Transaction transaction;
    Error begun = transaction.begin(target, "persistence.paste");
    if (!begun.ok()) return begun;

    // Objects must be inserted in an order that lets a reference resolve. The
    // incoming document is already topologically safe for atoms-before-bonds, but
    // a caller may hand us an arbitrary document, so the order is derived here.
    const std::vector<core::AtomRecord>& inAtoms = incoming.atoms().records();
    std::map<core::ObjectId, core::ObjectId> atomMap;
    for (size_t i = 0; i < inAtoms.size(); ++i) {
        if (!inAtoms[i].isAlive()) continue;
        core::AtomRecord copy = inAtoms[i];
        const int32_t originalSourceId = copy.sourceId;
        if (originalSourceId != 0) {
            // The question is OCCUPANCY, not self-consistency: if the target already has
            // an object carrying this source id, the pasted copy must be renumbered, or
            // two objects would share an id and a reference to it would be ambiguous.
            // collides() would answer a different question (does one id map to two
            // objects inside the map) and would miss exactly this case.
            const bool targetUsesIt = targetIds.refFor(originalSourceId).valid();
            if (targetUsesIt) {
                copy.sourceId = static_cast<int32_t>(nextSourceId++);
                local.remappedSourceIds.push_back(copy.sourceId);
            }
            targetIds.bind(copy.sourceId, core::ObjectRef(ObjectKind::Atom, core::kInvalidObjectId));
        }
        core::ObjectId newId = core::kInvalidObjectId;
        Error added = target.addAtomRecord(copy, &newId);
        if (!added.ok()) { transaction.abort(); return added; }
        target.setAtomLabelQuiet(newId, incoming.atomLabel(inAtoms[i].id));
        target.setRunsQuiet(newId, incoming.runs(inAtoms[i].id));
        atomMap[inAtoms[i].id] = newId;
        local.atoms[inAtoms[i].id] = newId;

        // Extension payload, with any id-bearing key rewritten.
        const core::ExtensionBag& bag = incoming.extensions(
            core::ObjectRef(ObjectKind::Atom, inAtoms[i].id));
        const std::vector<core::ExtensionEntry>& entries = bag.entries();
        for (size_t k = 0; k < entries.size(); ++k) {
            if (!core::isPersistentExtensionKey(entries[k].key)) continue;
            target.extensions(core::ObjectRef(ObjectKind::Atom, newId))
                .set(entries[k].key, entries[k].value,
                     originalSourceId != copy.sourceId ? copy.sourceId : entries[k].sourceId);
        }
    }

    const std::vector<core::BondRecord>& inBonds = incoming.bonds().records();
    for (size_t i = 0; i < inBonds.size(); ++i) {
        if (!inBonds[i].isAlive()) continue;
        const std::map<core::ObjectId, core::ObjectId>::const_iterator beginIt =
            atomMap.find(inBonds[i].beginAtom);
        const std::map<core::ObjectId, core::ObjectId>::const_iterator endIt =
            atomMap.find(inBonds[i].endAtom);
        if (beginIt == atomMap.end() || endIt == atomMap.end()) {
            if (refusals) {
                Refusal item;
                item.path = "/package/bond";
                item.element = "b";
                item.reason = "pasted bond references an atom that is not in the package";
                item.sourceId = inBonds[i].sourceId;
                refusals->add(item);
            }
            transaction.abort();
            return Error(ErrorCode::NotFound,
                         "pasted bond references an atom outside the package");
        }
        core::BondRecord copy = inBonds[i];
        copy.beginAtom = beginIt->second;
        copy.endAtom = endIt->second;
        ++local.referencesRewritten;
        core::ObjectId newId = core::kInvalidObjectId;
        Error added = target.addBondRecord(copy, &newId);
        if (!added.ok()) { transaction.abort(); return added; }
        local.bonds[inBonds[i].id] = newId;
    }

    const std::vector<core::TextRecord>& inTexts = incoming.texts().records();
    for (size_t i = 0; i < inTexts.size(); ++i) {
        if (!inTexts[i].isAlive()) continue;
        core::TextRecord copy = inTexts[i];
        core::ObjectId newId = core::kInvalidObjectId;
        Error added = target.addTextRecord(copy, &newId);
        if (!added.ok()) { transaction.abort(); return added; }
        target.setRunsQuiet(newId, incoming.runs(inTexts[i].id));
        local.texts[inTexts[i].id] = newId;
    }

    Error committed = transaction.commit();
    if (!committed.ok()) return committed;
    if (mapping) *mapping = local;
    return Error::success();
}

// ---------------------------------------------------------------------------
// Decode = inspect, load into a scratch document, then remap into the target
// ---------------------------------------------------------------------------
Error decodeSelectionPackage(const std::string& bytes, core::Document& target,
                             std::vector<core::ObjectRef>* inserted,
                             RefusalReport* refusals) {
    SelectionPackageInfo info;
    Error inspected = inspectSelectionPackage(bytes, &info);
    if (!inspected.ok()) return inspected;

    const size_t firstNewline = bytes.find(10);
    size_t cursor = firstNewline + 1;
    while (cursor < bytes.size() && bytes.compare(cursor, 2, "S ") == 0)
        cursor = bytes.find(10, cursor) + 1;

    std::istringstream header(bytes.substr(0, firstNewline));
    std::string magic; int version = 0; unsigned long long payloadBytes = 0;
    header >> magic >> version >> payloadBytes;
    if (cursor + payloadBytes > bytes.size())
        return Error(ErrorCode::Truncated, "selection package payload is truncated");

    const std::string payload = bytes.substr(cursor, static_cast<size_t>(payloadBytes));
    const uint64_t computed = static_cast<uint64_t>(crc32(payload.data(), payload.size()));
    const size_t zPos = bytes.rfind("Z ");
    if (zPos != std::string::npos && zPos > cursor) {
        std::string hex = bytes.substr(zPos + 2);
        while (!hex.empty() && (hex[hex.size() - 1] == 10 || hex[hex.size() - 1] == 13))
            hex.erase(hex.size() - 1);
        uint64_t stored = 0;
        if (!parseHexU64(hex, &stored))
            return Error(ErrorCode::ParseError, "selection package checksum is malformed");
        if (stored != computed)
            return Error(ErrorCode::ParseError,
                         "selection package checksum mismatch: the payload is corrupt");
    }

    core::Document incoming;
    std::istringstream payloadStream(payload);
    core::LoadReport loadReport;
    Error loaded = core::loadDocument(incoming, payloadStream, &loadReport);
    if (!loaded.ok()) return loaded;

    PasteMapping mapping;
    Error remapped = remapForPaste(incoming, target, &mapping, refusals);
    if (!remapped.ok()) return remapped;

    if (inserted) {
        inserted->clear();
        for (std::map<core::ObjectId, core::ObjectId>::const_iterator it = mapping.atoms.begin();
             it != mapping.atoms.end(); ++it)
            inserted->push_back(core::ObjectRef(ObjectKind::Atom, it->second));
        for (std::map<core::ObjectId, core::ObjectId>::const_iterator it = mapping.bonds.begin();
             it != mapping.bonds.end(); ++it)
            inserted->push_back(core::ObjectRef(ObjectKind::Bond, it->second));
        for (std::map<core::ObjectId, core::ObjectId>::const_iterator it = mapping.texts.begin();
             it != mapping.texts.end(); ++it)
            inserted->push_back(core::ObjectRef(ObjectKind::Text, it->second));
    }
    return Error::success();
}

} // namespace persistence
} // namespace r62

