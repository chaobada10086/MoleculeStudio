#include "document.hpp"
#include "commands.hpp"   // DocumentSession definition for the patch recorder

#include <algorithm>
#include <cstring>
#include <sstream>

namespace r62 {
namespace core {

// ---------------------------------------------------------------------------
// Error names
// ---------------------------------------------------------------------------
const char* errorName(ErrorCode code) {
    switch (code) {
    case ErrorCode::Ok: return "Ok";
    case ErrorCode::NotImplemented: return "NotImplemented";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::OutOfRange: return "OutOfRange";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::NotAlive: return "NotAlive";
    case ErrorCode::WrongKind: return "WrongKind";
    case ErrorCode::InvalidGeometry: return "InvalidGeometry";
    case ErrorCode::InvalidSemantics: return "InvalidSemantics";
    case ErrorCode::InvalidText: return "InvalidText";
    case ErrorCode::DuplicateBond: return "DuplicateBond";
    case ErrorCode::SelfBond: return "SelfBond";
    case ErrorCode::DocumentMismatch: return "DocumentMismatch";
    case ErrorCode::RevisionConflict: return "RevisionConflict";
    case ErrorCode::Locked: return "Locked";
    case ErrorCode::RolledBack: return "RolledBack";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::IoError: return "IoError";
    case ErrorCode::ParseError: return "ParseError";
    case ErrorCode::Truncated: return "Truncated";
    case ErrorCode::LimitExceeded: return "LimitExceeded";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// UTF-8 / XML character validity
// ---------------------------------------------------------------------------
Error validateUtf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i++]);
        unsigned cp = lead, count = 0, minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { cp = lead & 31u; count = 1; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { cp = lead & 15u; count = 2; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { cp = lead & 7u; count = 3; minimum = 0x10000; }
        else if (lead >= 0x80) return Error(ErrorCode::InvalidText, "Invalid UTF-8 lead byte");
        if (i + count > text.size())
            return Error(ErrorCode::InvalidText, "Truncated UTF-8 sequence");
        for (unsigned j = 0; j < count; ++j) {
            const unsigned char c = static_cast<unsigned char>(text[i++]);
            if ((c & 0xc0u) != 0x80u)
                return Error(ErrorCode::InvalidText, "Invalid UTF-8 continuation byte");
            cp = (cp << 6) | (c & 63u);
        }
        if (cp < minimum || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu))
            return Error(ErrorCode::InvalidText, "Invalid UTF-8 code point");
        if (!(cp == 9 || cp == 10 || cp == 13 || (cp >= 0x20 && cp <= 0xd7ff) ||
              (cp >= 0xe000 && cp <= 0xfffd) || (cp >= 0x10000 && cp <= 0x10ffffu)))
            return Error(ErrorCode::InvalidText, "Text contains a character XML 1.0 forbids");
    }
    return Error::success();
}

Error validateText(const std::string& utf8, bool allowEmpty) {
    if (utf8.empty() && !allowEmpty)
        return Error(ErrorCode::InvalidText, "Text must not be empty");
    return validateUtf8(utf8);
}

// ---------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------
namespace {
const char* const kElements[] = {
    "?","H","He","Li","Be","B","C","N","O","F","Ne","Na","Mg","Al","Si","P","S","Cl","Ar","K",
    "Ca","Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn","Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y",
    "Zr","Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn","Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr",
    "Nd","Pm","Sm","Eu","Gd","Tb","Dy","Ho","Er","Tm","Yb","Lu","Hf","Ta","W","Re","Os","Ir","Pt","Au",
    "Hg","Tl","Pb","Bi","Po","At","Rn","Fr","Ra","Ac","Th","Pa","U","Np","Pu","Am","Cm","Bk","Cf","Es",
    "Fm","Md","No","Lr","Rf","Db","Sg","Bh","Hs","Mt","Ds","Rg","Cn","Nh","Fl","Mc","Lv","Ts","Og"
};
const size_t kElementCount = sizeof(kElements) / sizeof(kElements[0]);
} // namespace

const char* elementSymbol(int element) {
    if (element < 0 || static_cast<size_t>(element) >= kElementCount) return 0;
    return kElements[element];
}

int elementNumber(const std::string& symbol) {
    for (size_t i = 1; i < kElementCount; ++i)
        if (symbol == kElements[i]) return static_cast<int>(i);
    return 0;
}

// ---------------------------------------------------------------------------
// ObjectClass / ObjectKind
// ---------------------------------------------------------------------------
ObjectClass classOf(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Atom: return ObjectClass::Node;
    case ObjectKind::Bond: return ObjectClass::Bond;
    case ObjectKind::Text: return ObjectClass::RichText;
    case ObjectKind::Path: return ObjectClass::Path;
    case ObjectKind::Arrow: return ObjectClass::Arrow;
    case ObjectKind::Bracket: return ObjectClass::Bracket;
    case ObjectKind::Table: return ObjectClass::Table;
    case ObjectKind::Orbital:
    case ObjectKind::Symbol: return ObjectClass::Science;
    case ObjectKind::BioShape: return ObjectClass::Bio;
    case ObjectKind::Group: return ObjectClass::Group;
    default: return ObjectClass::Unknown;
    }
}

const char* kindName(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Atom: return "Atom";
    case ObjectKind::Bond: return "Bond";
    case ObjectKind::Text: return "Text";
    case ObjectKind::Path: return "Path";
    case ObjectKind::Arrow: return "Arrow";
    case ObjectKind::Bracket: return "Bracket";
    case ObjectKind::Table: return "Table";
    case ObjectKind::Orbital: return "Orbital";
    case ObjectKind::Symbol: return "Symbol";
    case ObjectKind::BioShape: return "BioShape";
    case ObjectKind::Group: return "Group";
    default: return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Document construction
// ---------------------------------------------------------------------------
DocumentId Document::nextDocumentId() {
    static DocumentId counter = 1;
    return counter++;
}

Document::Document()
    : documentId_(nextDocumentId()), revision_(1), structureSerial_(1),
      defaultBondLength_(14.4), defaultLineWidth_(0.6), defaultTextSize_(10.0),
      defaultFont_("Arial"), name_(), sourcePath_(), session_(0) {}

Document::Document(DocumentId id)
    : documentId_(id == kInvalidDocumentId ? nextDocumentId() : id),
      revision_(1), structureSerial_(1),
      defaultBondLength_(14.4), defaultLineWidth_(0.6), defaultTextSize_(10.0),
      defaultFont_("Arial"), name_(), sourcePath_(), session_(0) {}

// ---------------------------------------------------------------------------
// Session notices
// ---------------------------------------------------------------------------
void Document::noteTouchedAtom(ObjectId id) { if (session_) session_->noteAtom(id); }
void Document::noteTouchedBond(ObjectId id) { if (session_) session_->noteBond(id); }
void Document::noteTouchedText(ObjectId id) { if (session_) session_->noteText(id); }
void Document::noteTouchedPath(ObjectId id) { if (session_) session_->notePath(id); }
void Document::noteTouchedAnnotation(ObjectKind kind, ObjectId id) {
    if (session_) session_->noteAnnotation(kind, id);
}
void Document::noteTouchedTable(ObjectId id) { if (session_) session_->noteTable(id); }
void Document::noteTouchedGroup(ObjectId id) { if (session_) session_->noteGroup(id); }
void Document::noteTouchedDefaults() { if (session_) session_->noteDefaults(); }
void Document::noteTouchedExtensions(const ObjectRef& ref, bool documentLevel) {
    if (session_) session_->noteExtensions(ref, documentLevel);
}
void Document::noteTouchedStructure() { if (session_) session_->noteStructure(); }

// ---------------------------------------------------------------------------
// Slabs by enum
// ---------------------------------------------------------------------------
const void* Document::slabData(Slab slab) const {
    switch (slab) {
    case Slab::Atom: return &atoms_;
    case Slab::Bond: return &bonds_;
    case Slab::Text: return &texts_;
    case Slab::Path: return &paths_;
    case Slab::Arrow: return &annotations_[0];
    case Slab::Bracket: return &annotations_[1];
    case Slab::Orbital: return &annotations_[2];
    case Slab::Symbol: return &annotations_[3];
    case Slab::BioShape: return &annotations_[4];
    case Slab::Table: return &tables_;
    case Slab::Group: return &groups_;
    }
    return 0;
}

size_t Document::slabSize(Slab slab) const {
    switch (slab) {
    case Slab::Atom: return atoms_.size();
    case Slab::Bond: return bonds_.size();
    case Slab::Text: return texts_.size();
    case Slab::Path: return paths_.size();
    case Slab::Arrow: return annotations_[0].size();
    case Slab::Bracket: return annotations_[1].size();
    case Slab::Orbital: return annotations_[2].size();
    case Slab::Symbol: return annotations_[3].size();
    case Slab::BioShape: return annotations_[4].size();
    case Slab::Table: return tables_.size();
    case Slab::Group: return groups_.size();
    }
    return 0;
}

ObjectSlab<AnnotationRecord>& Document::annotationSlab(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Arrow: return annotations_[0];
    case ObjectKind::Bracket: return annotations_[1];
    case ObjectKind::Orbital: return annotations_[2];
    case ObjectKind::Symbol: return annotations_[3];
    case ObjectKind::BioShape: return annotations_[4];
    default: return annotations_[0];
    }
}

// ---------------------------------------------------------------------------
// Extensions
// ---------------------------------------------------------------------------
namespace {
const ExtensionBag kEmptyBag;
}

const ExtensionBag& Document::extensions(const ObjectRef& ref) const {
    std::map<ObjectRef, ExtensionBag>::const_iterator it = objectExtensions_.find(ref);
    return it == objectExtensions_.end() ? kEmptyBag : it->second;
}

ExtensionBag& Document::extensions(const ObjectRef& ref) {
    return objectExtensions_[ref];
}

// ---------------------------------------------------------------------------
// Adjacency
// ---------------------------------------------------------------------------
std::vector<ObjectId> Document::neighbours(ObjectId atom) const {
    std::vector<ObjectId> result;
    if (atom >= adjacency_.size()) return result;
    const std::vector<ObjectId>& incident = adjacency_[atom];
    for (size_t i = 0; i < incident.size(); ++i) {
        const BondRecord* b = bonds_.alive(incident[i]);
        if (!b) continue;
        result.push_back(b->beginAtom == atom ? b->endAtom : b->beginAtom);
    }
    return result;
}

ObjectId Document::bondBetween(ObjectId a, ObjectId b) const {
    std::map< std::pair<ObjectId, ObjectId>, ObjectId >::const_iterator it =
        bondIndex_.find(bondKey(a, b));
    return it == bondIndex_.end() ? kInvalidObjectId : it->second;
}

uint64_t Document::aliveObjectCount() const {
    uint64_t n = 0;
    for (size_t i = 0; i < atoms_.size(); ++i) if (atoms_.records()[i].isAlive()) ++n;
    for (size_t i = 0; i < bonds_.size(); ++i) if (bonds_.records()[i].isAlive()) ++n;
    for (size_t i = 0; i < texts_.size(); ++i) if (texts_.records()[i].isAlive()) ++n;
    for (size_t i = 0; i < paths_.size(); ++i) if (paths_.records()[i].isAlive()) ++n;
    for (int s = 0; s < 5; ++s)
        for (size_t i = 0; i < annotations_[s].size(); ++i)
            if (annotations_[s].records()[i].isAlive()) ++n;
    for (size_t i = 0; i < tables_.size(); ++i) if (tables_.records()[i].isAlive()) ++n;
    for (size_t i = 0; i < groups_.size(); ++i) if (groups_.records()[i].isAlive()) ++n;
    return n;
}

uint64_t Document::totalSlotCount() const {
    return atoms_.size() + bonds_.size() + texts_.size() + paths_.size() +
           annotations_[0].size() + annotations_[1].size() + annotations_[2].size() +
           annotations_[3].size() + annotations_[4].size() +
           tables_.size() + groups_.size();
}

// ---------------------------------------------------------------------------
// Mutation primitives
// ---------------------------------------------------------------------------
Error Document::addAtom(const Vec2& position, int element, ObjectId* out) {
    if (!position.finite()) return Error(ErrorCode::InvalidGeometry, "Atom position must be finite");
    if (!validElement(element)) return Error(ErrorCode::InvalidArgument, "Element must be 1..118");
    AtomRecord record;
    record.position = position;
    record.element = static_cast<int16_t>(element);
    return addAtomRecord(record, out);
}

Error Document::addAtomRecord(const AtomRecord& record, ObjectId* out) {
    if (!record.position.finite())
        return Error(ErrorCode::InvalidGeometry, "Atom position must be finite");
    if (!validElement(record.element))
        return Error(ErrorCode::InvalidArgument, "Element must be 1..118");
    if (!validCharge(record.semantics.charge))
        return Error(ErrorCode::InvalidSemantics, "Charge must be -15..15");
    if (static_cast<int>(record.semantics.radical) >
        static_cast<int>(Radical::Triplet))
        return Error(ErrorCode::InvalidSemantics, "Unknown radical value");
    const ObjectId id = atoms_.push(record);
    if (adjacency_.size() < atoms_.size()) adjacency_.resize(atoms_.size());
    noteTouchedAtom(id);
    noteTouchedStructure();
    bumpStructure();
    if (out) *out = id;
    return Error::success();
}

Error Document::addBond(ObjectId begin, ObjectId end, ObjectId* out) {
    BondRecord record;
    record.beginAtom = begin;
    record.endAtom = end;
    record.order = BondOrder::Single;
    return addBondRecord(record, out);
}

Error Document::validateBondReference(const BondRecord& bond) const {
    if (bond.beginAtom == bond.endAtom)
        return Error(ErrorCode::SelfBond, "A bond must connect two different atoms");
    if (!atoms_.alive(bond.beginAtom))
        return Error(ErrorCode::NotAlive, "Bond begin atom does not exist or is deleted");
    if (!atoms_.alive(bond.endAtom))
        return Error(ErrorCode::NotAlive, "Bond end atom does not exist or is deleted");
    return Error::success();
}

Error Document::addBondRecord(const BondRecord& record, ObjectId* out) {
    Error valid = validateBondReference(record);
    if (!valid.ok()) return valid;
    if (!validBondOrderValue(record.order))
        return Error(ErrorCode::InvalidSemantics, "Bond order value out of range");
    // Aromatic/query semantics must be expressible: refuse combinations that
    // contradict each other rather than silently storing them.
    if (record.order == BondOrder::Aromatic &&
        record.semantics != BondSemantics::Aromatic &&
        record.semantics != BondSemantics::Query)
        return Error(ErrorCode::InvalidSemantics,
                     "Aromatic order requires aromatic or query semantics; "
                     "do not encode aromaticity in an integer order");
    const std::pair<ObjectId, ObjectId> key = bondKey(record.beginAtom, record.endAtom);
    std::map< std::pair<ObjectId, ObjectId>, ObjectId >::const_iterator existing =
        bondIndex_.find(key);
    if (existing != bondIndex_.end()) {
        const BondRecord* prev = bonds_.alive(existing->second);
        if (prev)
            return Error(ErrorCode::DuplicateBond, "A bond between these atoms already exists");
    }
    const ObjectId id = bonds_.push(record);
    bondIndex_[key] = id;
    if (adjacency_.size() < atoms_.size()) adjacency_.resize(atoms_.size());
    adjacency_[record.beginAtom].push_back(id);
    adjacency_[record.endAtom].push_back(id);
    noteTouchedBond(id);
    noteTouchedStructure();
    bumpStructure();
    if (out) *out = id;
    return Error::success();
}

Error Document::addText(const Vec2& position, double size, const std::string& utf8, ObjectId* out) {
    TextRecord record;
    record.position = position;
    record.size = size;
    record.role = TextRole::Caption;
    Error valid = validateText(utf8, true);
    if (!valid.ok()) return valid;
    Error result = addTextRecord(record, out);
    if (!result.ok()) return result;
    std::vector<TextRun> runs(1);
    runs[0].utf8 = utf8;
    runs_[*out] = runs;
    noteTouchedText(*out);
    return Error::success();
}

Error Document::addTextRecord(const TextRecord& record, ObjectId* out) {
    if (!record.position.finite())
        return Error(ErrorCode::InvalidGeometry, "Text position must be finite");
    if (!std::isfinite(record.size) || record.size < 0.0)
        return Error(ErrorCode::InvalidArgument, "Text size must be finite and non-negative");
    const ObjectId id = texts_.push(record);
    if (runs_.find(id) == runs_.end()) {
        std::vector<TextRun> runs(1);
        runs_[id] = runs;
    }
    noteTouchedText(id);
    noteTouchedStructure();
    bumpStructure();
    if (out) *out = id;
    return Error::success();
}

Error Document::addPath(const PathRecord& record, ObjectId* out) {
    if (record.points.size() < 2)
        return Error(ErrorCode::InvalidGeometry, "A path needs at least two points");
    for (size_t i = 0; i < record.points.size(); ++i)
        if (!record.points[i].finite())
            return Error(ErrorCode::InvalidGeometry, "Path point must be finite");
    if (!record.pointKind.empty() && record.pointKind.size() != record.points.size())
        return Error(ErrorCode::InvalidArgument, "pointKind must match points or be empty");
    const ObjectId id = paths_.push(record);
    noteTouchedPath(id);
    noteTouchedStructure();
    bumpStructure();
    if (out) *out = id;
    return Error::success();
}

Error Document::addAnnotation(ObjectKind kind, const AnnotationRecord& record, ObjectId* out) {
    if (kind != ObjectKind::Arrow && kind != ObjectKind::Bracket &&
        kind != ObjectKind::Orbital && kind != ObjectKind::Symbol &&
        kind != ObjectKind::BioShape)
        return Error(ErrorCode::WrongKind, "Kind has no annotation slab");
    for (int i = 0; i < 4; ++i)
        if (!record.geometry[i].finite())
            return Error(ErrorCode::InvalidGeometry, "Annotation geometry must be finite");
    AnnotationRecord copy = record;
    copy.kind = kind;
    const ObjectId id = annotationSlab(kind).push(copy);
    noteTouchedAnnotation(kind, id);
    noteTouchedStructure();
    bumpStructure();
    if (out) *out = id;
    return Error::success();
}

Error Document::addTable(const TableRecord& record, ObjectId* out) {
    if (record.columns == 0 || record.rows == 0)
        return Error(ErrorCode::InvalidArgument, "A table needs at least one row and column");
    if (record.cells.size() != static_cast<size_t>(record.rows) * record.columns)
        return Error(ErrorCode::InvalidArgument, "cells must be rows*columns entries");
    const ObjectId id = tables_.push(record);
    noteTouchedTable(id);
    noteTouchedStructure();
    bumpStructure();
    if (out) *out = id;
    return Error::success();
}

Error Document::addGroup(const GroupRecord& record, ObjectId* out) {
    const ObjectId id = groups_.push(record);
    noteTouchedGroup(id);
    noteTouchedStructure();
    bumpStructure();
    if (out) *out = id;
    return Error::success();
}

Error Document::removeBond(ObjectId id) {
    BondRecord* bond = bonds_.mutableGet(id);
    if (!bond) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
    if (!bond->isAlive()) return Error(ErrorCode::NotAlive, "Bond is already deleted");
    bondIndex_.erase(bondKey(bond->beginAtom, bond->endAtom));
    bonds_.kill(id);
    noteTouchedBond(id);
    noteTouchedStructure();
    bumpStructure();
    return Error::success();
}

Error Document::removeAtom(ObjectId id) {
    if (!atoms_.alive(id)) return Error(ErrorCode::NotAlive, "Atom does not exist or is deleted");
    // Cascade: every incident alive bond is removed in the SAME transaction.
    std::vector<ObjectId> incident = adjacency_[id];
    for (size_t i = 0; i < incident.size(); ++i) {
        BondRecord* bond = bonds_.mutableGet(incident[i]);
        if (bond && bond->isAlive()) {
            bondIndex_.erase(bondKey(bond->beginAtom, bond->endAtom));
            bonds_.kill(incident[i]);
            noteTouchedBond(incident[i]);
        }
    }
    atoms_.kill(id);
    atomLabels_.erase(id);
    runs_.erase(id);
    std::map<ObjectRef, ExtensionBag>::iterator it =
        objectExtensions_.find(ObjectRef(ObjectKind::Atom, id));
    if (it != objectExtensions_.end()) {
        objectExtensions_.erase(it);
        noteTouchedExtensions(ObjectRef(ObjectKind::Atom, id), false);
    }
    noteTouchedAtom(id);
    noteTouchedStructure();
    bumpStructure();
    return Error::success();
}

Error Document::removeObject(const ObjectRef& ref) {
    switch (ref.kind) {
    case ObjectKind::Atom:
        return removeAtom(ref.id);
    case ObjectKind::Bond:
        return removeBond(ref.id);
    case ObjectKind::Text: {
        TextRecord* record = texts_.mutableGet(ref.id);
        if (!record) return Error(ErrorCode::OutOfRange, "Text id is out of range");
        if (!record->isAlive()) return Error(ErrorCode::NotAlive, "Text is already deleted");
        texts_.kill(ref.id);
        noteTouchedText(ref.id);
        noteTouchedStructure();
        bumpStructure();
        return Error::success();
    }
    case ObjectKind::Path:
    case ObjectKind::Arrow:
    case ObjectKind::Bracket:
    case ObjectKind::Table:
    case ObjectKind::Orbital:
    case ObjectKind::Symbol:
    case ObjectKind::BioShape:
    case ObjectKind::Group:
        return Error(ErrorCode::NotImplemented,
                     "Deleting this object class is not implemented in T01");
    default:
        return Error(ErrorCode::WrongKind, "Unknown object kind");
    }
}

// ---------------------------------------------------------------------------
// Field mutators
// ---------------------------------------------------------------------------
Error Document::setAtomPosition(ObjectId id, const Vec2& p) {
    AtomRecord* atom = atoms_.mutableGet(id);
    if (!atom) return Error(ErrorCode::OutOfRange, "Atom id is out of range");
    if (!atom->isAlive()) return Error(ErrorCode::NotAlive, "Atom is deleted");
    if (!p.finite()) return Error(ErrorCode::InvalidGeometry, "Position must be finite");
    if (atom->position.x == p.x && atom->position.y == p.y) return Error::success();
    atom->position = p;
    noteTouchedAtom(id);
    bumpStructure();
    return Error::success();
}

Error Document::setElement(ObjectId id, int element) {
    AtomRecord* atom = atoms_.mutableGet(id);
    if (!atom) return Error(ErrorCode::OutOfRange, "Atom id is out of range");
    if (!atom->isAlive()) return Error(ErrorCode::NotAlive, "Atom is deleted");
    if (!validElement(element)) return Error(ErrorCode::InvalidArgument, "Element must be 1..118");
    if (atom->element == element) return Error::success();
    atom->element = static_cast<int16_t>(element);
    noteTouchedAtom(id);
    bumpStructure();
    return Error::success();
}

Error Document::setAtomSemantics(ObjectId id, const AtomSemantics& s) {
    AtomRecord* atom = atoms_.mutableGet(id);
    if (!atom) return Error(ErrorCode::OutOfRange, "Atom id is out of range");
    if (!atom->isAlive()) return Error(ErrorCode::NotAlive, "Atom is deleted");
    if (!validCharge(s.charge)) return Error(ErrorCode::InvalidSemantics, "Charge must be -15..15");
    if (s.isotope < 0) return Error(ErrorCode::InvalidSemantics, "Isotope must be >= 0");
    if (s.implicitHydrogens < -1 || s.implicitHydrogens > 8)
        return Error(ErrorCode::InvalidSemantics, "implicitHydrogens must be -1 or 0..8");
    if (atom->semantics == s) return Error::success();
    atom->semantics = s;
    noteTouchedAtom(id);
    bumpStructure();
    return Error::success();
}

Error Document::setAtomLabel(ObjectId id, const std::string& utf8) {
    const AtomRecord* atom = atoms_.alive(id);
    if (!atom) return Error(ErrorCode::NotAlive, "Atom does not exist");
    Error valid = validateText(utf8, true);
    if (!valid.ok()) return valid;
    atomLabels_[id] = utf8;
    noteTouchedAtom(id);
    bumpStructure();
    return Error::success();
}

Error Document::setBondEndpoints(ObjectId id, ObjectId begin, ObjectId end) {
    BondRecord* bond = bonds_.mutableGet(id);
    if (!bond) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
    if (!bond->isAlive()) return Error(ErrorCode::NotAlive, "Bond is deleted");
    if (bond->beginAtom == begin && bond->endAtom == end) return Error::success();
    BondRecord candidate = *bond;
    candidate.beginAtom = begin;
    candidate.endAtom = end;
    Error valid = validateBondReference(candidate);
    if (!valid.ok()) return valid;
    const std::pair<ObjectId, ObjectId> key = bondKey(begin, end);
    std::map< std::pair<ObjectId, ObjectId>, ObjectId >::const_iterator existing =
        bondIndex_.find(key);
    if (existing != bondIndex_.end() && existing->second != id) {
        const BondRecord* prev = bonds_.alive(existing->second);
        if (prev) return Error(ErrorCode::DuplicateBond, "A bond between these atoms already exists");
    }
    bondIndex_.erase(bondKey(bond->beginAtom, bond->endAtom));
    bond->beginAtom = begin;
    bond->endAtom = end;
    bondIndex_[key] = id;
    noteTouchedBond(id);
    bumpStructure();
    return Error::success();
}

Error Document::setBondOrder(ObjectId id, BondOrder order) {
    BondRecord* bond = bonds_.mutableGet(id);
    if (!bond) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
    if (!bond->isAlive()) return Error(ErrorCode::NotAlive, "Bond is deleted");
    if (!validBondOrderValue(order))
        return Error(ErrorCode::InvalidSemantics, "Bond order value out of range");
    if (order == BondOrder::Aromatic &&
        bond->semantics != BondSemantics::Aromatic &&
        bond->semantics != BondSemantics::Query)
        return Error(ErrorCode::InvalidSemantics,
                     "Aromatic order requires aromatic or query semantics");
    if (bond->order == order) return Error::success();
    bond->order = order;
    noteTouchedBond(id);
    bumpStructure();
    return Error::success();
}

Error Document::setBondDisplay(ObjectId id, BondDisplay display) {
    BondRecord* bond = bonds_.mutableGet(id);
    if (!bond) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
    if (!bond->isAlive()) return Error(ErrorCode::NotAlive, "Bond is deleted");
    if (bond->display == display) return Error::success();
    bond->display = display;
    noteTouchedBond(id);
    bumpStructure();
    return Error::success();
}

Error Document::setBondSemantics(ObjectId id, BondSemantics semantics) {
    BondRecord* bond = bonds_.mutableGet(id);
    if (!bond) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
    if (!bond->isAlive()) return Error(ErrorCode::NotAlive, "Bond is deleted");
    if (bond->semantics == semantics) return Error::success();
    bond->semantics = semantics;
    noteTouchedBond(id);
    bumpStructure();
    return Error::success();
}

Error Document::setBondStereo(ObjectId id, BondStereo stereo) {
    BondRecord* bond = bonds_.mutableGet(id);
    if (!bond) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
    if (!bond->isAlive()) return Error(ErrorCode::NotAlive, "Bond is deleted");
    if (bond->stereo == stereo) return Error::success();
    bond->stereo = stereo;
    noteTouchedBond(id);
    bumpStructure();
    return Error::success();
}

Error Document::setTextPosition(ObjectId id, const Vec2& p) {
    TextRecord* text = texts_.mutableGet(id);
    if (!text) return Error(ErrorCode::OutOfRange, "Text id is out of range");
    if (!text->isAlive()) return Error(ErrorCode::NotAlive, "Text is deleted");
    if (!p.finite()) return Error(ErrorCode::InvalidGeometry, "Position must be finite");
    if (text->position.x == p.x && text->position.y == p.y) return Error::success();
    text->position = p;
    noteTouchedText(id);
    bumpStructure();
    return Error::success();
}

Error Document::setTextPlain(ObjectId id, const std::string& utf8) {
    const TextRecord* text = texts_.alive(id);
    if (!text) return Error(ErrorCode::NotAlive, "Text does not exist");
    Error valid = validateText(utf8, true);
    if (!valid.ok()) return valid;
    std::vector<TextRun> runs = runs_.count(id) ? runs_[id] : std::vector<TextRun>(1);
    if (runs.empty()) runs.resize(1);
    if (runs[0].utf8 == utf8 && runs.size() == 1) return Error::success();
    runs[0].utf8 = utf8;
    if (runs.size() > 1) runs.resize(1);
    runs_[id] = runs;
    noteTouchedText(id);
    bumpStructure();
    return Error::success();
}

Error Document::setTextSize(ObjectId id, double size) {
    TextRecord* text = texts_.mutableGet(id);
    if (!text) return Error(ErrorCode::OutOfRange, "Text id is out of range");
    if (!text->isAlive()) return Error(ErrorCode::NotAlive, "Text is deleted");
    if (!std::isfinite(size) || size < 0.0)
        return Error(ErrorCode::InvalidArgument, "Text size must be finite and non-negative");
    if (text->size == size) return Error::success();
    text->size = size;
    noteTouchedText(id);
    bumpStructure();
    return Error::success();
}

Error Document::setTextFormat(ObjectId id, const TextFormat& format) {
    TextRecord* text = texts_.mutableGet(id);
    if (!text) return Error(ErrorCode::OutOfRange, "Text id is out of range");
    if (!text->isAlive()) return Error(ErrorCode::NotAlive, "Text is deleted");
    if (text->format == format) return Error::success();
    text->format = format;
    noteTouchedText(id);
    bumpStructure();
    return Error::success();
}

Error Document::setTextRuns(ObjectId id, const std::vector<TextRun>& runs) {
    const TextRecord* text = texts_.alive(id);
    if (!text) return Error(ErrorCode::NotAlive, "Text does not exist");
    if (runs.empty()) return Error(ErrorCode::InvalidText, "Text needs at least one run");
    for (size_t i = 0; i < runs.size(); ++i) {
        Error valid = validateText(runs[i].utf8, true);
        if (!valid.ok()) return valid;
    }
    runs_[id] = runs;
    noteTouchedText(id);
    bumpStructure();
    return Error::success();
}

Error Document::setRuns(ObjectId id, const std::vector<TextRun>& runs) {
    return setTextRuns(id, runs);
}

const std::string& Document::atomLabel(ObjectId id) const {
    static const std::string empty;
    std::map< ObjectId, std::string >::const_iterator it = atomLabels_.find(id);
    return it == atomLabels_.end() ? empty : it->second;
}

void Document::setAtomLabelQuiet(ObjectId id, const std::string& utf8) {
    if (utf8.empty()) atomLabels_.erase(id);
    else atomLabels_[id] = utf8;
}

const std::vector<TextRun>& Document::runs(ObjectId id) const {
    static const std::vector<TextRun> empty;
    std::map< ObjectId, std::vector<TextRun> >::const_iterator it = runs_.find(id);
    return it == runs_.end() ? empty : it->second;
}

std::string Document::plainText(ObjectId id) const {
    const std::vector<TextRun>& r = runs(id);
    return r.empty() ? std::string() : r[0].utf8;
}

Error Document::setLineStyle(ObjectRef ref, const LineStyle& style) {
    switch (ref.kind) {
    case ObjectKind::Bond: {
        BondRecord* bond = bonds_.mutableGet(ref.id);
        if (!bond) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
        if (!bond->isAlive()) return Error(ErrorCode::NotAlive, "Bond is deleted");
        // Bond line style is derived from display; store the width/colour in the
        // extension bag so the public record stays compact.
        ExtensionBag& bag = objectExtensions_[ref];
        std::ostringstream out;
        out << style.width << ',' << style.colorRGBA << ',' << static_cast<int>(style.dash);
        bag.set("core.style.line", out.str());
        noteTouchedExtensions(ref, false);
        bumpStructure();
        return Error::success();
    }
    case ObjectKind::Path: {
        PathRecord* path = paths_.mutableGet(ref.id);
        if (!path) return Error(ErrorCode::OutOfRange, "Path id is out of range");
        if (!path->isAlive()) return Error(ErrorCode::NotAlive, "Path is deleted");
        path->style = style;
        noteTouchedPath(ref.id);
        bumpStructure();
        return Error::success();
    }
    case ObjectKind::Arrow:
    case ObjectKind::Bracket:
    case ObjectKind::BioShape: {
        ObjectSlab<AnnotationRecord>& slab = annotationSlab(ref.kind);
        AnnotationRecord* record = slab.mutableGet(ref.id);
        if (!record) return Error(ErrorCode::OutOfRange, "Annotation id is out of range");
        if (!record->isAlive()) return Error(ErrorCode::NotAlive, "Annotation is deleted");
        record->style = style;
        noteTouchedAnnotation(ref.kind, ref.id);
        bumpStructure();
        return Error::success();
    }
    default:
        return Error(ErrorCode::Unsupported, "This object class has no line style yet");
    }
}

Error Document::setAnnotationGeometry(ObjectId id, const Vec2* points, size_t count) {
    if (count > 4) return Error(ErrorCode::InvalidArgument, "At most four geometry points");
    for (size_t i = 0; i < count; ++i)
        if (!points[i].finite()) return Error(ErrorCode::InvalidGeometry, "Geometry must be finite");
    for (int s = 0; s < 5; ++s) {
        AnnotationRecord* record = annotations_[s].mutableGet(id);
        if (!record) continue;
        if (!record->isAlive()) return Error(ErrorCode::NotAlive, "Annotation is deleted");
        for (size_t i = 0; i < count; ++i) record->geometry[i] = points[i];
        noteTouchedAnnotation(static_cast<ObjectKind>(record->kind), id);
        bumpStructure();
        return Error::success();
    }
    return Error(ErrorCode::NotFound, "No annotation with that id");
}

// ---------------------------------------------------------------------------
// Whole-record access used by the change-set writer
// ---------------------------------------------------------------------------
Error Document::readAtom(ObjectId id, AtomRecord* out) const {
    const AtomRecord* record = atoms_.get(id);
    if (!record) return Error(ErrorCode::OutOfRange, "Atom id is out of range");
    *out = *record;
    return Error::success();
}

Error Document::readBond(ObjectId id, BondRecord* out) const {
    const BondRecord* record = bonds_.get(id);
    if (!record) return Error(ErrorCode::OutOfRange, "Bond id is out of range");
    *out = *record;
    return Error::success();
}

Error Document::readText(ObjectId id, TextRecord* out) const {
    const TextRecord* record = texts_.get(id);
    if (!record) return Error(ErrorCode::OutOfRange, "Text id is out of range");
    *out = *record;
    return Error::success();
}

namespace {
// Grow a slab so that a record with this id exists, and write it. Used only by
// change-set application, which restores states captured earlier.
template <typename T, typename Slab>
Error writeRecord(Slab& slab, ObjectId id, const T& record, const char* what) {
    if (id == kInvalidObjectId)
        return Error(ErrorCode::InvalidArgument, std::string(what) + " id is invalid");
    std::vector<T>& records = slab.mutableRecords();
    if (records.size() <= static_cast<size_t>(id)) records.resize(static_cast<size_t>(id) + 1);
    if (!records[static_cast<size_t>(id)].isAlive() && record.isAlive()) {
        // Reviving a dead slot: allowed only through change-set application,
        // where the id is proven to have existed in the captured state.
    }
    records[static_cast<size_t>(id)] = record;
    records[static_cast<size_t>(id)].id = id;
    slab.rebuildBookkeeping();
    return Error::success();
}
} // namespace

Error Document::writeAtom(const AtomRecord& record) {
    Error r = writeRecord(atoms_, record.id, record, "Atom");
    if (!r.ok()) return r;
    if (adjacency_.size() < atoms_.size()) adjacency_.resize(atoms_.size());
    return Error::success();
}

Error Document::writeBond(const BondRecord& record) {
    return writeRecord(bonds_, record.id, record, "Bond");
}

Error Document::writeText(const TextRecord& record) {
    Error r = writeRecord(texts_, record.id, record, "Text");
    if (!r.ok()) return r;
    if (runs_.find(record.id) == runs_.end()) {
        std::vector<TextRun> runs(1);
        runs_[record.id] = runs;
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// Derived data
// ---------------------------------------------------------------------------
void Document::rebuildDerived() {
    adjacency_.assign(atoms_.size(), std::vector<ObjectId>());
    bondIndex_.clear();
    for (size_t i = 0; i < bonds_.size(); ++i) {
        BondRecord& bond = bonds_.mutableRecords()[i];
        if (!bond.isAlive()) continue;
        if (bond.beginAtom >= atoms_.size() || bond.endAtom >= atoms_.size()) {
            bond.alive = 0;
            continue;
        }
        bondIndex_[bondKey(bond.beginAtom, bond.endAtom)] = bond.id;
        adjacency_[bond.beginAtom].push_back(bond.id);
        adjacency_[bond.endAtom].push_back(bond.id);
    }
    atoms_.rebuildBookkeeping();
    bonds_.rebuildBookkeeping();
    texts_.rebuildBookkeeping();
    paths_.rebuildBookkeeping();
    for (int s = 0; s < 5; ++s) annotations_[s].rebuildBookkeeping();
    tables_.rebuildBookkeeping();
    groups_.rebuildBookkeeping();
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
Error Document::validate() const {
    for (size_t i = 0; i < atoms_.size(); ++i) {
        const AtomRecord& atom = atoms_.records()[i];
        if (atom.id != static_cast<ObjectId>(i))
            return Error(ErrorCode::InvalidArgument, "Atom id must equal its slab index");
        if (!atom.position.finite())
            return Error(ErrorCode::InvalidGeometry, "Atom position must be finite");
        if (!validElement(atom.element))
            return Error(ErrorCode::InvalidArgument, "Element must be 1..118");
        if (!validCharge(atom.semantics.charge))
            return Error(ErrorCode::InvalidSemantics, "Charge must be -15..15");
    }
    for (size_t i = 0; i < bonds_.size(); ++i) {
        const BondRecord& bond = bonds_.records()[i];
        if (bond.id != static_cast<ObjectId>(i))
            return Error(ErrorCode::InvalidArgument, "Bond id must equal its slab index");
        if (!bond.isAlive()) continue;
        Error valid = validateBondReference(bond);
        if (!valid.ok()) return valid;
        if (!validBondOrderValue(bond.order))
            return Error(ErrorCode::InvalidSemantics, "Bond order value out of range");
    }
    for (size_t i = 0; i < texts_.size(); ++i) {
        const TextRecord& text = texts_.records()[i];
        if (text.id != static_cast<ObjectId>(i))
            return Error(ErrorCode::InvalidArgument, "Text id must equal its slab index");
        if (!text.position.finite())
            return Error(ErrorCode::InvalidGeometry, "Text position must be finite");
        const std::vector<TextRun>& r = runs(text.id);
        for (size_t k = 0; k < r.size(); ++k) {
            Error valid = validateUtf8(r[k].utf8);
            if (!valid.ok()) return valid;
        }
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// Raw state capture / restore  (transaction rollback)
// ---------------------------------------------------------------------------
void Document::captureAll(RawState* out) const {
    out->atoms = atoms_.records();
    out->bonds = bonds_.records();
    out->texts = texts_.records();
    out->paths = paths_.records();
    for (int s = 0; s < 5; ++s) out->annotations[s] = annotations_[s].records();
    out->tables = tables_.records();
    out->groups = groups_.records();
    out->extensions = objectExtensions_;
    out->documentExtensions = documentExtensions_;
    out->runs = runs_;
    out->atomLabels = atomLabels_;
    out->documentDefaults.bondLength = defaultBondLength_;
    out->documentDefaults.lineWidth = defaultLineWidth_;
    out->documentDefaults.textSize = defaultTextSize_;
    out->documentDefaults.font = defaultFont_;
}

Error Document::restoreRaw(const RawState& state) {
    atoms_.adoptRecords(state.atoms);
    bonds_.adoptRecords(state.bonds);
    texts_.adoptRecords(state.texts);
    paths_.adoptRecords(state.paths);
    for (int s = 0; s < 5; ++s) annotations_[s].adoptRecords(state.annotations[s]);
    tables_.adoptRecords(state.tables);
    groups_.adoptRecords(state.groups);
    objectExtensions_ = state.extensions;
    documentExtensions_ = state.documentExtensions;
    runs_ = state.runs;
    atomLabels_ = state.atomLabels;
    defaultBondLength_ = state.documentDefaults.bondLength;
    defaultLineWidth_ = state.documentDefaults.lineWidth;
    defaultTextSize_ = state.documentDefaults.textSize;
    defaultFont_ = state.documentDefaults.font;
    rebuildDerived();
    bumpStructure();
    return validate();
}

// ---------------------------------------------------------------------------
// Quiet mutators (change-set application and rollback)
// ---------------------------------------------------------------------------
void Document::setRunsQuiet(ObjectId id, const std::vector<TextRun>& runs) {
    if (runs.empty()) runs_.erase(id);
    else runs_[id] = runs;
}

void Document::setDefaultsQuiet(const Defaults& defaults) {
    defaultBondLength_ = defaults.bondLength;
    defaultLineWidth_ = defaults.lineWidth;
    defaultTextSize_ = defaults.textSize;
    defaultFont_ = defaults.font;
    bumpStructure();
}

// ---------------------------------------------------------------------------
// Digest
// ---------------------------------------------------------------------------
namespace {
inline uint64_t fnv1a(uint64_t hash, const void* data, size_t bytes) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= static_cast<uint64_t>(p[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}
inline uint64_t fnv1aString(uint64_t hash, const std::string& s) {
    hash = fnv1a(hash, s.data(), s.size());
    const unsigned char zero = 0;
    return fnv1a(hash, &zero, 1);
}
template <typename T>
inline uint64_t fnv1aRecords(uint64_t hash, const std::vector<T>& records) {
    if (records.empty()) return hash;
    return fnv1a(hash, &records[0], records.size() * sizeof(T));
}
} // namespace

uint64_t Document::digestOf(const RawState& state) {
    // Identity (DocumentId) is deliberately excluded from the digest, so a
    // scratch document built from a captured state compares equal.
    Document scratch;
    Error restored = scratch.restoreRaw(state);
    (void)restored;
    return scratch.digest();
}

// ---------------------------------------------------------------------------
// Digest
// ---------------------------------------------------------------------------
// The digest hashes LOGICAL FIELDS ONLY, never raw record bytes. Hashing raw
// bytes would fold struct padding into the result, and padding is not part of
// the document state: a memcmp/digest that depends on it would report a
// rollback mismatch (or, worse, a false match) for reasons unrelated to the
// edit. Every field that any other module can observe is listed explicitly, so
// adding a field to a record without adding it here is a visible omission.
namespace {

inline uint64_t hashBool(uint64_t hash, bool value) {
    const unsigned char v = value ? 1 : 0;
    return fnv1a(hash, &v, 1);
}
inline uint64_t hashU8(uint64_t hash, uint8_t value) { return fnv1a(hash, &value, 1); }
inline uint64_t hashI16(uint64_t hash, int16_t value) { return fnv1a(hash, &value, sizeof(value)); }
inline uint64_t hashI32(uint64_t hash, int32_t value) { return fnv1a(hash, &value, sizeof(value)); }
inline uint64_t hashU32(uint64_t hash, uint32_t value) { return fnv1a(hash, &value, sizeof(value)); }
inline uint64_t hashU16(uint64_t hash, uint16_t value) { return fnv1a(hash, &value, sizeof(value)); }
inline uint64_t hashString(uint64_t hash, const std::string& value) {
    hash = hashU32(hash, static_cast<uint32_t>(value.size()));
    return fnv1aString(hash, value);
}
inline uint64_t hashU64(uint64_t hash, uint64_t value) { return fnv1a(hash, &value, sizeof(value)); }
inline uint64_t hashDouble(uint64_t hash, double value) { return fnv1a(hash, &value, sizeof(value)); }

uint64_t hashAtom(uint64_t hash, const AtomRecord& a) {
    hash = hashU32(hash, a.id);
    hash = hashDouble(hash, a.position.x);
    hash = hashDouble(hash, a.position.y);
    hash = hashI16(hash, a.element);
    hash = hashBool(hash, a.isAlive());
    hash = hashI16(hash, static_cast<int16_t>(a.semantics.charge));
    hash = hashU8(hash, a.semantics.radical);
    hash = hashI16(hash, a.semantics.isotope);
    hash = hashI16(hash, static_cast<int16_t>(a.semantics.implicitHydrogens));
    hash = hashU8(hash, a.semantics.nodeType);
    hash = hashBool(hash, a.semantics.showLabel);
    hash = hashBool(hash, a.semantics.aromatic);
    return hashI32(hash, a.sourceId);
}

uint64_t hashBond(uint64_t hash, const BondRecord& b) {
    hash = hashU32(hash, b.id);
    hash = hashU32(hash, b.beginAtom);
    hash = hashU32(hash, b.endAtom);
    hash = hashU8(hash, static_cast<uint8_t>(b.order));
    hash = hashU8(hash, static_cast<uint8_t>(b.display));
    hash = hashU8(hash, static_cast<uint8_t>(b.semantics));
    hash = hashU8(hash, static_cast<uint8_t>(b.stereo));
    hash = hashBool(hash, b.isAlive());
    return hashI32(hash, b.sourceId);
}

uint64_t hashText(uint64_t hash, const TextRecord& t) {
    hash = hashU32(hash, t.id);
    hash = hashDouble(hash, t.position.x);
    hash = hashDouble(hash, t.position.y);
    hash = hashDouble(hash, t.size);
    hash = hashU8(hash, static_cast<uint8_t>(t.role));
    hash = hashBool(hash, t.isAlive());
    hash = hashString(hash, t.format.fontName);
    hash = hashDouble(hash, t.format.size);
    hash = hashU16(hash, t.format.flags);
    hash = hashU8(hash, t.format.align);
    hash = hashU32(hash, t.format.colorRGBA);
    return hashI32(hash, t.sourceId);
}

uint64_t hashLineStyle(uint64_t hash, const LineStyle& s) {
    hash = hashDouble(hash, s.width);
    hash = hashU32(hash, s.colorRGBA);
    hash = hashU8(hash, s.dash);
    return hashU8(hash, s.lineCap);
}

uint64_t hashPath(uint64_t hash, const PathRecord& p) {
    hash = hashU32(hash, p.id);
    hash = hashBool(hash, p.isAlive());
    hash = hashBool(hash, p.closed);
    hash = hashBool(hash, p.fillEnabled != 0);
    hash = hashU32(hash, p.fillRGBA);
    hash = hashI32(hash, p.sourceId);
    hash = hashLineStyle(hash, p.style);
    hash = hashU32(hash, static_cast<uint32_t>(p.points.size()));
    for (size_t i = 0; i < p.points.size(); ++i) {
        hash = hashDouble(hash, p.points[i].x);
        hash = hashDouble(hash, p.points[i].y);
    }
    hash = hashU32(hash, static_cast<uint32_t>(p.pointKind.size()));
    for (size_t i = 0; i < p.pointKind.size(); ++i) hash = hashU8(hash, p.pointKind[i]);
    return hash;
}

uint64_t hashAnnotation(uint64_t hash, const AnnotationRecord& a) {
    hash = hashU32(hash, a.id);
    hash = hashU8(hash, static_cast<uint8_t>(a.kind));
    hash = hashBool(hash, a.isAlive());
    hash = hashU32(hash, a.presetId);
    hash = hashDouble(hash, a.scale);
    hash = hashDouble(hash, a.rotation);
    hash = hashU32(hash, a.fillRGBA);
    hash = hashI32(hash, a.sourceId);
    hash = hashLineStyle(hash, a.style);
    for (int i = 0; i < 4; ++i) hash = hashU32(hash, a.anchors[i]);
    for (int i = 0; i < 4; ++i) {
        hash = hashDouble(hash, a.geometry[i].x);
        hash = hashDouble(hash, a.geometry[i].y);
    }
    return hash;
}

uint64_t hashTable(uint64_t hash, const TableRecord& t) {
    hash = hashU32(hash, t.id);
    hash = hashBool(hash, t.isAlive());
    hash = hashDouble(hash, t.frame.min.x);
    hash = hashDouble(hash, t.frame.min.y);
    hash = hashDouble(hash, t.frame.max.x);
    hash = hashDouble(hash, t.frame.max.y);
    hash = hashU32(hash, t.rows);
    hash = hashU32(hash, t.columns);
    hash = hashDouble(hash, t.rowHeight);
    hash = hashI32(hash, t.sourceId);
    hash = hashU32(hash, static_cast<uint32_t>(t.cells.size()));
    for (size_t i = 0; i < t.cells.size(); ++i) hash = hashU32(hash, t.cells[i]);
    return hash;
}

uint64_t hashGroup(uint64_t hash, const GroupRecord& g) {
    hash = hashU32(hash, g.id);
    hash = hashBool(hash, g.isAlive());
    hash = hashI32(hash, g.zIndex);
    hash = hashI32(hash, g.sourceId);
    hash = hashString(hash, g.label);
    hash = hashU32(hash, static_cast<uint32_t>(g.members.size()));
    for (size_t i = 0; i < g.members.size(); ++i) {
        hash = hashU8(hash, static_cast<uint8_t>(g.members[i].kind));
        hash = hashU32(hash, g.members[i].id);
    }
    return hash;
}

} // namespace

uint64_t Document::digest() const {
    uint64_t hash = 1469598103934665603ull;
    hash = fnv1aString(hash, "r62.core.document.v1");
    hash = hashDouble(hash, defaultBondLength_);
    hash = hashDouble(hash, defaultLineWidth_);
    hash = hashDouble(hash, defaultTextSize_);
    hash = fnv1aString(hash, defaultFont_);
    hash = fnv1aString(hash, name_);

    const std::vector<AtomRecord>& atomRecords = atoms_.records();
    hash = hashU32(hash, static_cast<uint32_t>(atomRecords.size()));
    for (size_t i = 0; i < atomRecords.size(); ++i) hash = hashAtom(hash, atomRecords[i]);
    const std::vector<BondRecord>& bondRecords = bonds_.records();
    hash = hashU32(hash, static_cast<uint32_t>(bondRecords.size()));
    for (size_t i = 0; i < bondRecords.size(); ++i) hash = hashBond(hash, bondRecords[i]);
    const std::vector<TextRecord>& textRecords = texts_.records();
    hash = hashU32(hash, static_cast<uint32_t>(textRecords.size()));
    for (size_t i = 0; i < textRecords.size(); ++i) hash = hashText(hash, textRecords[i]);
    const std::vector<PathRecord>& pathRecords = paths_.records();
    hash = hashU32(hash, static_cast<uint32_t>(pathRecords.size()));
    for (size_t i = 0; i < pathRecords.size(); ++i) hash = hashPath(hash, pathRecords[i]);
    for (int s = 0; s < 5; ++s) {
        const std::vector<AnnotationRecord>& records = annotations_[s].records();
        hash = hashU32(hash, static_cast<uint32_t>(records.size()));
        for (size_t i = 0; i < records.size(); ++i) hash = hashAnnotation(hash, records[i]);
    }
    const std::vector<TableRecord>& tableRecords = tables_.records();
    hash = hashU32(hash, static_cast<uint32_t>(tableRecords.size()));
    for (size_t i = 0; i < tableRecords.size(); ++i) hash = hashTable(hash, tableRecords[i]);
    const std::vector<GroupRecord>& groupRecords = groups_.records();
    hash = hashU32(hash, static_cast<uint32_t>(groupRecords.size()));
    for (size_t i = 0; i < groupRecords.size(); ++i) hash = hashGroup(hash, groupRecords[i]);

    for (std::map< ObjectId, std::vector<TextRun> >::const_iterator it = runs_.begin();
         it != runs_.end(); ++it) {
        hash = fnv1a(hash, &it->first, sizeof(it->first));
        for (size_t k = 0; k < it->second.size(); ++k) {
            hash = fnv1aString(hash, it->second[k].utf8);
            hash = fnv1a(hash, &it->second[k].flags, sizeof(it->second[k].flags));
            hash = fnv1aString(hash, it->second[k].fontName);
            hash = fnv1a(hash, &it->second[k].size, sizeof(it->second[k].size));
        }
    }
    for (std::map< ObjectId, std::string >::const_iterator it = atomLabels_.begin();
         it != atomLabels_.end(); ++it) {
        hash = fnv1a(hash, &it->first, sizeof(it->first));
        hash = fnv1aString(hash, it->second);
    }
    // Extensions are keyed by ObjectRef; hash the ordered keys explicitly.
    hash = fnv1aString(hash, documentExtensions_.size() ? "doc-ext" : "no-doc-ext");
    const std::vector<ExtensionEntry>& docEntries = documentExtensions_.entries();
    for (size_t i = 0; i < docEntries.size(); ++i) {
        hash = fnv1aString(hash, docEntries[i].key);
        hash = fnv1aString(hash, docEntries[i].value);
        hash = fnv1a(hash, &docEntries[i].sourceId, sizeof(docEntries[i].sourceId));
    }
    for (std::map<ObjectRef, ExtensionBag>::const_iterator it = objectExtensions_.begin();
         it != objectExtensions_.end(); ++it) {
        const ObjectRef& ref = it->first;
        hash = fnv1a(hash, &ref.kind, sizeof(ref.kind));
        hash = fnv1a(hash, &ref.id, sizeof(ref.id));
        const std::vector<ExtensionEntry>& entries = it->second.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            hash = fnv1aString(hash, entries[i].key);
            hash = fnv1aString(hash, entries[i].value);
            hash = fnv1a(hash, &entries[i].sourceId, sizeof(entries[i].sourceId));
        }
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
Document::Stats Document::stats() const {
    Stats s;
    s.atoms = atoms_.size();
    s.bonds = bonds_.size();
    s.texts = texts_.size();
    s.paths = paths_.size();
    for (int k = 0; k < 5; ++k) s.annotations += annotations_[k].size();
    s.tables = tables_.size();
    s.groups = groups_.size();
    for (size_t i = 0; i < atoms_.size(); ++i) if (atoms_.records()[i].isAlive()) ++s.aliveAtoms;
    for (size_t i = 0; i < bonds_.size(); ++i) if (bonds_.records()[i].isAlive()) ++s.aliveBonds;
    for (size_t i = 0; i < texts_.size(); ++i) if (texts_.records()[i].isAlive()) ++s.aliveTexts;
    s.deadSlots = totalSlotCount() - aliveObjectCount();
    for (std::map<ObjectRef, ExtensionBag>::const_iterator it = objectExtensions_.begin();
         it != objectExtensions_.end(); ++it)
        s.extensionEntries += it->second.size();
    s.extensionEntries += documentExtensions_.size();
    return s;
}

} // namespace core
} // namespace r62
