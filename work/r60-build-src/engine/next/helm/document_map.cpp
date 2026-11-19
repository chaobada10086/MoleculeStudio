// =============================================================================
// re_round62_viewer/next/helm/document_map.cpp
//
// T15 - the SequenceModel <-> core::Document mapping.
//
// The id contract, the extension keys and the honest limit are in document_map.hpp.
// =============================================================================

#include "next/helm/document_map.hpp"
#include "next/core/commands.hpp"

#include <cstdio>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace r62 {
namespace helm {

std::string labelForMonomer(const MonomerDefinition& definition) {
    if (!definition.symbol.empty()) return definition.symbol;
    return std::string("?");
}

int elementForMonomer(const MonomerDefinition& definition) {
    // A labelled node, never a claim about the monomer. The T01 model needs SOME
    // element for a node; carbon is its neutral default and the label override
    // carries the identity.
    (void)definition;
    return 6;
}

namespace {

// Add a monomer record to an object bag. The key is the T15 namespace, so a
// document that mixes a sequence with ordinary chemistry does not collide.
void stashMonomer(core::Document& document, core::ObjectId atom,
                  const MonomerInstance& instance, size_t polymerOrder, size_t position) {
    document.extensions(core::ObjectRef(core::ObjectKind::Atom, atom))
        .set(kKeyMonomerRecord, encodeMonomerRecord(instance, polymerOrder, position));
}

void stashConnection(core::Document& document, core::ObjectId bond,
                     const SequenceConnection& connection) {
    document.extensions(core::ObjectRef(core::ObjectKind::Bond, bond))
        .set(kKeyConnectionRecord, encodeConnectionRecord(connection));
}

} // namespace

// ----------------------------------------------------------------------------
// mapToDocument
// ----------------------------------------------------------------------------
core::Error mapToDocument(const SequenceModel& model, core::Document& document,
                          MapReport* report) {
    MapReport local;

    // Build into a SCRATCH document so a failure cannot leave the caller's
    // document half written. This is the same all-or-nothing rule the T02 reader
    // and the T01 CDXML adapter use.
    core::Document scratch;
    scratch.setDefaultBondLength(model.layout().residuePitch);

    // -------------------------------------------------------------------------
    // The id contract: reserve atom slots until the slab index equals the id.
    // -------------------------------------------------------------------------
    // ObjectSlab::push assigns id == index, so reserving in ascending id order is
    // enough. Monomer ids are allocated by the sequence model in ascending order
    // with a possible trailing dead run reuse; a reused id is always the LAST dead
    // slot, so ascending allocation still holds. This loop asserts the equality
    // and refuses to proceed if the two models could disagree.
    // NOTE: the sentinel kInvalidObjectId is 0xFFFFFFFF, so it can NOT double as the
    // "nothing seen yet" value for a maximum: `id > kInvalidObjectId` is never true
    // and the reservation would silently stay empty. A separate flag is used, and the
    // loop below is skipped entirely when the model holds no monomer.
    core::ObjectId highest = 0;
    bool anyMonomer = false;
    for (size_t p = 0; p < model.polymerCount(); ++p) {
        const SequencePolymer* polymer = model.polymer(p);
        if (!polymer) continue;
        for (size_t m = 0; m < polymer->monomers.size(); ++m) {
            const MonomerInstance& instance = polymer->monomers[m];
            if (instance.id == kInvalidMonomerInstanceId) continue;
            if (!anyMonomer || instance.id > highest) highest = instance.id;
            anyMonomer = true;
        }
    }
    std::set<core::ObjectId> ownedAtomIds;
    if (anyMonomer) {
        for (core::ObjectId i = 0; i <= highest; ++i) {
            const core::Vec2 position(0.0, 0.0);
            core::ObjectId created = core::kInvalidObjectId;
            core::Error added = scratch.addAtom(position, 6, &created);
            if (!added.ok()) return added;
            if (created != i) {
                // A mismatch means the two id allocators have diverged, which
                // would silently mis-wire every connection. Refuse loudly.
                return core::Error(core::ErrorCode::InvalidSemantics,
                    "atom slot allocation does not match the monomer id space");
            }
            // The slot stays LIVE during the reservation. Killing it here would make
            // it a trailing dead run, and ObjectSlab::push deliberately REUSES a
            // trailing dead slot -- so the next reservation would take the id we just
            // freed and the id space would no longer match the monomer ids. Unused
            // slots are therefore killed once, after every monomer has been written.
        }
    }

    // -------------------------------------------------------------------------
    // Polymer records: one per polymer, stored on the first monomer's bag, or on
    // the document bag when the polymer has no live monomer.
    // -------------------------------------------------------------------------
    std::map<size_t, std::string> polymerRecordByIndex;
    for (size_t p = 0; p < model.polymerCount(); ++p) {
        const SequencePolymer* polymer = model.polymer(p);
        if (!polymer) continue;
        polymerRecordByIndex[p] = encodePolymerRecord(*polymer, p);
        ++local.polymersCreated;
    }

    // -------------------------------------------------------------------------
    // Monomers -> atoms
    // -------------------------------------------------------------------------
    for (size_t p = 0; p < model.polymerCount(); ++p) {
        const SequencePolymer* polymer = model.polymer(p);
        if (!polymer) continue;
        for (size_t m = 0; m < polymer->monomers.size(); ++m) {
            const MonomerInstance& instance = polymer->monomers[m];
            if (instance.id == kInvalidMonomerInstanceId) continue;
            core::AtomRecord record;
            core::Error read = scratch.readAtom(instance.id, &record);
            if (!read.ok()) return read;
            if (!instance.alive) {
                // A dead monomer stays a dead slot. Killing it again is a no-op.
                scratch.atoms().kill(instance.id);
                continue;
            }
            record.alive = 1;
            ownedAtomIds.insert(instance.id);
            // The position comes from the LAYOUT, which is a view. If no layout
            // exists yet the monomer lands at the origin and a later layout pass
            // moves it; nothing about the chemistry depends on this number.
            record.position = model.layout().get(instance.id);
            record.element = static_cast<int16_t>(elementForMonomer(instance.definition));
            record.semantics.nodeType = static_cast<uint8_t>(core::NodeType::Polymer);
            record.semantics.showLabel = true;
            core::Error written = scratch.writeAtom(record);
            if (!written.ok()) return written;
            // The label override is the monomer symbol, so an ordinary T01 label
            // renderer draws something meaningful with no sequence awareness.
            scratch.setAtomLabelQuiet(instance.id, labelForMonomer(instance.definition));
            stashMonomer(scratch, instance.id, instance, p, m);
            ++local.atomsCreated;
            if (!instance.definition.chemistryDefined) {
                if (instance.definition.isBlob()) ++local.blobMonomers;
                else ++local.unknownMonomers;
            }
        }
        // Attach the polymer record to the polymer's first live monomer.
        const int first = polymer->firstLiveIndex();
        if (first >= 0) {
            const MonomerInstance& firstInstance = polymer->monomers[first];
            const core::ObjectRef ref(core::ObjectKind::Atom, firstInstance.id);
            scratch.extensions(ref).set(kKeyPolymerRecord, polymerRecordByIndex[p]);
        } else {
            std::ostringstream key;
            key << kKeyPolymerRecord << '.' << p;
            scratch.documentExtensions().set(key.str(), polymerRecordByIndex[p]);
        }
    }

    // Any reserved slot the model did not claim becomes dead exactly once. Doing it
    // here, after the last push, is what keeps the dead-run reuse from shifting an id.
    for (core::ObjectId i = 0; anyMonomer && i <= highest; ++i) {
        if (ownedAtomIds.count(i) == 0) scratch.atoms().kill(i);
    }

    // -------------------------------------------------------------------------
    // Connections -> bonds
    // -------------------------------------------------------------------------
    // A connection is an EXPLICIT bond. Only connections create bonds; adjacency in
    // the monomer list never does. That is the structural form of 'the layout is a
    // view and the connectivity is chemistry'.
    //
    // BOND IDS MATCH CONNECTION IDS. The bond slab is reserved exactly like the atom
    // slab, because building it densely would RENUMBER every later bond whenever a
    // connection was removed, and T01's own validate() requires a record's id to
    // equal its slab index. A dense rebuild therefore produced
    // "Bond id must equal its slab index" the first time an edit deleted a bond.
    core::ObjectId highestBond = 0;
    bool anyBond = false;
    for (size_t c = 0; c < model.connectionCount(); ++c) {
        const SequenceConnection* conn = model.connection(c);
        if (!conn || !conn->alive) continue;
        if (!anyBond || conn->id > highestBond) highestBond = conn->id;
        anyBond = true;
    }
    if (anyBond) {
        for (core::ObjectId i = 0; i <= highestBond; ++i) {
            core::BondRecord placeholder;
            placeholder.id = i;
            // ObjectSlab::push assigns id == index. The result is checked because a
            // mismatch would mean the two id spaces had diverged.
            if (scratch.bonds().push(placeholder) != i) {
                return core::Error(core::ErrorCode::InvalidSemantics,
                    "bond slot allocation does not match the connection id space");
            }
        }
    }
    std::set<core::ObjectId> ownedBondIds;
    for (size_t c = 0; c < model.connectionCount(); ++c) {
        const SequenceConnection* conn = model.connection(c);
        if (!conn || !conn->alive) continue;
        const SequencePolymer* pf = model.polymer(conn->from.polymer);
        const SequencePolymer* pt = model.polymer(conn->to.polymer);
        if (!pf || !pt) continue;
        if (conn->from.position >= pf->monomers.size() ||
            conn->to.position >= pt->monomers.size()) continue;
        const MonomerInstance& mf = pf->monomers[conn->from.position];
        const MonomerInstance& mt = pt->monomers[conn->to.position];
        if (!mf.alive || !mt.alive) continue;
        if (mf.id == mt.id) continue;   // a self bond is not representable
        if (!anyBond || conn->id > highestBond) continue;
        core::BondRecord& bond = scratch.bonds().mutableRecords()[conn->id];
        bond.id = conn->id;
        bond.beginAtom = mf.id;
        bond.endAtom = mt.id;
        bond.order = core::BondOrder::Single;
        if (conn->kind == ConnectionKind::Hydrogen) {
            // The T01 model reserves BondSemantics::Hydrogen for exactly this, so a
            // base pair is a real document bond that Undo and Persistence handle.
            bond.semantics = core::BondSemantics::Hydrogen;
            bond.display = core::BondDisplay::Dashed;
            ++local.hydrogenBonds;
        } else {
            bond.semantics = core::BondSemantics::Normal;
            bond.display = core::BondDisplay::Normal;
        }
        bond.alive = 1;
        stashConnection(scratch, conn->id, *conn);
        ownedBondIds.insert(conn->id);
        ++local.bondsCreated;
    }
    // Unused reserved bond slots become dead exactly once, after the last reservation.
    for (core::ObjectId i = 0; anyBond && i <= highestBond; ++i) {
        if (ownedBondIds.count(i) == 0) scratch.bonds().kill(i);
    }
    // The slab was written directly, so the derived adjacency and duplicate index are
    // rebuilt here rather than maintained incrementally.
    scratch.rebuildDerived();
    // -------------------------------------------------------------------------
    // Document header
    // -------------------------------------------------------------------------
    scratch.documentExtensions().set(kKeyDocumentHeader, "v=1");

    // Swap the scratch document in.
    //
    // The assignment copies the scratch document's `session_` pointer, which is NULL,
    // so the recording session the caller installed would be LOST here and every
    // noteTouched* call below would be a silent no-op. The pointer is saved and put
    // back around the assignment; that single line is what makes an undo step exist.
    core::DocumentSession* savedSession = document.session();
    document = scratch;
    document.setSession(savedSession);

    // -------------------------------------------------------------------------
    // Tell the T01 session what changed
    // -------------------------------------------------------------------------
    // A whole-document ASSIGNMENT bypasses every mutator, so the recording session
    // installed by Transaction::begin() would see an empty dirty list and produce an
    // EMPTY ChangeSet -- which in turn means no history entry and an undo that does
    // nothing. Every object therefore announces itself through the public
    // noteTouched* hooks, which are documented no-ops outside a transaction. Without
    // this block the sequence edit would change the model and leave the undo stack
    // silently empty.
    for (size_t i = 0; i < document.atoms().records().size(); ++i)
        document.noteTouchedAtom(document.atoms().records()[i].id);
    for (size_t i = 0; i < document.bonds().records().size(); ++i)
        document.noteTouchedBond(document.bonds().records()[i].id);
    for (size_t i = 0; i < document.texts().records().size(); ++i)
        document.noteTouchedText(document.texts().records()[i].id);
    for (size_t i = 0; i < document.paths().records().size(); ++i)
        document.noteTouchedPath(document.paths().records()[i].id);
    for (size_t i = 0; i < document.tables().records().size(); ++i)
        document.noteTouchedTable(document.tables().records()[i].id);
    for (size_t i = 0; i < document.groups().records().size(); ++i)
        document.noteTouchedGroup(document.groups().records()[i].id);
    // Object-level extension bags carry the monomer and connection records, and the
    // document bag carries the sequence header. Both are document state, so both
    // must be announced or an undo would restore the records but not their meaning.
    // The union of the AFTER bags and the session BASELINE bags must be announced.
    // Announcing only the after-state is not enough: an extension bag that this map
    // REMOVED (a monomer that was deleted, or a connection that was killed) is no
    // longer present to be iterated, so its disappearance would never enter the patch
    // and Undo could not bring it back.
    const std::map<core::ObjectRef, core::ExtensionBag>& bags =
        document.allObjectExtensions();
    for (std::map<core::ObjectRef, core::ExtensionBag>::const_iterator it = bags.begin();
         it != bags.end(); ++it) {
        document.noteTouchedExtensions(it->first, false);
    }
    core::DocumentSession* session = document.session();
    if (session) {
        const std::map<core::ObjectRef, core::ExtensionBag>& before =
            session->baseline().extensions;
        for (std::map<core::ObjectRef, core::ExtensionBag>::const_iterator it = before.begin();
             it != before.end(); ++it) {
            document.noteTouchedExtensions(it->first, false);
        }
        if (!session->baseline().documentExtensions.empty())
            document.noteTouchedExtensions(core::ObjectRef(), true);
    }
    if (!document.documentExtensions().empty())
        document.noteTouchedExtensions(core::ObjectRef(), true);
    document.noteTouchedStructure();

    if (report) *report = local;
    return core::Error();
}

// ----------------------------------------------------------------------------
// mapFromDocument
// ----------------------------------------------------------------------------
core::Error mapFromDocument(const core::Document& document, SequenceModel& model,
                            MapReport* report) {
    MapReport local;
    SequenceModel scratch;
    scratch.setLibrary(model.library());


    // ---- pass 1: polymers ----------------------------------------------------
    // A polymer record carries its own dense ORDER. Polymers are created in that
    // order, so the order IS the rebuilt index and a connection's stored polymer
    // reference needs no translation table.
    struct PendingPolymer {
        size_t order;
        bool hasOrder;
        SequencePolymer polymer;
        PendingPolymer() : order(0), hasOrder(false) {}
    };
    std::vector<PendingPolymer> pending;
    const std::map<core::ObjectRef, core::ExtensionBag>& bags =
        document.allObjectExtensions();
    for (std::map<core::ObjectRef, core::ExtensionBag>::const_iterator it = bags.begin();
         it != bags.end(); ++it) {
        const std::string* record = it->second.findValue(kKeyPolymerRecord);
        if (!record) continue;
        PendingPolymer entry;
        std::string reason;
        if (!decodePolymerRecord(*record, &entry.polymer, &entry.order, &reason))
            return core::Error(core::ErrorCode::InvalidSemantics, reason);
        entry.hasOrder = (entry.order != kInvalidOrder);
        pending.push_back(entry);
    }
    const core::ExtensionBag& documentBag = document.documentExtensions();
    for (size_t i = 0; i < documentBag.entries().size(); ++i) {
        const core::ExtensionEntry& entry = documentBag.entries()[i];
        const std::string prefix = std::string(kKeyPolymerRecord) + ".";
        if (entry.key.compare(0, prefix.size(), prefix) != 0) continue;
        PendingPolymer record;
        std::string reason;
        if (!decodePolymerRecord(entry.value, &record.polymer, &record.order, &reason))
            return core::Error(core::ErrorCode::InvalidSemantics, reason);
        record.hasOrder = (record.order != kInvalidOrder);
        if (!record.hasOrder)
            record.order = static_cast<size_t>(
                std::strtoul(entry.key.substr(prefix.size()).c_str(), 0, 10));
        record.hasOrder = true;
        pending.push_back(record);
    }
    // A record without an explicit order keeps its discovery order.
    for (size_t i = 0; i < pending.size(); ++i)
        if (!pending[i].hasOrder) pending[i].order = i;
    // Create in order, so index == order for every polymer.
    std::map<size_t, size_t> orderToIndex;
    {
        std::vector<size_t> orderList;
        for (size_t i = 0; i < pending.size(); ++i) orderList.push_back(pending[i].order);
        std::sort(orderList.begin(), orderList.end());
        orderList.erase(std::unique(orderList.begin(), orderList.end()), orderList.end());
        for (size_t o = 0; o < orderList.size(); ++o) {
            const size_t order = orderList[o];
            const PendingPolymer* source = 0;
            for (size_t i = 0; i < pending.size(); ++i)
                if (pending[i].order == order) { source = &pending[i]; break; }
            if (!source) continue;
            size_t newIndex = 0;
            core::Error added = scratch.addPolymer(source->polymer.polymerId,
                                                   source->polymer.type, &newIndex);
            if (!added.ok()) return added;
            scratch.mutablePolymer(newIndex)->display = source->polymer.display;
            scratch.mutablePolymer(newIndex)->strandType = source->polymer.strandType;
            orderToIndex[order] = newIndex;
            ++local.polymersCreated;
        }
    }
    // A polymer with no live monomer keeps its record on the DOCUMENT bag; make sure
    // an empty polymer still exists for it.
    if (pending.empty() && documentHasSequence(document)) {
        // Nothing to create; the document carries only a header.
    }

    // ---- pass 2: monomers ----------------------------------------------------
    // A monomer record states its polymer ORDER and its POSITION, so the placement is
    // a direct assignment and does not depend on the global atom id at all. Resizing
    // the polymer once, to the highest position seen, keeps dead slots in place.
    const std::vector<core::AtomRecord>& atoms = document.atoms().records();
    // Grow every polymer to the highest position its records mention before placing,
    // so a hole in the middle stays a hole and does not shift later residues.
    std::vector<size_t> requiredSize(scratch.polymerCount(), 0);
    for (size_t i = 0; i < atoms.size(); ++i) {
        const core::ObjectRef ref(core::ObjectKind::Atom, atoms[i].id);
        const std::string* record = document.extensions(ref).findValue(kKeyMonomerRecord);
        if (!record) continue;
        size_t order = kInvalidOrder, position = kInvalidOrder;
        MonomerInstance probe;
        std::string reason;
        if (!decodeMonomerRecord(*record, &probe, &order, &position, &reason))
            return core::Error(core::ErrorCode::InvalidSemantics, reason);
        std::map<size_t, size_t>::const_iterator it = orderToIndex.find(order);
        if (it == orderToIndex.end() || position == kInvalidOrder) continue;
        if (requiredSize[it->second] < position + 1) requiredSize[it->second] = position + 1;
    }
    for (size_t i = 0; i < scratch.polymerCount(); ++i) {
        SequencePolymer* polymer = scratch.mutablePolymer(i);
        if (!polymer) continue;
        while (polymer->monomers.size() < requiredSize[i]) {
            MonomerInstance filler;
            filler.id = static_cast<MonomerInstanceId>(polymer->monomers.size());
            filler.alive = false;
            filler.definition = unknownMonomer(polymer->type, std::string());
            filler.definition.symbol = std::string();
            polymer->monomers.push_back(filler);
        }
    }
    for (size_t i = 0; i < atoms.size(); ++i) {
        const core::AtomRecord& atom = atoms[i];
        const core::ObjectRef ref(core::ObjectKind::Atom, atom.id);
        const std::string* record = document.extensions(ref).findValue(kKeyMonomerRecord);
        if (!record) continue;
        MonomerInstance instance;
        size_t order = kInvalidOrder, position = kInvalidOrder;
        std::string reason;
        if (!decodeMonomerRecord(*record, &instance, &order, &position, &reason))
            return core::Error(core::ErrorCode::InvalidSemantics, reason);
        instance.id = atom.id;   // the id contract: the slot IS the id
        instance.alive = atom.isAlive();
        std::map<size_t, size_t>::const_iterator it = orderToIndex.find(order);
        if (it == orderToIndex.end()) {
            local.warnings.push_back(SequenceDiagnostic(core::ErrorCode::NotFound,
                "a monomer record names a polymer order that is not present", 0));
            continue;
        }
        SequencePolymer* polymer = scratch.mutablePolymer(it->second);
        if (!polymer) continue;
        if (position == kInvalidOrder) position = polymer->monomers.size();
        while (polymer->monomers.size() <= position) {
            MonomerInstance filler;
            filler.id = static_cast<MonomerInstanceId>(polymer->monomers.size());
            filler.alive = false;
            filler.definition = unknownMonomer(polymer->type, std::string());
            filler.definition.symbol = std::string();
            polymer->monomers.push_back(filler);
        }
        polymer->monomers[position] = instance;
        ++local.atomsCreated;
    }
    // ---- pass 3: connections -------------------------------------------------
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord& bond = bonds[i];
        const core::ObjectRef ref(core::ObjectKind::Bond, bond.id);
        const std::string* record = document.extensions(ref).findValue(kKeyConnectionRecord);
        if (!record) continue;
        if (!bond.isAlive()) continue;
        SequenceConnection connection;
        std::string reason;
        if (!decodeConnectionRecord(*record, &connection, &reason))
            return core::Error(core::ErrorCode::InvalidSemantics, reason);
        // Translate the source polymer index into the rebuilt one.
        std::map<size_t, size_t>::const_iterator fromIt = orderToIndex.find(connection.from.polymer);
        std::map<size_t, size_t>::const_iterator toIt = orderToIndex.find(connection.to.polymer);
        if (fromIt == orderToIndex.end() || toIt == orderToIndex.end()) {
            local.warnings.push_back(SequenceDiagnostic(core::ErrorCode::NotFound,
                "a stored connection names a polymer that is not present in the document", 0));
            continue;
        }
        connection.from.polymer = fromIt->second;
        connection.to.polymer = toIt->second;
        connection.id = static_cast<ConnectionId>(scratch.connectionCount());
        connection.alive = true;
        scratch.mutableConnections().push_back(connection);
        ++local.bondsCreated;
        if (connection.kind == ConnectionKind::Hydrogen) ++local.hydrogenBonds;
    }

    model = scratch;
    if (report) *report = local;
    return core::Error();
}

bool documentHasSequence(const core::Document& document) {
    if (document.documentExtensions().findValue(kKeyDocumentHeader) != 0) return true;
    const std::map<core::ObjectRef, core::ExtensionBag>& bags = document.allObjectExtensions();
    for (std::map<core::ObjectRef, core::ExtensionBag>::const_iterator it = bags.begin();
         it != bags.end(); ++it) {
        if (it->second.findValue(kKeyMonomerRecord) != 0) return true;
        if (it->second.findValue(kKeyPolymerRecord) != 0) return true;
    }
    return false;
}

std::string documentSequenceHeader(const core::Document& document) {
    const std::string* value = document.documentExtensions().findValue(kKeyDocumentHeader);
    return value ? *value : std::string();
}

} // namespace helm
} // namespace r62
