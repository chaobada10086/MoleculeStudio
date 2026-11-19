#include "next/selection/grouping.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace r62 {
namespace selection {

namespace {

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

// The LOWEST-id alive group that owns `ref` (excluding `except`), so a
// deterministic "the owner" exists when the invariants have been violated and a
// repair has not run yet.
core::ObjectId ownerOf(const core::Document& document, const core::ObjectRef& ref,
                       core::ObjectId except) {
    const std::vector<core::GroupRecord>& groups = document.groups().records();
    core::ObjectId found = core::kInvalidObjectId;
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!groups[g].isAlive()) continue;
        if (groups[g].id == except) continue;
        for (size_t m = 0; m < groups[g].members.size(); ++m) {
            if (!(groups[g].members[m] == ref)) continue;
            if (found == core::kInvalidObjectId || groups[g].id < found) found = groups[g].id;
            break;
        }
    }
    return found;
}

// Remove ref from every group's member list except keep and the groups in
// preserve. createGroup passes the groups being nested so that grouping
// {outer, inner} keeps inner inside outer instead of silently re-parenting it.
uint64_t stripMembership(core::Document& document, const core::ObjectRef& ref,
                         core::ObjectId keep,
                         const std::set<core::ObjectId>* preserve) {
    uint64_t removed = 0;
    const std::vector<core::GroupRecord>& snapshot = document.groups().records();
    for (size_t g = 0; g < snapshot.size(); ++g) {
        if (!snapshot[g].isAlive()) continue;
        if (snapshot[g].id == keep) continue;
        if (preserve && preserve->count(snapshot[g].id)) continue;
        core::GroupRecord* group = document.groups().mutableGet(snapshot[g].id);
        if (!group) continue;
        const size_t before = group->members.size();
        group->members.erase(
            std::remove(group->members.begin(), group->members.end(), ref),
            group->members.end());
        if (group->members.size() != before) {
            removed += static_cast<uint64_t>(before - group->members.size());
            document.noteTouchedGroup(group->id);
            document.bumpStructure();
        }
    }
    return removed;
}
// Depth-first ancestor walk with a visited set, so a corrupt cycle terminates.
core::ObjectId parentOf(const core::Document& document, core::ObjectId group) {
    const std::vector<core::GroupRecord>& groups = document.groups().records();
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!groups[g].isAlive()) continue;
        for (size_t m = 0; m < groups[g].members.size(); ++m) {
            if (groups[g].members[m].kind == core::ObjectKind::Group &&
                groups[g].members[m].id == group)
                return groups[g].id;
        }
    }
    return core::kInvalidObjectId;
}

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
const char* zOrderCommandName(ZOrderCommand command) {
    switch (command) {
    case ZOrderCommand::BringToFront: return "bring-to-front";
    case ZOrderCommand::SendToBack:   return "send-to-back";
    case ZOrderCommand::BringForward: return "bring-forward";
    case ZOrderCommand::SendBackward: return "send-backward";
    }
    return "unknown";
}

std::string GroupReport::summary() const {
    std::ostringstream out;
    out << "group=" << group << " added=" << membersAdded
        << " refused=" << membersRefused << " nested=" << nestedGroups
        << " reparented=" << membersReparented << " groupsRemoved=" << groupsRemoved
        << " dead=" << refsSkippedDead;
    if (!detail.empty()) out << " (" << detail << ")";
    return out.str();
}

std::string ZOrderReport::summary() const {
    std::ostringstream out;
    out << "groups=" << groupsReordered << " members=" << membersReordered
        << " unsupported=" << unsupported << " dead=" << refsSkippedDead;
    if (!detail.empty()) out << " (" << detail << ")";
    return out.str();
}

// ---------------------------------------------------------------------------
// createGroup
// ---------------------------------------------------------------------------
core::Error createGroup(core::Document& document,
                        const std::vector<core::ObjectRef>& refs,
                        const std::string& label,
                        GroupReport* report) {
    GroupReport local;

    // 1. Normalise: alive, distinct, and with bonds implied by selected atoms.
    // A molecule grouped from its atoms must carry its bonds as well, otherwise
    // a later ungroup returns atoms without the bonds that make them a molecule.
    std::vector<core::ObjectRef> wanted;
    for (size_t i = 0; i < refs.size(); ++i) {
        const core::ObjectRef& ref = refs[i];
        if (!ref.valid() || !refAlive(document, ref)) { ++local.refsSkippedDead; continue; }
        wanted.push_back(ref);
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    std::set<core::ObjectRef> incoming(wanted.begin(), wanted.end());
    std::set<core::ObjectId> incomingGroups;
    std::set<core::ObjectId> wantedAtoms;
    for (size_t i = 0; i < wanted.size(); ++i) {
        if (wanted[i].kind == core::ObjectKind::Group) incomingGroups.insert(wanted[i].id);
        else if (wanted[i].kind == core::ObjectKind::Atom) wantedAtoms.insert(wanted[i].id);
    }
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    for (size_t b = 0; b < bonds.size(); ++b) {
        if (!bonds[b].isAlive()) continue;
        if (!wantedAtoms.count(bonds[b].beginAtom) || !wantedAtoms.count(bonds[b].endAtom))
            continue;
        const core::ObjectRef bondRef(core::ObjectKind::Bond, bonds[b].id);
        if (incoming.count(bondRef)) continue;
        wanted.push_back(bondRef);
        incoming.insert(bondRef);
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    // 2. An object already owned by a group that is NOT part of this operation
    // is refused as a whole. Silently grouping only the free objects would make
    // the group lose members the user did not ask to move.
    bool refused = false;
    std::ostringstream refusal;
    for (size_t i = 0; i < wanted.size(); ++i) {
        const core::ObjectRef& ref = wanted[i];
        if (ref.kind == core::ObjectKind::Group) continue;
        if (ref.kind == core::ObjectKind::Bond) {
            const core::BondRecord* bond = document.bonds().alive(ref.id);
            if (!bond) continue;
            const core::ObjectId beginOwner = ownerOf(
                document, core::ObjectRef(core::ObjectKind::Atom, bond->beginAtom),
                core::kInvalidObjectId);
            const core::ObjectId endOwner = ownerOf(
                document, core::ObjectRef(core::ObjectKind::Atom, bond->endAtom),
                core::kInvalidObjectId);
            const bool beginIncoming = beginOwner != core::kInvalidObjectId &&
                                       incomingGroups.count(beginOwner) != 0;
            const bool endIncoming = endOwner != core::kInvalidObjectId &&
                                     incomingGroups.count(endOwner) != 0;
            if (beginIncoming || endIncoming) continue;
            const core::ObjectId bondOwner = ownerOf(document, ref, core::kInvalidObjectId);
            if (beginOwner != core::kInvalidObjectId ||
                endOwner != core::kInvalidObjectId ||
                (bondOwner != core::kInvalidObjectId &&
                 !incomingGroups.count(bondOwner))) {
                ++local.membersRefused;
                refused = true;
                if (refusal.str().empty())
                    refusal << "所选化学键已属于其他编组；请连同该编组一起选择";
                continue;
            }
        }
        const core::ObjectId owner = ownerOf(document, ref, core::kInvalidObjectId);
        if (owner != core::kInvalidObjectId && !incomingGroups.count(owner)) {
            ++local.membersRefused;
            refused = true;
            if (refusal.str().empty())
                refusal << "所选对象已属于编组 " << owner << "；请连同该编组一起选择";
            continue;
        }
    }
    if (refused) {
        local.detail = refusal.str();
        if (report) *report = local;
        return core::Error(core::ErrorCode::AlreadyExists, local.detail);
    }

    // 3. Build the member list. A group whose ancestor is also being grouped is
    // already covered by that ancestor, and an object covered by an incoming
    // group must not become a second-parent member.
    core::GroupRecord record;
    record.label = label;
    for (size_t i = 0; i < wanted.size(); ++i) {
        const core::ObjectRef& ref = wanted[i];
        if (ref.kind == core::ObjectKind::Group) {
            bool coveredByAncestor = false;
            std::set<core::ObjectId> chain;
            core::ObjectId cursor = parentOf(document, ref.id);
            while (cursor != core::kInvalidObjectId && chain.insert(cursor).second) {
                if (incomingGroups.count(cursor)) { coveredByAncestor = true; break; }
                cursor = parentOf(document, cursor);
            }
            if (coveredByAncestor) continue;
            record.members.push_back(ref);
            ++local.nestedGroups;
            continue;
        }
        if (ref.kind == core::ObjectKind::Bond) {
            const core::BondRecord* bond = document.bonds().alive(ref.id);
            if (bond) {
                const core::ObjectId beginOwner = ownerOf(
                    document, core::ObjectRef(core::ObjectKind::Atom, bond->beginAtom),
                    core::kInvalidObjectId);
                const core::ObjectId endOwner = ownerOf(
                    document, core::ObjectRef(core::ObjectKind::Atom, bond->endAtom),
                    core::kInvalidObjectId);
                if ((beginOwner != core::kInvalidObjectId && incomingGroups.count(beginOwner)) ||
                    (endOwner != core::kInvalidObjectId && incomingGroups.count(endOwner)))
                    continue;
                const core::ObjectId bondOwner = ownerOf(document, ref, core::kInvalidObjectId);
                if (bondOwner != core::kInvalidObjectId && incomingGroups.count(bondOwner))
                    continue;
            }
        } else {
            const core::ObjectId owner = ownerOf(document, ref, core::kInvalidObjectId);
            if (owner != core::kInvalidObjectId && incomingGroups.count(owner)) continue;
        }
        record.members.push_back(ref);
    }

    if (record.members.size() < 2) {
        local.detail = "编组至少需要两个独立对象；已取消，未创建单成员编组";
        if (report) *report = local;
        return core::Error(core::ErrorCode::InvalidArgument, local.detail);
    }

    // Put a new group in front of every existing group; otherwise a freshly
    // created group can render behind an older one for no reason the user chose.
    bool haveZ = false;
    int32_t maxZ = 0;
    const std::vector<core::GroupRecord>& existing = document.groups().records();
    for (size_t g = 0; g < existing.size(); ++g) {
        if (!existing[g].isAlive()) continue;
        if (!haveZ) { maxZ = existing[g].zIndex; haveZ = true; }
        else maxZ = std::max(maxZ, existing[g].zIndex);
    }
    record.zIndex = haveZ ? maxZ + 1 : 1;

    core::ObjectId groupId = core::kInvalidObjectId;
    const core::Error added = document.addGroup(record, &groupId);
    if (!added.ok()) {
        if (report) *report = local;
        return added;
    }
    local.group = groupId;
    local.membersAdded = record.members.size();

    // 4. A nested group stops being owned by groups OUTSIDE the new one, but
    // keeps the nesting inside the selection (preserve).
    for (size_t i = 0; i < record.members.size(); ++i) {
        const core::ObjectRef& member = record.members[i];
        if (member.kind == core::ObjectKind::Group)
            stripMembership(document, member, groupId, &incomingGroups);
    }
    if (report) *report = local;
    return core::Error::success();
}
// ---------------------------------------------------------------------------
// addMember / removeMember
// ---------------------------------------------------------------------------
core::Error addMember(core::Document& document, core::ObjectId group,
                      const core::ObjectRef& ref, GroupReport* report) {
    GroupReport local;
    local.group = group;
    core::GroupRecord* record = document.groups().mutableGet(group);
    if (!record || !record->isAlive())
        return core::Error(core::ErrorCode::NotFound, "no such group");
    if (!ref.valid() || !refAlive(document, ref))
        return core::Error(core::ErrorCode::NotAlive, "member is not alive");
    if (ref.kind == core::ObjectKind::Group && ref.id == group)
        return core::Error(core::ErrorCode::InvalidArgument, "a group cannot contain itself");
    if (std::find(record->members.begin(), record->members.end(), ref) != record->members.end()) {
        if (report) *report = local;
        return core::Error::success();   // idempotent
    }
    const core::ObjectId owner = ownerOf(document, ref, group);
    if (owner != core::kInvalidObjectId) {
        ++local.membersRefused;
        std::ostringstream why;
        why << "member already belongs to group " << owner
            << "; refusing to silently re-parent";
        local.detail = why.str();
        if (report) *report = local;
        return core::Error(core::ErrorCode::AlreadyExists, local.detail);
    }
    record->members.push_back(ref);
    ++local.membersAdded;
    document.noteTouchedGroup(group);
    document.bumpStructure();
    // The member is now owned by exactly one group: any stale second link from a
    // corrupted earlier state is stripped, so the tree invariant is restored by
    // the operation that could break it.
    stripMembership(document, ref, group, nullptr);
    if (report) *report = local;
    return core::Error::success();
}

core::Error removeMember(core::Document& document, core::ObjectId group,
                         const core::ObjectRef& ref, GroupReport* report) {
    GroupReport local;
    local.group = group;
    core::GroupRecord* record = document.groups().mutableGet(group);
    if (!record || !record->isAlive())
        return core::Error(core::ErrorCode::NotFound, "no such group");
    const std::vector<core::ObjectRef>::iterator it =
        std::find(record->members.begin(), record->members.end(), ref);
    if (it == record->members.end()) {
        if (report) *report = local;
        return core::Error::success();   // idempotent
    }
    record->members.erase(it);
    ++local.membersReparented;
    document.noteTouchedGroup(group);
    document.bumpStructure();
    if (report) *report = local;
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// ungroup
// ---------------------------------------------------------------------------
SelectionSet ungroup(core::Document& document, const std::vector<core::ObjectId>& groups,
                     bool recursive, GroupReport* report) {
    GroupReport local;
    std::set<core::ObjectId> pending(groups.begin(), groups.end());
    std::set<core::ObjectId> removed;
    SelectionSet freed;

    while (!pending.empty()) {
        const core::ObjectId id = *pending.begin();
        pending.erase(pending.begin());
        if (removed.count(id)) continue;
        const core::GroupRecord* record = document.groups().alive(id);
        if (!record) continue;
        const std::vector<core::ObjectRef> members(record->members);

        // A nested group must be detached from every surviving parent. Without
        // this the parent keeps a reference to a dead group, which is exactly
        // the "ungroup one level and the hierarchy is corrupt" defect.
        const std::vector<core::GroupRecord>& all = document.groups().records();
        for (size_t g = 0; g < all.size(); ++g) {
            if (!all[g].isAlive() || all[g].id == id) continue;
            core::GroupRecord* parent = document.groups().mutableGet(all[g].id);
            if (!parent) continue;
            const core::ObjectRef link(core::ObjectKind::Group, id);
            const size_t before = parent->members.size();
            parent->members.erase(
                std::remove(parent->members.begin(), parent->members.end(), link),
                parent->members.end());
            if (parent->members.size() != before) {
                document.noteTouchedGroup(parent->id);
                document.bumpStructure();
            }
        }

        for (size_t m = 0; m < members.size(); ++m) {
            const core::ObjectRef& member = members[m];
            if (!refAlive(document, member)) { ++local.refsSkippedDead; continue; }
            if (member.kind == core::ObjectKind::Group && recursive) {
                // A recursive ungroup dissolves nested groups too; their
                // members are freed when their own turn comes.
                pending.insert(member.id);
                continue;
            }
            // Freed members are returned in the selection, so the user holds
            // exactly what the group released.
            freed.add(member, SelectionReason::GroupMember);
            ++local.membersReparented;
        }

        // T01's removeObject does not implement the Group class, so kill the
        // record through the same slab mutator the rest of the module uses.
        // The slot stays dead and is never renumbered, which is what lets Undo
        // restore the SAME group id with the SAME members.
        core::GroupRecord* mutableRecord = document.groups().mutableGet(id);
        if (!mutableRecord || !mutableRecord->isAlive()) continue;
        document.noteTouchedGroup(id);
        document.groups().kill(id);
        document.noteTouchedStructure();
        document.bumpStructure();
        removed.insert(id);
        ++local.groupsRemoved;
    }

    local.detail = recursive
        ? "已递归解组；所有叶对象已回到选区"
        : "已解组一层；成员已回到选区";
    if (report) *report = local;
    return freed;
}
// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
std::vector<core::ObjectId> groupsContaining(const core::Document& document,
                                             const core::ObjectRef& ref) {
    std::vector<core::ObjectId> result;
    std::set<core::ObjectId> visited;
    core::ObjectId cursor = ownerOf(document, ref, core::kInvalidObjectId);
    while (cursor != core::kInvalidObjectId) {
        if (!visited.insert(cursor).second) break;    // cycle guard
        result.push_back(cursor);
        cursor = parentOf(document, cursor);
    }
    return result;
}

std::vector<core::ObjectId> allGroups(const core::Document& document) {
    std::vector<core::ObjectId> result;
    const std::vector<core::GroupRecord>& groups = document.groups().records();
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].isAlive()) result.push_back(groups[i].id);
    return result;
}

core::ObjectId parentGroupOf(const core::Document& document, core::ObjectId group) {
    return parentOf(document, group);
}

// ---------------------------------------------------------------------------
// Z-order
// ---------------------------------------------------------------------------
core::Error setGroupZIndex(core::Document& document, core::ObjectId group, int32_t z) {
    core::GroupRecord* record = document.groups().mutableGet(group);
    if (!record || !record->isAlive())
        return core::Error(core::ErrorCode::NotFound, "no such group");
    record->zIndex = z;
    document.noteTouchedGroup(group);
    document.bumpStructure();
    return core::Error::success();
}

ZOrderReport reorderObjects(core::Document& document,
                            const std::vector<core::ObjectRef>& refs,
                            ZOrderCommand command) {
    ZOrderReport report;
    // Compute the full extent of the current z values so front/back are
    // absolute and forward/backward are a bounded step. Doing this once avoids
    // a scan per object.
    int32_t minZ = 0, maxZ = 0;
    bool seen = false;
    const std::vector<core::GroupRecord>& all = document.groups().records();
    for (size_t i = 0; i < all.size(); ++i) {
        if (!all[i].isAlive()) continue;
        if (!seen) { minZ = maxZ = all[i].zIndex; seen = true; }
        else { minZ = std::min(minZ, all[i].zIndex); maxZ = std::max(maxZ, all[i].zIndex); }
    }

    for (size_t i = 0; i < refs.size(); ++i) {
        const core::ObjectRef& ref = refs[i];
        if (!ref.valid() || !refAlive(document, ref)) { ++report.refsSkippedDead; continue; }
        // An object's z-order lives in the group that owns it. An object with no
        // group has no z field in T01 at all, and there is nothing this module
        // can honestly write - it is REPORTED, not faked by reordering the
        // document's slab indices (which would change object identity).
        const core::ObjectId owner = ownerOf(document, ref, core::kInvalidObjectId);
        const core::ObjectRef ownerRef =
            (ref.kind == core::ObjectKind::Group)
                ? core::ObjectRef(core::ObjectKind::Group, ref.id)
                : core::ObjectRef(core::ObjectKind::Group, owner);
        const bool isGroupItself = (ref.kind == core::ObjectKind::Group);
        if (!isGroupItself && owner == core::kInvalidObjectId) {
            ++report.unsupported;
            if (report.detail.empty())
                report.detail = "T01 has no per-object z index outside a group; "
                                "group the object to reorder it";
            continue;
        }

        // 1. Move the object WITHIN its owner's member list. That is the only
        //    ordering an ungrouped-inside-a-group object has.
        if (owner != core::kInvalidObjectId) {
            core::GroupRecord* group = document.groups().mutableGet(owner);
            if (group && group->isAlive()) {
                const std::vector<core::ObjectRef>::iterator it =
                    std::find(group->members.begin(), group->members.end(), ref);
                if (it != group->members.end()) {
                    const size_t index = static_cast<size_t>(it - group->members.begin());
                    const size_t last = group->members.size() - 1;
                    core::ObjectRef moved = *it;
                    group->members.erase(it);
                    switch (command) {
                    case ZOrderCommand::BringToFront:
                        group->members.push_back(moved);
                        break;
                    case ZOrderCommand::SendToBack:
                        group->members.insert(group->members.begin(), moved);
                        break;
                    case ZOrderCommand::BringForward:
                        group->members.insert(
                            group->members.begin() + static_cast<std::ptrdiff_t>(
                                std::min(index + 1, last)), moved);
                        break;
                    case ZOrderCommand::SendBackward:
                        group->members.insert(
                            group->members.begin() + static_cast<std::ptrdiff_t>(
                                index > 0 ? index - 1 : 0), moved);
                        break;
                    }
                    document.noteTouchedGroup(owner);
                    document.bumpStructure();
                    ++report.membersReordered;
                }
            }
        }

        // 2. Move the GROUP's own zIndex among its siblings, if it is a group.
        if (isGroupItself) {
            core::GroupRecord* group = document.groups().mutableGet(ref.id);
            if (group && group->isAlive()) {
                switch (command) {
                case ZOrderCommand::BringToFront: group->zIndex = maxZ + kZOrderStep; break;
                case ZOrderCommand::SendToBack:   group->zIndex = minZ - kZOrderStep; break;
                case ZOrderCommand::BringForward: group->zIndex += kZOrderStep; break;
                case ZOrderCommand::SendBackward: group->zIndex -= kZOrderStep; break;
                }
                document.noteTouchedGroup(ref.id);
                document.bumpStructure();
                ++report.groupsReordered;
            }
        }
        (void)ownerRef;
    }
    return report;
}

std::vector<core::ObjectRef> groupDrawOrder(const core::Document& document,
                                            core::ObjectId group) {
    std::vector<core::ObjectRef> result;
    const core::GroupRecord* record = document.groups().alive(group);
    if (!record) return result;
    // The member list itself is the draw order (it is maintained by
    // reorderObjects); it is returned as-is so the caller sees one order, not
    // two. A copy rather than a reference keeps the caller safe against a later
    // mutation of the group.
    result.assign(record->members.begin(), record->members.end());
    return result;
}

// ---------------------------------------------------------------------------
// Hierarchy validation / repair
// ---------------------------------------------------------------------------
bool validateHierarchy(const core::Document& document,
                       std::vector<HierarchyViolation>* violations) {
    bool ok = true;
    std::vector<HierarchyViolation> local;
    std::map<core::ObjectRef, core::ObjectId> firstOwner;
    const std::vector<core::GroupRecord>& groups = document.groups().records();
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!groups[g].isAlive()) continue;
        for (size_t m = 0; m < groups[g].members.size(); ++m) {
            const core::ObjectRef& member = groups[g].members[m];
            if (!refAlive(document, member)) {
                HierarchyViolation v;
                v.kind = HierarchyViolation::DeadMember;
                v.group = groups[g].id;
                v.member = member.id;
                local.push_back(v);
                ok = false;
                continue;
            }
            if (member.kind == core::ObjectKind::Group && member.id == groups[g].id) {
                HierarchyViolation v;
                v.kind = HierarchyViolation::SelfReference;
                v.group = groups[g].id;
                v.member = member.id;
                local.push_back(v);
                ok = false;
                continue;
            }
            const std::map<core::ObjectRef, core::ObjectId>::iterator it = firstOwner.find(member);
            if (it == firstOwner.end()) {
                firstOwner[member] = groups[g].id;
            } else if (it->second != groups[g].id) {
                HierarchyViolation v;
                v.kind = HierarchyViolation::MultipleParents;
                v.group = groups[g].id;
                v.member = member.id;
                v.otherGroup = it->second;
                local.push_back(v);
                ok = false;
            }
        }
    }
    // Cycle detection: walk each group's ancestor chain with a bounded set.
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!groups[g].isAlive()) continue;
        std::set<core::ObjectId> chain;
        core::ObjectId cursor = groups[g].id;
        size_t guard = 0;
        while (cursor != core::kInvalidObjectId && guard <= groups.size()) {
            if (!chain.insert(cursor).second) {
                HierarchyViolation v;
                v.kind = HierarchyViolation::Cycle;
                v.group = groups[g].id;
                v.member = cursor;
                local.push_back(v);
                ok = false;
                break;
            }
            cursor = parentOf(document, cursor);
            ++guard;
        }
    }
    if (violations) *violations = local;
    return ok;
}

uint64_t repairHierarchy(core::Document& document, uint64_t* droppedMembers) {
    uint64_t repairs = 0;
    uint64_t dropped = 0;
    // Newest group keeps its claim; earlier groups lose the duplicate. That is
    // the only rule that does not require guessing which link the user meant.
    std::map<core::ObjectRef, core::ObjectId> keeper;
    const std::vector<core::GroupRecord>& snapshot = document.groups().records();
    for (size_t g = 0; g < snapshot.size(); ++g) {
        if (!snapshot[g].isAlive()) continue;
        core::GroupRecord* group = document.groups().mutableGet(snapshot[g].id);
        if (!group) continue;
        std::vector<core::ObjectRef> kept;
        for (size_t m = 0; m < group->members.size(); ++m) {
            const core::ObjectRef& member = group->members[m];
            if (!refAlive(document, member)) { ++dropped; ++repairs; continue; }
            if (member.kind == core::ObjectKind::Group && member.id == group->id) {
                ++dropped; ++repairs; continue;
            }
            const std::map<core::ObjectRef, core::ObjectId>::iterator it = keeper.find(member);
            if (it != keeper.end() && it->second != group->id) {
                ++dropped; ++repairs; continue;
            }
            keeper[member] = group->id;
            kept.push_back(member);
        }
        if (kept.size() != group->members.size()) {
            group->members.swap(kept);
            document.noteTouchedGroup(group->id);
            document.bumpStructure();
        }
    }
    // --- break cycles -------------------------------------------------------
    // Repairing dead members and duplicate parents does NOT break a cycle: two
    // groups can each legitimately be the other's only parent. The cycle is
    // broken at the link that closes it, and never at a link that would leave a
    // group with zero members.
    {
        const std::vector<core::GroupRecord>& cyclic = document.groups().records();
        for (size_t g = 0; g < cyclic.size(); ++g) {
            if (!cyclic[g].isAlive()) continue;
            std::set<core::ObjectId> chain;
            core::ObjectId cursor = cyclic[g].id;
            size_t guard = 0;
            while (cursor != core::kInvalidObjectId && guard <= cyclic.size()) {
                if (!chain.insert(cursor).second) break;   // cycle found
                cursor = parentOf(document, cursor);
                ++guard;
            }
            if (cursor == core::kInvalidObjectId || guard > cyclic.size()) continue;
            // `cursor` closes the cycle: detach it from its parent's member list.
            const core::ObjectId parent = parentOf(document, cursor);
            if (parent == core::kInvalidObjectId) continue;
            core::GroupRecord* parentRecord = document.groups().mutableGet(parent);
            if (!parentRecord || !parentRecord->isAlive()) continue;
            const core::ObjectRef link(core::ObjectKind::Group, cursor);
            const size_t beforeCount = parentRecord->members.size();
            parentRecord->members.erase(
                std::remove(parentRecord->members.begin(), parentRecord->members.end(), link),
                parentRecord->members.end());
            if (parentRecord->members.size() != beforeCount) {
                ++repairs;
                ++dropped;
                document.noteTouchedGroup(parent);
                document.bumpStructure();
            }
        }
    }

    if (droppedMembers) *droppedMembers = dropped;
    return repairs;
}

} // namespace selection
} // namespace r62
