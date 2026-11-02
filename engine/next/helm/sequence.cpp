// =============================================================================
// re_round62_viewer/next/helm/sequence.cpp
//
// T15 - SequenceModel implementation.
//
// The invariants this file maintains are stated in sequence.hpp. Short version:
//   * the monomer list is chemistry ORDER plus stable ids, not connectivity;
//   * connectivity lives only in connections_;
//   * layout is derived and never influences either.
// =============================================================================

#include "next/helm/sequence.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>

namespace r62 {
namespace helm {

// ---------------------------------------------------------------------------
// Enum names
// ---------------------------------------------------------------------------
const char* sequenceDisplayModeName(SequenceDisplayMode m) {
    return m == SequenceDisplayMode::Cartoon ? "cartoon" : "structure";
}

bool sequenceDisplayModeFromName(const std::string& n, SequenceDisplayMode* out) {
    if (!out) return false;
    if (n == "structure") { *out = SequenceDisplayMode::Structure; return true; }
    if (n == "cartoon")   { *out = SequenceDisplayMode::Cartoon;   return true; }
    return false;
}

const char* cartoonShapeName(CartoonShape s) {
    switch (s) {
        case CartoonShape::Circle:    return "circle";
        case CartoonShape::Octagon:   return "octagon";
        case CartoonShape::Diamond:   return "diamond";
        case CartoonShape::Trapezoid: return "trapezoid";
        case CartoonShape::Square:    return "square";
        case CartoonShape::Rectangle: return "rectangle";
        default: return "none";
    }
}

// The shape table from guide PDF page 172, applied to a RESOLVED definition.
// A monomer whose chemistry is unknown keeps shape None so a renderer cannot
// draw a confident shape for a residue whose type we did not establish.
CartoonShape cartoonShapeFor(const MonomerDefinition& definition) {
    if (!definition.chemistryDefined) {
        // A BLOB is a known KIND of unknown: the guide does give it a rectangle.
        return definition.isBlob() ? CartoonShape::Rectangle : CartoonShape::None;
    }
    switch (definition.polymerType) {
        case PolymerType::Peptide:
            return CartoonShape::Circle;
        case PolymerType::Rna:
            if (definition.role == MonomerRole::Branch) return CartoonShape::Trapezoid;
            // Sugar vs phosphate is decided by the natural analog the library
            // records, not by the spelling of the symbol.
            if (definition.naturalAnalog == "p" || definition.naturalAnalog == "P")
                return CartoonShape::Diamond;
            return CartoonShape::Octagon;
        case PolymerType::Chem: return CartoonShape::Square;
        case PolymerType::Blob: return CartoonShape::Rectangle;
        default: return CartoonShape::None;
    }
}

// Reversing a chain swaps the two backbone ends. R1 <-> R2; anything else (a
// branch such as R3, or a side-chain point) stays where it is, because a branch
// is not an end of the chain.
std::string oppositeBackboneAttachment(const std::string& label) {
    if (label == "R1") return "R2";
    if (label == "R2") return "R1";
    return label;
}

const char* connectionKindName(ConnectionKind k) {
    switch (k) {
        case ConnectionKind::Hydrogen: return "hydrogen";
        case ConnectionKind::Generic:  return "generic";
        default: return "covalent";
    }
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
void computeLineColumn(const std::string& text, size_t offset, size_t* line, size_t* column) {
    if (offset > text.size()) offset = text.size();
    size_t l = 1, c = 1;
    for (size_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') { ++l; c = 1; } else { ++c; }
    }
    if (line) *line = l;
    if (column) *column = c;
}

std::string SequenceDiagnostic::summary() const {
    std::ostringstream out;
    out << core::errorName(code);
    if (line != 0) out << " at line " << line << ", column " << column;
    if (!polymerId.empty()) out << " in " << polymerId;
    if (!monomerSymbol.empty()) out << " monomer " << monomerSymbol;
    out << ": " << message;
    return out.str();
}

// ---------------------------------------------------------------------------
// SequencePolymer
// ---------------------------------------------------------------------------
size_t SequencePolymer::liveCount() const {
    size_t n = 0;
    for (size_t i = 0; i < monomers.size(); ++i) if (monomers[i].alive) ++n;
    return n;
}

int SequencePolymer::firstLiveIndex() const {
    for (size_t i = 0; i < monomers.size(); ++i) if (monomers[i].alive) return static_cast<int>(i);
    return -1;
}

int SequencePolymer::lastLiveIndex() const {
    for (size_t i = monomers.size(); i > 0; --i)
        if (monomers[i - 1].alive) return static_cast<int>(i - 1);
    return -1;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
SequenceModel::Stats::Stats()
    : polymers(0), monomers(0), liveMonomers(0), connections(0),
      covalentConnections(0), hydrogenConnections(0), deadSlots(0),
      unknownMonomers(0), blobMonomers(0) {}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
SequenceModel::SequenceModel() : serial_(0), attachmentSerial_(0) {
    library_ = builtinNaturalLibrary();
}

void SequenceModel::clear() {
    polymers_.clear();
    connections_.clear();
    layout_.clear();
    idPool_.clear();
    ++serial_;
    ++attachmentSerial_;
}

void SequenceModel::setLayout(const SequenceLayout& layout) {
    layout_ = layout;
    // Deliberately does NOT bump attachmentSerial_: layout is a view.
    ++serial_;
}

// Slot allocation mirrors core::ObjectSlab: consume a trailing dead run before
// growing, never renumber, never reuse a middle slot. Keeping the two models
// identical here is what lets the adapter assert id equality.
MonomerInstanceId SequenceModel::allocateInstanceId() {
    const size_t n = idPool_.size();
    if (n > 0 && !idPool_[n - 1].alive) {
        idPool_[n - 1].alive = true;
        return static_cast<MonomerInstanceId>(n - 1);
    }
    idPool_.push_back(MonomerInstance());
    idPool_[idPool_.size() - 1].alive = true;
    return static_cast<MonomerInstanceId>(idPool_.size() - 1);
}

size_t SequenceModel::polymerIndexById(const std::string& polymerId) const {
    for (size_t i = 0; i < polymers_.size(); ++i)
        if (polymers_[i].polymerId == polymerId) return i;
    return static_cast<size_t>(-1);
}

core::Error SequenceModel::addPolymer(const std::string& polymerId, PolymerType type,
                                     size_t* outIndex) {
    if (polymerId.empty())
        return core::Error(core::ErrorCode::InvalidArgument, "empty polymer id");
    if (type == PolymerType::Unknown)
        return core::Error(core::ErrorCode::InvalidArgument, "unknown polymer type");
    if (polymerIndexById(polymerId) != static_cast<size_t>(-1))
        return core::Error(core::ErrorCode::AlreadyExists, "duplicate polymer id " + polymerId);
    PolymerType implied = PolymerType::Unknown;
    if (!polymerTypeFromId(polymerId, &implied))
        return core::Error(core::ErrorCode::InvalidArgument,
                           "polymer id must be a type name followed by digits: " + polymerId);
    if (implied != type)
        return core::Error(core::ErrorCode::InvalidArgument,
                           "polymer id prefix disagrees with the declared type: " + polymerId);
    SequencePolymer polymer;
    polymer.polymerId = polymerId;
    polymer.type = type;
    polymers_.push_back(polymer);
    if (outIndex) *outIndex = polymers_.size() - 1;
    ++serial_;
    return core::Error();
}

// ---------------------------------------------------------------------------
// Insertion
// ---------------------------------------------------------------------------
core::Error SequenceModel::insertMonomer(size_t polymerIndex, size_t position,
                                        const std::string& symbol,
                                        MonomerInstanceId* outId) {
    SequencePolymer* polymer = mutablePolymer(polymerIndex);
    if (!polymer)
        return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");
    if (symbol.empty())
        return core::Error(core::ErrorCode::InvalidArgument, "empty monomer symbol");
    if (position > polymer->monomers.size())
        return core::Error(core::ErrorCode::OutOfRange, "insert position out of range");

    MonomerInstance instance;
    instance.sourceText = symbol;
    const MonomerDefinition* found = library_.find(polymer->type, symbol);
    if (found) {
        instance.definition = *found;
    } else {
        // R1: an unresolved symbol keeps its text and gets NO invented structure.
        instance.definition = unknownMonomer(polymer->type, symbol);
    }
    int copies = 0;
    for (size_t i = 0; i < polymer->monomers.size(); ++i)
        if (polymer->monomers[i].alive &&
            polymer->monomers[i].definition.symbol == symbol) ++copies;
    instance.copyIndex = copies;
    return insertMonomerInstance(polymerIndex, position, instance, outId);
}

core::Error SequenceModel::insertMonomerInstance(size_t polymerIndex, size_t position,
                                                const MonomerInstance& instance,
                                                MonomerInstanceId* outId) {
    SequencePolymer* polymer = mutablePolymer(polymerIndex);
    if (!polymer)
        return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");
    if (position > polymer->monomers.size())
        return core::Error(core::ErrorCode::OutOfRange, "insert position out of range");
    MonomerInstance stored = instance;
    stored.id = allocateInstanceId();
    stored.alive = true;
    polymer->monomers.insert(
        polymer->monomers.begin() + static_cast<std::ptrdiff_t>(position), stored);

    // INSERTION DOES NOT CREATE A BOND. Every connection whose position is at or
    // after the insertion point must keep pointing at the SAME residue, so its
    // position index shifts. This is what makes 'insert a residue and the
    // cross-link still lands on the right monomer' true instead of hopeful.
    for (size_t c = 0; c < connections_.size(); ++c) {
        SequenceConnection& conn = connections_[c];
        if (!conn.alive) continue;
        if (conn.from.polymer == polymerIndex && conn.from.position >= position)
            ++conn.from.position;
        if (conn.to.polymer == polymerIndex && conn.to.position >= position)
            ++conn.to.position;
    }

    if (outId) *outId = stored.id;
    ++serial_;
    ++attachmentSerial_;
    return core::Error();
}

// ---------------------------------------------------------------------------
// Replacement
// ---------------------------------------------------------------------------
core::Error SequenceModel::replaceMonomer(size_t polymerIndex, size_t position,
                                         const std::string& symbol,
                                         size_t* outRequiredConnections) {
    SequencePolymer* polymer = mutablePolymer(polymerIndex);
    if (!polymer)
        return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");
    if (position >= polymer->monomers.size())
        return core::Error(core::ErrorCode::OutOfRange, "replace position out of range");
    MonomerInstance& current = polymer->monomers[position];
    if (!current.alive)
        return core::Error(core::ErrorCode::NotAlive, "monomer slot is not alive");

    const std::vector<std::string> usedLabels = usedAttachments(polymerIndex, position);
    const size_t used = usedLabels.size();
    if (outRequiredConnections) *outRequiredConnections = used;

    MonomerInstance replacement;
    replacement.sourceText = symbol;
    const MonomerDefinition* found = library_.find(polymer->type, symbol);
    if (found) replacement.definition = *found;
    else replacement.definition = unknownMonomer(polymer->type, symbol);

    const size_t available = replacement.definition.attachmentCount();
    if (!connectionsSatisfy(used, available)) {
        std::ostringstream msg;
        msg << "replacement monomer '" << symbol << "' offers " << available;
        msg << " attachment point(s) but this position uses " << used;
        return core::Error(core::ErrorCode::InvalidSemantics, msg.str());
    }
    // The new definition must actually HAVE the labels in use, not merely enough
    // of them. C offering R1,R2,R3 does not make it able to fill an R4 slot.
    for (size_t i = 0; i < usedLabels.size(); ++i) {
        if (!replacement.definition.hasAttachment(usedLabels[i])) {
            return core::Error(core::ErrorCode::InvalidSemantics,
                               "replacement monomer lacks attachment point " + usedLabels[i]);
        }
    }

    // The instance ID is PRESERVED, so no connection needs rewriting.
    const MonomerInstanceId keptId = current.id;
    const int keptCopy = current.copyIndex;
    const int keptVersion = current.dataVersion;
    current = replacement;
    current.id = keptId;
    current.alive = true;
    current.copyIndex = keptCopy;
    current.dataVersion = keptVersion;

    ++serial_;
    ++attachmentSerial_;
    return core::Error();
}

// ---------------------------------------------------------------------------
// Deletion
// ---------------------------------------------------------------------------
core::Error SequenceModel::deleteMonomer(size_t polymerIndex, size_t position,
                                        size_t* outKilledConnections) {
    SequencePolymer* polymer = mutablePolymer(polymerIndex);
    if (!polymer)
        return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");
    if (position >= polymer->monomers.size())
        return core::Error(core::ErrorCode::OutOfRange, "delete position out of range");
    MonomerInstance& victim = polymer->monomers[position];
    if (!victim.alive)
        return core::Error(core::ErrorCode::NotAlive, "monomer slot is not alive");

    // Connections that touched this residue are killed with it. They are NOT
    // silently rerouted to a neighbour: rerouting would create a bond between two
    // residues that were never bonded.
    size_t killed = 0;
    for (size_t c = 0; c < connections_.size(); ++c) {
        SequenceConnection& conn = connections_[c];
        if (!conn.alive) continue;
        const bool touchesFrom =
            conn.from.polymer == polymerIndex && conn.from.position == position;
        const bool touchesTo =
            conn.to.polymer == polymerIndex && conn.to.position == position;
        if (touchesFrom || touchesTo) { conn.alive = false; ++killed; }
    }

    // The slot stays. Its id is never reused or renumbered (ObjectSlab rule).
    victim.alive = false;
    if (outKilledConnections) *outKilledConnections = killed;
    ++serial_;
    ++attachmentSerial_;
    return core::Error();
}

// ---------------------------------------------------------------------------
// Direction
// ---------------------------------------------------------------------------
// Reversing is a CHEMISTRY-PRESERVING transformation of the reading direction.
// The residue order is reversed and every covalent connection that joins two
// LIVE positions of this polymer is remapped so the same pair of residues stays
// bonded by the same pair of attachment points. A left-to-right backbone
// (R2 on i, R1 on i+1) becomes (R1 on i, R2 on i+1) after reversal: same chain,
// read the other way.
core::Error SequenceModel::reversePolymer(size_t polymerIndex) {
    SequencePolymer* polymer = mutablePolymer(polymerIndex);
    if (!polymer)
        return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");

    const size_t n = polymer->monomers.size();
    for (size_t c = 0; c < connections_.size(); ++c) {
        SequenceConnection& conn = connections_[c];
        if (!conn.alive) continue;
        if (conn.from.polymer != polymerIndex && conn.to.polymer != polymerIndex) continue;
        const bool flip = (conn.kind == ConnectionKind::Covalent);
        if (conn.from.polymer == polymerIndex) {
            conn.from.position = n - 1 - conn.from.position;
            if (flip) conn.from.attachment = oppositeBackboneAttachment(conn.from.attachment);
        }
        if (conn.to.polymer == polymerIndex) {
            conn.to.position = n - 1 - conn.to.position;
            if (flip) conn.to.attachment = oppositeBackboneAttachment(conn.to.attachment);
        }
    }
    std::reverse(polymer->monomers.begin(), polymer->monomers.end());
    ++serial_;
    ++attachmentSerial_;
    return core::Error();
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------
core::Error SequenceModel::connect(size_t fromPolymer, size_t fromPosition,
                                  const std::string& fromAttachment,
                                  size_t toPolymer, size_t toPosition,
                                  const std::string& toAttachment,
                                  ConnectionKind kind, ConnectionId* outId) {
    const SequencePolymer* pf = polymer(fromPolymer);
    const SequencePolymer* pt = polymer(toPolymer);
    if (!pf) return core::Error(core::ErrorCode::OutOfRange, "source polymer out of range");
    if (!pt) return core::Error(core::ErrorCode::OutOfRange, "target polymer out of range");
    if (fromPosition >= pf->monomers.size())
        return core::Error(core::ErrorCode::OutOfRange, "source position out of range");
    if (toPosition >= pt->monomers.size())
        return core::Error(core::ErrorCode::OutOfRange, "target position out of range");
    const MonomerInstance& mf = pf->monomers[fromPosition];
    const MonomerInstance& mt = pt->monomers[toPosition];
    if (!mf.alive) return core::Error(core::ErrorCode::NotAlive, "source monomer is deleted");
    if (!mt.alive) return core::Error(core::ErrorCode::NotAlive, "target monomer is deleted");
    if (fromPolymer == toPolymer && fromPosition == toPosition &&
        fromAttachment == toAttachment && kind == ConnectionKind::Covalent)
        return core::Error(core::ErrorCode::SelfBond, "a monomer cannot bond to itself");

    if (kind != ConnectionKind::Generic) {
        if (!isValidAttachmentLabel(fromAttachment))
            return core::Error(core::ErrorCode::InvalidArgument,
                               "invalid source attachment " + fromAttachment);
        if (!isValidAttachmentLabel(toAttachment))
            return core::Error(core::ErrorCode::InvalidArgument,
                               "invalid target attachment " + toAttachment);
        // The monomer must actually HAVE the point. This stops a connection from
        // being written for an attachment that does not exist.
        if (kind == ConnectionKind::Covalent) {
            if (mf.definition.attachmentCount() > 0 &&
                !mf.definition.hasAttachment(fromAttachment) && !mf.definition.isBlob()) {
                return core::Error(core::ErrorCode::InvalidSemantics,
                                   "source monomer lacks attachment point " + fromAttachment);
            }
            if (mt.definition.attachmentCount() > 0 &&
                !mt.definition.hasAttachment(toAttachment) && !mt.definition.isBlob()) {
                return core::Error(core::ErrorCode::InvalidSemantics,
                                   "target monomer lacks attachment point " + toAttachment);
            }
            // An attachment point is normally used once. Refusing a second use is
            // what prevents a chemical impossibility from being stored silently.
            if (!isAttachmentFree(fromPolymer, fromPosition, fromAttachment))
                return core::Error(core::ErrorCode::InvalidSemantics,
                                   "source attachment already used: " + fromAttachment);
            if (!isAttachmentFree(toPolymer, toPosition, toAttachment))
                return core::Error(core::ErrorCode::InvalidSemantics,
                                   "target attachment already used: " + toAttachment);
        }
    }

    for (size_t i = 0; i < connections_.size(); ++i) {
        const SequenceConnection& e = connections_[i];
        if (!e.alive || e.kind != kind) continue;
        if (e.from.polymer == fromPolymer && e.from.position == fromPosition &&
            e.to.polymer == toPolymer && e.to.position == toPosition &&
            e.from.attachment == fromAttachment && e.to.attachment == toAttachment)
            return core::Error(core::ErrorCode::DuplicateBond, "connection already exists");
        if (kind == ConnectionKind::Covalent &&
            e.from.polymer == toPolymer && e.from.position == toPosition &&
            e.to.polymer == fromPolymer && e.to.position == fromPosition &&
            e.from.attachment == toAttachment && e.to.attachment == fromAttachment)
            return core::Error(core::ErrorCode::DuplicateBond,
                               "the reverse connection already exists");
    }

    SequenceConnection created;
    created.id = static_cast<ConnectionId>(connections_.size());
    created.alive = true;
    created.kind = kind;
    created.from = ConnectionEndpoint(fromPolymer, fromPosition, fromAttachment);
    created.to = ConnectionEndpoint(toPolymer, toPosition, toAttachment);
    connections_.push_back(created);
    if (outId) *outId = created.id;
    ++serial_;
    return core::Error();
}

core::Error SequenceModel::disconnect(ConnectionId id) {
    if (id >= connections_.size())
        return core::Error(core::ErrorCode::OutOfRange, "connection id out of range");
    if (!connections_[id].alive)
        return core::Error(core::ErrorCode::NotAlive, "connection already removed");
    connections_[id].alive = false;
    ++serial_;
    return core::Error();
}

core::Error SequenceModel::setConnectionAnnotation(ConnectionId id,
                                                  const std::string& annotation) {
    if (id >= connections_.size())
        return core::Error(core::ErrorCode::OutOfRange, "connection id out of range");
    if (!connections_[id].alive)
        return core::Error(core::ErrorCode::NotAlive, "connection already removed");
    connections_[id].annotation = annotation;
    ++serial_;
    return core::Error();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
std::vector<std::string> SequenceModel::usedAttachments(size_t polymerIndex,
                                                       size_t position) const {
    std::vector<std::string> out;
    const SequencePolymer* polymer = this->polymer(polymerIndex);
    if (!polymer || position >= polymer->monomers.size()) return out;
    const MonomerDefinition& definition = polymer->monomers[position].definition;
    // Iterate the DEFINITION order so the answer is deterministic, then report
    // which of those labels a live connection currently uses.
    for (size_t a = 0; a < definition.attachments.size(); ++a) {
        const std::string& label = definition.attachments[a].label;
        for (size_t c = 0; c < connections_.size(); ++c) {
            const SequenceConnection& conn = connections_[c];
            if (!conn.alive) continue;
            if (conn.kind == ConnectionKind::Generic) continue;
            const bool asFrom = conn.from.polymer == polymerIndex &&
                                conn.from.position == position &&
                                conn.from.attachment == label;
            const bool asTo = conn.to.polymer == polymerIndex &&
                              conn.to.position == position &&
                              conn.to.attachment == label;
            if (asFrom || asTo) { out.push_back(label); break; }
        }
    }
    return out;
}

bool SequenceModel::isAttachmentFree(size_t polymerIndex, size_t position,
                                    const std::string& attachment) const {
    const std::vector<std::string> used = usedAttachments(polymerIndex, position);
    for (size_t i = 0; i < used.size(); ++i) if (used[i] == attachment) return false;
    return true;
}

std::vector<std::string> SequenceModel::freeAttachments(size_t polymerIndex,
                                                       size_t position) const {
    std::vector<std::string> out;
    const SequencePolymer* polymer = this->polymer(polymerIndex);
    if (!polymer || position >= polymer->monomers.size()) return out;
    const std::vector<std::string> used = usedAttachments(polymerIndex, position);
    const MonomerDefinition& definition = polymer->monomers[position].definition;
    for (size_t a = 0; a < definition.attachments.size(); ++a) {
        bool taken = false;
        for (size_t u = 0; u < used.size(); ++u)
            if (used[u] == definition.attachments[a].label) { taken = true; break; }
        if (!taken) out.push_back(definition.attachments[a].label);
    }
    return out;
}

size_t SequenceModel::connectionCountAt(size_t polymerIndex, size_t position) const {
    size_t n = 0;
    for (size_t c = 0; c < connections_.size(); ++c) {
        const SequenceConnection& conn = connections_[c];
        if (!conn.alive) continue;
        if (conn.from.polymer == polymerIndex && conn.from.position == position) ++n;
        else if (conn.to.polymer == polymerIndex && conn.to.position == position) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
size_t SequenceModel::checkConnections(std::vector<SequenceDiagnostic>* problems) const {
    size_t count = 0;
    for (size_t c = 0; c < connections_.size(); ++c) {
        const SequenceConnection& conn = connections_[c];
        if (!conn.alive) continue;
        const SequencePolymer* pf = polymer(conn.from.polymer);
        const SequencePolymer* pt = polymer(conn.to.polymer);
        if (!pf || !pt) {
            ++count;
            if (problems) problems->push_back(SequenceDiagnostic(
                core::ErrorCode::InvalidSemantics, "connection references a missing polymer", 0));
            continue;
        }
        if (conn.from.position >= pf->monomers.size() ||
            conn.to.position >= pt->monomers.size()) {
            ++count;
            if (problems) problems->push_back(SequenceDiagnostic(
                core::ErrorCode::OutOfRange, "connection endpoint position out of range", 0));
            continue;
        }
        const MonomerInstance& mf = pf->monomers[conn.from.position];
        const MonomerInstance& mt = pt->monomers[conn.to.position];
        if (!mf.alive || !mt.alive) {
            ++count;
            if (problems) problems->push_back(SequenceDiagnostic(
                core::ErrorCode::NotAlive, "connection endpoint residue was deleted", 0));
            continue;
        }
        if (conn.kind == ConnectionKind::Generic) continue;
        const bool okFrom = mf.definition.attachmentCount() == 0 || mf.definition.isBlob() ||
                            mf.definition.hasAttachment(conn.from.attachment);
        const bool okTo = mt.definition.attachmentCount() == 0 || mt.definition.isBlob() ||
                          mt.definition.hasAttachment(conn.to.attachment);
        if (!okFrom || !okTo) {
            ++count;
            if (problems) problems->push_back(SequenceDiagnostic(
                core::ErrorCode::InvalidSemantics,
                "connection uses an attachment point the monomer does not define", 0));
        }
    }
    return count;
}

size_t SequenceModel::validate(std::vector<SequenceDiagnostic>* problems) const {
    size_t count = 0;
    std::map<std::string, size_t> seenIds;
    for (size_t i = 0; i < polymers_.size(); ++i) {
        const SequencePolymer& polymer = polymers_[i];
        if (polymer.polymerId.empty()) {
            ++count;
            if (problems) problems->push_back(SequenceDiagnostic(
                core::ErrorCode::InvalidArgument, "polymer has no id", 0));
        } else if (seenIds.count(polymer.polymerId)) {
            ++count;
            if (problems) problems->push_back(SequenceDiagnostic(
                core::ErrorCode::AlreadyExists,
                "duplicate polymer id " + polymer.polymerId, 0));
        } else {
            seenIds[polymer.polymerId] = i;
        }
        if (polymer.type == PolymerType::Unknown) {
            ++count;
            if (problems) problems->push_back(SequenceDiagnostic(
                core::ErrorCode::InvalidArgument, "polymer has an unknown type", 0));
        }
        for (size_t m = 0; m < polymer.monomers.size(); ++m) {
            const MonomerInstance& instance = polymer.monomers[m];
            if (!instance.alive) continue;
            if (instance.definition.symbol.empty()) {
                ++count;
                if (problems) problems->push_back(SequenceDiagnostic(
                    core::ErrorCode::InvalidArgument, "live monomer has no symbol", 0));
            }
            // A definition may not claim chemistry without stating a structure.
            if (instance.definition.chemistryDefined && instance.definition.smiles.empty()) {
                ++count;
                if (problems) problems->push_back(SequenceDiagnostic(
                    core::ErrorCode::InvalidSemantics,
                    "monomer claims a chemistry definition but states no structure", 0));
            }
            if (instance.definition.isBlob() && instance.definition.chemistryDefined) {
                ++count;
                if (problems) problems->push_back(SequenceDiagnostic(
                    core::ErrorCode::InvalidSemantics, "BLOB monomer claims chemistry", 0));
            }
        }
    }
    count += checkConnections(problems);
    // An attachment point used by two live covalent connections is a chemical
    // impossibility and is reported with the polymer it happened in.
    for (size_t i = 0; i < polymers_.size(); ++i) {
        const SequencePolymer& polymer = polymers_[i];
        for (size_t m = 0; m < polymer.monomers.size(); ++m) {
            if (!polymer.monomers[m].alive) continue;
            const std::vector<std::string> used = usedAttachments(i, m);
            std::map<std::string, int> tally;
            for (size_t u = 0; u < used.size(); ++u) tally[used[u]] += 1;
        }
        // Count actual usages per label rather than the de-duplicated list.
        std::map<std::string, int> byMonomerAttachment;
        for (size_t c = 0; c < connections_.size(); ++c) {
            const SequenceConnection& conn = connections_[c];
            if (!conn.alive || conn.kind != ConnectionKind::Covalent) continue;
            if (conn.from.polymer == i) {
                std::ostringstream key;
                key << conn.from.position << ':' << conn.from.attachment;
                byMonomerAttachment[key.str()] += 1;
            }
            if (conn.to.polymer == i) {
                std::ostringstream key;
                key << conn.to.position << ':' << conn.to.attachment;
                byMonomerAttachment[key.str()] += 1;
            }
        }
        for (std::map<std::string, int>::const_iterator it = byMonomerAttachment.begin();
             it != byMonomerAttachment.end(); ++it) {
            if (it->second > 1) {
                ++count;
                if (problems) problems->push_back(SequenceDiagnostic(
                    core::ErrorCode::InvalidSemantics,
                    "attachment point " + it->first + " is used more than once", 0));
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Statistics and digests
// ---------------------------------------------------------------------------
SequenceModel::Stats SequenceModel::stats() const {
    Stats s;
    s.polymers = polymers_.size();
    for (size_t i = 0; i < polymers_.size(); ++i) {
        const SequencePolymer& polymer = polymers_[i];
        s.monomers += polymer.monomers.size();
        for (size_t m = 0; m < polymer.monomers.size(); ++m) {
            const MonomerInstance& instance = polymer.monomers[m];
            if (instance.alive) {
                ++s.liveMonomers;
                if (!instance.definition.chemistryDefined && !instance.definition.isBlob())
                    ++s.unknownMonomers;
                if (instance.definition.isBlob()) ++s.blobMonomers;
            } else {
                ++s.deadSlots;
            }
        }
    }
    for (size_t c = 0; c < connections_.size(); ++c) {
        const SequenceConnection& conn = connections_[c];
        if (!conn.alive) continue;
        ++s.connections;
        if (conn.kind == ConnectionKind::Hydrogen) ++s.hydrogenConnections;
        else if (conn.kind == ConnectionKind::Covalent) ++s.covalentConnections;
    }
    return s;
}

namespace {

// A small FNV-1a mixer. Deterministic, dependency free, and stable across
// platforms, which is all a change-detection digest needs.
void mixByte(uint64_t* h, unsigned char byte) {
    *h ^= static_cast<uint64_t>(byte);
    *h *= 1099511628211ull;
}

void mixString(uint64_t* h, const std::string& value) {
    for (size_t i = 0; i < value.size(); ++i)
        mixByte(h, static_cast<unsigned char>(value[i]));
    mixByte(h, 0x1F);
}

void mixU64(uint64_t* h, uint64_t value) {
    for (int i = 0; i < 8; ++i) mixByte(h, static_cast<unsigned char>((value >> (i * 8)) & 0xFFu));
}

} // namespace

uint64_t SequenceModel::chemistryDigest() const {
    uint64_t h = 1469598103934665603ull;
    // Polymer identity and monomer definitions by STABLE ID.
    for (size_t i = 0; i < polymers_.size(); ++i) {
        const SequencePolymer& polymer = polymers_[i];
        mixString(&h, polymer.polymerId);
        mixString(&h, polymerTypeName(polymer.type));
        for (size_t m = 0; m < polymer.monomers.size(); ++m) {
            const MonomerInstance& instance = polymer.monomers[m];
            if (!instance.alive) continue;
            mixU64(&h, instance.id);
            // The SIGNED POSITION is mixed too, because chemistry order is part
            // of the chemistry. Moving a residue is a chemistry edit; moving a
            // residue on screen is not (that lives in the layout).
            mixU64(&h, m);
            mixString(&h, instance.definition.symbol);
            mixString(&h, instance.definition.smiles);
            // sourceText is mixed ONLY when it is the monomer's identity, which is the
            // case for an unknown or BLOB monomer: there the authoring text is all we
            // have. For a chemistry-defined monomer the symbol and the structure already
            // identify it, and mixing the authoring text would make the digest depend on
            // the NOTATION rather than on the chemistry. That distinction is what lets
            // R(A)P and its expanded spelling r.A.p digest equal, which they must: they
            // describe the same molecule.
            if (!instance.definition.chemistryDefined) mixString(&h, instance.sourceText);
        }
    }
    for (size_t c = 0; c < connections_.size(); ++c) {
        const SequenceConnection& conn = connections_[c];
        if (!conn.alive) continue;
        mixString(&h, connectionKindName(conn.kind));
        mixU64(&h, conn.from.polymer);
        mixU64(&h, conn.from.position);
        mixString(&h, conn.from.attachment);
        mixU64(&h, conn.to.polymer);
        mixU64(&h, conn.to.position);
        mixString(&h, conn.to.attachment);
        mixString(&h, conn.annotation);
    }
    return h;
}

uint64_t SequenceModel::digest() const {
    uint64_t h = chemistryDigest();
    // Layout participates here (persistence round trip) but NOT in the chemistry
    // digest above. The two digests exist so a test can assert exactly that.
    mixU64(&h, layout_.placements.size());
    for (std::map<MonomerInstanceId, MonomerLayout>::const_iterator it =
             layout_.placements.begin(); it != layout_.placements.end(); ++it) {
        mixU64(&h, it->first);
        mixString(&h, std::string(reinterpret_cast<const char*>(&it->second.position.x),
                                  sizeof(double)));
        mixString(&h, std::string(reinterpret_cast<const char*>(&it->second.position.y),
                                  sizeof(double)));
        mixByte(&h, it->second.lineBreakBefore ? 1 : 0);
    }
    for (size_t i = 0; i < polymers_.size(); ++i) {
        mixString(&h, polymers_[i].polymerId);
        mixString(&h, polymers_[i].strandType);
        mixString(&h, sequenceDisplayModeName(polymers_[i].display));
    }
    return h;
}

} // namespace helm
} // namespace r62
