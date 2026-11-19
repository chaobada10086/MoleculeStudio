#include "next/selection/selection_set.hpp"

#include <algorithm>
#include <deque>
#include <set>

// ===========================================================================
// Selection OPERATIONS: click / marquee / molecule / group / all / inverse /
// closure. Each one is a pure function that returns a new set and a delta.
// ===========================================================================
namespace r62 {
namespace selection {

namespace {

// Every alive object of a document, in stable (kind, id) order. Deliberately the
// SAME walk bounds.cpp uses, so Select All and documentBounds can never disagree
// about which objects exist.
void collectAllRefs(const core::Document& document, std::vector<core::ObjectRef>* out) {
    if (!out) return;
    const core::ObjectKind annotationKinds[5] = {
        core::ObjectKind::Arrow, core::ObjectKind::Bracket, core::ObjectKind::Orbital,
        core::ObjectKind::Symbol, core::ObjectKind::BioShape
    };
    const std::vector<core::AtomRecord>& atoms = document.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i)
        if (atoms[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Atom, atoms[i].id));
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i)
        if (bonds[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Bond, bonds[i].id));
    const std::vector<core::TextRecord>& texts = document.texts().records();
    for (size_t i = 0; i < texts.size(); ++i)
        if (texts[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Text, texts[i].id));
    const std::vector<core::PathRecord>& paths = document.paths().records();
    for (size_t i = 0; i < paths.size(); ++i)
        if (paths[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Path, paths[i].id));
    const std::vector<core::TableRecord>& tables = document.tables().records();
    for (size_t i = 0; i < tables.size(); ++i)
        if (tables[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Table, tables[i].id));
    const std::vector<core::GroupRecord>& groups = document.groups().records();
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Group, groups[i].id));
    const core::ObjectSlab<core::AnnotationRecord>* slabs[5] = {
        &document.arrows(), &document.brackets(), &document.orbitals(),
        &document.symbols(), &document.bioShapes()
    };
    for (int s = 0; s < 5; ++s) {
        const std::vector<core::AnnotationRecord>& records = slabs[s]->records();
        for (size_t i = 0; i < records.size(); ++i)
            if (records[i].isAlive())
                out->push_back(core::ObjectRef(annotationKinds[s], records[i].id));
    }
}

bool refAlive(const core::Document& document, const core::ObjectRef& ref) {
    switch (ref.kind) {
    case core::ObjectKind::Atom:    return document.atoms().alive(ref.id) != 0;
    case core::ObjectKind::Bond:    return document.bonds().alive(ref.id) != 0;
    case core::ObjectKind::Text:    return document.texts().alive(ref.id) != 0;
    case core::ObjectKind::Path:    return document.paths().alive(ref.id) != 0;
    case core::ObjectKind::Table:   return document.tables().alive(ref.id) != 0;
    case core::ObjectKind::Group:   return document.groups().alive(ref.id) != 0;
    case core::ObjectKind::Arrow:   return document.arrows().alive(ref.id) != 0;
    case core::ObjectKind::Bracket: return document.brackets().alive(ref.id) != 0;
    case core::ObjectKind::Orbital: return document.orbitals().alive(ref.id) != 0;
    case core::ObjectKind::Symbol:  return document.symbols().alive(ref.id) != 0;
    case core::ObjectKind::BioShape:return document.bioShapes().alive(ref.id) != 0;
    default: return false;
    }
}

// The group that directly owns `ref`, or kInvalidObjectId. Used by
// selectMolecule to decide whether a click on a member means the molecule or
// the group (the caller decides; this just answers the ownership question).
core::ObjectId owningGroup(const core::Document& document, const core::ObjectRef& ref) {
    const std::vector<core::GroupRecord>& groups = document.groups().records();
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!groups[g].isAlive()) continue;
        for (size_t m = 0; m < groups[g].members.size(); ++m)
            if (groups[g].members[m] == ref) return groups[g].id;
    }
    return core::kInvalidObjectId;
}

// Breadth-first walk over alive bonds from a seed atom set. Returns visited
// atoms and traversed bonds. Iterative on purpose: a 50,000-atom chain would
// blow a recursive implementation's stack, and a chain THAT long is one of the
// fixtures.
void connectedComponent(const core::Document& document,
                        const std::vector<core::ObjectId>& seeds,
                        std::vector<core::ObjectId>* atomsOut,
                        std::vector<core::ObjectId>* bondsOut,
                        uint64_t* scanned) {
    std::set<core::ObjectId> seenAtoms;
    std::set<core::ObjectId> seenBonds;
    std::deque<core::ObjectId> queue;
    for (size_t i = 0; i < seeds.size(); ++i) {
        if (!document.atoms().alive(seeds[i])) continue;
        if (seenAtoms.insert(seeds[i]).second) queue.push_back(seeds[i]);
    }
    while (!queue.empty()) {
        const core::ObjectId atom = queue.front();
        queue.pop_front();
        const std::vector<core::ObjectId>& incident = document.incidentBonds(atom);
        for (size_t i = 0; i < incident.size(); ++i) {
            const core::ObjectId bondId = incident[i];
            const core::BondRecord* bond = document.bonds().alive(bondId);
            if (!bond) continue;
            if (scanned) ++(*scanned);
            if (!seenBonds.insert(bondId).second) continue;
            const core::ObjectId other =
                (bond->beginAtom == atom) ? bond->endAtom : bond->beginAtom;
            if (!document.atoms().alive(other)) continue;
            if (seenAtoms.insert(other).second) queue.push_back(other);
        }
    }
    if (atomsOut) atomsOut->assign(seenAtoms.begin(), seenAtoms.end());
    if (bondsOut) bondsOut->assign(seenBonds.begin(), seenBonds.end());
}

} // namespace

// ---------------------------------------------------------------------------
// DocumentSelectionQuery
// ---------------------------------------------------------------------------
std::vector<core::ObjectRef> DocumentSelectionQuery::objectsInBox(const core::Box& box) const {
    ++boxQueries;
    ++fullScans;
    std::vector<core::ObjectRef> result;
    if (box.empty()) return result;
    std::vector<core::ObjectRef> all;
    collectAllRefs(document_, &all);
    for (size_t i = 0; i < all.size(); ++i) {
        const core::Box objectBox = objectBounds(document_, all[i], options_, 0);
        if (objectBox.empty()) continue;
        if (boxIntersectsBox(box, objectBox)) result.push_back(all[i]);
    }
    return result;
}

std::vector<core::ObjectRef> DocumentSelectionQuery::objectsInPolygon(
        const std::vector<core::Vec2>& points, bool containedOnly) const {
    ++polygonQueries;
    ++fullScans;
    std::vector<core::ObjectRef> result;
    if (points.empty()) return result;
    std::vector<core::ObjectRef> all;
    collectAllRefs(document_, &all);
    for (size_t i = 0; i < all.size(); ++i) {
        const core::Box objectBox = objectBounds(document_, all[i], options_, 0);
        if (objectBox.empty()) continue;
        if (containedOnly ? polygonContainsBox(points, objectBox)
                          : polygonIntersectsBox(points, objectBox))
            result.push_back(all[i]);
    }
    return result;
}

std::vector<core::ObjectRef> DocumentSelectionQuery::allObjects() const {
    ++fullScans;
    std::vector<core::ObjectRef> all;
    collectAllRefs(document_, &all);
    return all;
}

core::Box DocumentSelectionQuery::objectBoundsForQuery(const core::ObjectRef& ref) const {
    return objectBounds(document_, ref, options_, 0);
}

// ---------------------------------------------------------------------------
// selectOne
// ---------------------------------------------------------------------------
SelectionSet selectOne(const core::Document& document, const SelectionSet& current,
                       const core::ObjectRef& ref, bool additive, bool toggle,
                       SelectionDelta* delta) {
    SelectionSet next(current);
    uint64_t scanned = 1;
    if (!ref.valid() || !refAlive(document, ref)) {
        // A click on empty space clears, unless a modifier says "keep what I
        // have" - clicking blank canvas with Shift must not wipe the selection.
        if (!additive && !toggle) next.clear();
        if (delta) *delta = diffSelection(current, next, scanned);
        return next;
    }
    if (toggle) {
        next.toggle(ref, SelectionReason::Direct);
    } else if (additive) {
        next.add(ref, SelectionReason::Direct);
    } else {
        next.clear();
        next.add(ref, SelectionReason::Direct);
    }
    // A group pick is the group, not its members: ChemDraw's group behaves as
    // one object until you ungroup. Members come in only when the caller asks
    // for selectGroup explicitly.
    if (delta) *delta = diffSelection(current, next, scanned);
    return next;
}

// ---------------------------------------------------------------------------
// selectMarquee
// ---------------------------------------------------------------------------
SelectionSet selectMarquee(const core::Document& document, const SelectionSet& current,
                           const SelectionQuery& query, const MarqueeGesture& gesture,
                           const MarqueeOptions& options, bool additive,
                           SelectionDelta* delta) {
    uint64_t scanned = 0;
    const std::vector<core::ObjectRef> hits = queryMarquee(query, gesture, options, &scanned);
    SelectionSet next(additive ? current : SelectionSet());
    next.addAll(hits, SelectionReason::Direct);
    // Objects the marquee set previously that it no longer covers must LEAVE
    // when the marquee is non-additive; diffSelection reports that.
    next.pruneDead(document);
    if (delta) *delta = diffSelection(current, next, scanned + hits.size());
    return next;
}

// ---------------------------------------------------------------------------
// selectMolecule
// ---------------------------------------------------------------------------
SelectionSet selectMolecule(const core::Document& document, const SelectionSet& current,
                            const core::ObjectRef& seed, bool additive,
                            SelectionDelta* delta) {
    uint64_t scanned = 0;
    std::vector<core::ObjectId> seedAtoms;
    if (seed.kind == core::ObjectKind::Atom && document.atoms().alive(seed.id)) {
        seedAtoms.push_back(seed.id);
    } else if (seed.kind == core::ObjectKind::Bond) {
        const core::BondRecord* bond = document.bonds().alive(seed.id);
        if (bond) {
            if (document.atoms().alive(bond->beginAtom)) seedAtoms.push_back(bond->beginAtom);
            if (document.atoms().alive(bond->endAtom)) seedAtoms.push_back(bond->endAtom);
        }
    }

    std::vector<core::ObjectId> atoms, bonds;
    connectedComponent(document, seedAtoms, &atoms, &bonds, &scanned);

    SelectionSet next(additive ? current : SelectionSet());
    // The seed itself stays in the set even when it is not an atom (e.g. a text
    // caption the user clicked), because the user pointed at it.
    if (seed.valid() && refAlive(document, seed)) next.add(seed, SelectionReason::Direct);
    for (size_t i = 0; i < atoms.size(); ++i)
        next.add(core::ObjectRef(core::ObjectKind::Atom, atoms[i]), SelectionReason::Molecule);
    for (size_t i = 0; i < bonds.size(); ++i)
        next.add(core::ObjectRef(core::ObjectKind::Bond, bonds[i]), SelectionReason::Molecule);
    if (delta) *delta = diffSelection(current, next, scanned);
    return next;
}

// ---------------------------------------------------------------------------
// selectGroup
// ---------------------------------------------------------------------------
SelectionSet selectGroup(const core::Document& document, const SelectionSet& current,
                         core::ObjectId groupId, bool additive, SelectionDelta* delta) {
    uint64_t scanned = 0;
    SelectionSet next(additive ? current : SelectionSet());
    std::set<core::ObjectId> visiting;
    std::deque<core::ObjectId> pending;
    if (document.groups().alive(groupId)) pending.push_back(groupId);
    while (!pending.empty()) {
        const core::ObjectId id = pending.front();
        pending.pop_front();
        const core::GroupRecord* group = document.groups().alive(id);
        if (!group) continue;
        if (!visiting.insert(id).second) continue;
        next.add(core::ObjectRef(core::ObjectKind::Group, id), SelectionReason::Direct);
        ++scanned;
        for (size_t m = 0; m < group->members.size(); ++m) {
            const core::ObjectRef& member = group->members[m];
            ++scanned;
            if (!refAlive(document, member)) continue;
            // A nested group is expanded too: "select the group" means the whole
            // hierarchy, not one level of it. depth is bounded by `visiting`.
            if (member.kind == core::ObjectKind::Group) pending.push_back(member.id);
            next.add(member, SelectionReason::GroupMember);
        }
    }
    if (delta) *delta = diffSelection(current, next, scanned);
    return next;
}

// ---------------------------------------------------------------------------
// selectAll / selectInverse
// ---------------------------------------------------------------------------
SelectionSet selectAll(const core::Document& document, const SelectionQuery& query,
                       const SelectionSet& current, SelectionDelta* delta) {
    const std::vector<core::ObjectRef> all = query.allObjects();
    SelectionSet next;
    next.addAll(all, SelectionReason::Direct);
    next.pruneDead(document);
    // Groups are NOT expanded here. Select All selects every OBJECT; expanding
    // groups would add members that are already in the list and would make the
    // resulting count ambiguous.
    if (delta) *delta = diffSelection(current, next, all.size());
    return next;
}

SelectionSet selectInverse(const core::Document& document, const SelectionQuery& query,
                           const SelectionSet& current, SelectionDelta* delta) {
    const std::vector<core::ObjectRef> all = query.allObjects();
    SelectionSet next;
    next.addAll(all, SelectionReason::Direct);
    next.pruneDead(document);
    // Remove exactly what was selected; everything else stays.
    next.removeAll(current.refs());
    if (delta) *delta = diffSelection(current, next, all.size());
    return next;
}

// ---------------------------------------------------------------------------
// closeSelection
// ---------------------------------------------------------------------------
SelectionSet closeSelection(const core::Document& document, const SelectionSet& input,
                            CloseReport* report, SelectionDelta* delta) {
    SelectionSet next(input);
    uint64_t scanned = 0;
    CloseReport local;
    // One pass is NOT enough when a group contains a bond whose atoms are pulled
    // in, and the atoms' other bonds are themselves selected... The closure is
    // therefore iterated to a fixed point, bounded by the set's own growth.
    bool changed = true;
    size_t guard = 0;
    const size_t guardLimit = input.size() + 1024 + 1;
    while (changed && guard < guardLimit) {
        changed = false;
        ++guard;
        const std::vector<core::ObjectRef> snapshot(next.refs());
        for (size_t i = 0; i < snapshot.size(); ++i) {
            const core::ObjectRef ref = snapshot[i];
            ++scanned;
            if (ref.kind == core::ObjectKind::Bond) {
                const core::BondRecord* bond = document.bonds().alive(ref.id);
                if (!bond) continue;
                const core::ObjectRef a(core::ObjectKind::Atom, bond->beginAtom);
                const core::ObjectRef b(core::ObjectKind::Atom, bond->endAtom);
                if (document.atoms().alive(bond->beginAtom) && !next.contains(a)) {
                    next.add(a, SelectionReason::Dependency);
                    ++local.bondsPulledAtoms;
                    changed = true;
                }
                if (document.atoms().alive(bond->endAtom) && !next.contains(b)) {
                    next.add(b, SelectionReason::Dependency);
                    ++local.bondsPulledAtoms;
                    changed = true;
                }
            } else if (ref.kind == core::ObjectKind::Group) {
                const core::GroupRecord* group = document.groups().alive(ref.id);
                if (!group) continue;
                for (size_t m = 0; m < group->members.size(); ++m) {
                    if (!refAlive(document, group->members[m])) continue;
                    if (next.contains(group->members[m])) continue;
                    next.add(group->members[m], SelectionReason::GroupMember);
                    ++local.groupsPulledMembers;
                    changed = true;
                }
            } else if (ref.kind == core::ObjectKind::Arrow ||
                       ref.kind == core::ObjectKind::Bracket) {
                const core::ObjectSlab<core::AnnotationRecord>* slab =
                    ref.kind == core::ObjectKind::Arrow ? &document.arrows() : &document.brackets();
                const core::AnnotationRecord* record = slab->alive(ref.id);
                if (!record) continue;
                for (int a = 0; a < 4; ++a) {
                    if (record->anchors[a] == core::kInvalidObjectId) continue;
                    // An anchor is a SOFT reference: the arrow still draws when
                    // its anchor is unselected, so it is reported rather than
                    // silently pulled in. Pulling it in would make "select the
                    // arrow" also select whatever it points at.
                    ++local.annotationsWithUnselectedAnchors;
                }
            }
        }
    }
    if (report) *report = local;
    if (delta) *delta = diffSelection(input, next, scanned);
    return next;
}

SelectionSet closeSelection(const core::Document& document, const SelectionSet& input,
                            SelectionDelta* delta) {
    return closeSelection(document, input, 0, delta);
}

// ---------------------------------------------------------------------------
// mergeCompatible
// ---------------------------------------------------------------------------
bool mergeCompatible(const core::Document& document, core::ObjectId a, core::ObjectId b,
                     std::string* why) {
    const core::AtomRecord* atomA = document.atoms().alive(a);
    const core::AtomRecord* atomB = document.atoms().alive(b);
    if (!atomA || !atomB) {
        if (why) *why = "both nodes must be alive atoms";
        return false;
    }
    if (a == b) {
        if (why) *why = "an atom cannot be merged with itself";
        return false;
    }
    // DISTANCE IS NOT A COMPATIBILITY TEST. Two nodes 0.2pt apart can be a
    // carbon and a charged nitrogen; the caller's snap radius must never decide
    // this. Every semantic axis is compared explicitly.
    if (atomA->element != atomB->element) {
        if (why) *why = "different elements: distance is not a merge criterion";
        return false;
    }
    if (!(atomA->semantics == atomB->semantics)) {
        if (why) *why = "different semantics (charge/radical/isotope/nodetype/hydrogens)";
        return false;
    }
    const std::string& labelA = document.atomLabel(a);
    const std::string& labelB = document.atomLabel(b);
    if (!labelA.empty() || !labelB.empty()) {
        if (why) *why = "an atom carries a label override; merging would drop it";
        return false;
    }
    // NOTE: a pair that is ALREADY BONDED is NOT refused. Merging two bonded
    // nodes is the classic "I drew this atom twice and a bond now joins them"
    // case, and it is exactly what Ctrl+J is for. The resulting self-bond is
    // dropped by mergeSelection and counted as duplicateBondsDropped. Refusing
    // here would make the command useless for its most common use.
    if (why) *why = "compatible";
    return true;
}

// ---------------------------------------------------------------------------
// owningGroup accessor for other modules
// ---------------------------------------------------------------------------
core::ObjectId groupOwning(const core::Document& document, const core::ObjectRef& ref) {
    return owningGroup(document, ref);
}

} // namespace selection
} // namespace r62
