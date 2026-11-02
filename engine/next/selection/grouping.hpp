#ifndef R62_NEXT_SELECTION_GROUPING_HPP
#define R62_NEXT_SELECTION_GROUPING_HPP
// =============================================================================
// re_round62_viewer/next/selection/grouping.hpp
//
// T05 - GROUP / UNGROUP / Z-ORDER / HIERARCHY.
//
// WHAT A GROUP IS IN T01, AND WHAT IT IS NOT
// -----------------------------------------------------------------------------
// GroupRecord { id, members[], zIndex, label, sourceId, alive }. It is a flat
// record holding a member list. There is NO parent pointer on a member and NO
// nested tree container: a nested group is expressed by a member whose kind is
// Group. That is the whole hierarchy model, and this module must not invent a
// second one.
//
// CONSEQUENCES THAT SHAPE THIS API
// -----------------------------------------------------------------------------
// 1. "Ungroup" cannot be "delete the group and hope the members remember their
//    parent": there is nothing to remember it. So an ungroup is a GROUP DELETE
//    plus a re-parent of nested children, and the caller gets the resulting
//    selection back explicitly.
// 2. A member belongs to AT MOST ONE group for the hierarchy to be a tree.
//    addMember therefore REFUSES (rather than silently moving) when the member
//    already belongs to another group, and reports the owning group. A silent
//    move is how a "group of a group" turns into a member that vanished.
// 3. z-order inside a group's scope is a signed integer on the group AND on
//    nothing else. Objects outside a group have no z field at all in T01, so
//    "bring forward" for an ungrouped object is expressed by moving it in the
//    GROUP's member list it lives in, or - when it is ungrouped - reported as
//    unsupported rather than faked. This is a REAL limitation of the current
//    model and it is reported, not hidden. See ZOrderReport::unsupported.
// =============================================================================

#include "next/selection/selection_set.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// GroupReport
// ---------------------------------------------------------------------------
struct GroupReport {
    core::ObjectId group;
    uint64_t membersAdded;
    uint64_t membersRefused;      // already owned by another group
    uint64_t nestedGroups;        // member groups whose parent was set
    uint64_t membersReparented;   // on ungroup
    uint64_t groupsRemoved;
    uint64_t refsSkippedDead;
    std::string detail;
    GroupReport() : group(core::kInvalidObjectId), membersAdded(0), membersRefused(0),
                    nestedGroups(0), membersReparented(0), groupsRemoved(0),
                    refsSkippedDead(0) {}
    std::string summary() const;
};

// ---------------------------------------------------------------------------
// Group creation
// ---------------------------------------------------------------------------
// Create one group owning exactly `refs` (a group already in refs becomes a
// NESTED child, not a flat member). The whole thing is one transaction's worth
// of primitive mutations.
//
// Rules enforced (each is a refusal with a reason, never a silent fixup):
//  * a ref already owned by another group is REFUSED unless its owner is itself
//    in `refs` (then it is being nested, which is legal);
//  * a ref that is not alive is skipped and counted;
//  * fewer than two live distinct members after filtering is REFUSED - a
//    one-member group is not a group, it is a wrapper that will confuse the
//    next ungroup;
//  * a group cannot contain itself.
core::Error createGroup(core::Document& document,
                        const std::vector<core::ObjectRef>& refs,
                        const std::string& label,
                        GroupReport* report);

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------
// Add `ref` to `group`. Refuses (ErrorCode::AlreadyExists) when the ref already
// belongs to a DIFFERENT group; that owner is written into `report->detail`.
core::Error addMember(core::Document& document, core::ObjectId group,
                      const core::ObjectRef& ref, GroupReport* report);

// Remove one member. Does not delete the member object.
core::Error removeMember(core::Document& document, core::ObjectId group,
                         const core::ObjectRef& ref, GroupReport* report);

// ---------------------------------------------------------------------------
// Ungroup
// ---------------------------------------------------------------------------
// Delete the group records named by `groups`, leave their members ALIVE, and
// re-parent any nested group whose parent was deleted. The returned selection is
// the freed members, so an "undo the ungroup" restores exactly what the user
// sees. `recursive` also dissolves nested groups.
SelectionSet ungroup(core::Document& document, const std::vector<core::ObjectId>& groups,
                     bool recursive, GroupReport* report);

// Every group id that directly or transitively owns `ref`.
std::vector<core::ObjectId> groupsContaining(const core::Document& document,
                                             const core::ObjectRef& ref);

// The direct owner, or kInvalidObjectId. (Declaration lives in selection_set.hpp
// as groupOwning; this is the multi-level form.)
std::vector<core::ObjectId> allGroups(const core::Document& document);

// A nested group's PARENT, or kInvalidObjectId.
core::ObjectId parentGroupOf(const core::Document& document, core::ObjectId group);

// ---------------------------------------------------------------------------
// Z-ORDER
// ---------------------------------------------------------------------------
enum class ZOrderCommand : uint8_t {
    BringToFront = 0,
    SendToBack,
    BringForward,
    SendBackward
};
const char* zOrderCommandName(ZOrderCommand command);

// Levels are the four ChemDraw menu entries. A LEVEL is a jump of this many
// zIndex units; PROJECT value, because the guide does not state a step size and
// the model only requires an ordering.
enum { kZOrderStep = 1 };

struct ZOrderReport {
    uint64_t groupsReordered;
    uint64_t membersReordered;
    uint64_t unsupported;      // ungrouped objects: T01 has no per-object z
    uint64_t refsSkippedDead;
    std::string detail;
    ZOrderReport() : groupsReordered(0), membersReordered(0), unsupported(0),
                     refsSkippedDead(0) {}
    std::string summary() const;
};

// Reorder the selection. For a grouped object the group's zIndex moves AND the
// object is moved within the member list, because a group's zIndex orders the
// group among its SIBLINGS while the member list orders the members among
// themselves. Both are needed; doing one and calling it done is the defect.
ZOrderReport reorderObjects(core::Document& document,
                            const std::vector<core::ObjectRef>& refs,
                            ZOrderCommand command);

// Set an explicit zIndex on a group. Separate from the four commands because a
// caller that knows the exact order should not have to compute a delta from a
// step size it does not control.
core::Error setGroupZIndex(core::Document& document, core::ObjectId group, int32_t z);

// The member list of a group in draw order. Ties on zIndex break by insertion
// order, so the result is deterministic.
std::vector<core::ObjectRef> groupDrawOrder(const core::Document& document,
                                            core::ObjectId group);

// ---------------------------------------------------------------------------
// Hierarchy validation
// ---------------------------------------------------------------------------
// The invariants this module maintains, checkable in one call:
//  * every group member is alive;
//  * no object is a member of two groups;
//  * no group contains itself, directly or transitively.
// A violation is REPORTED with the offending ids; the caller decides whether to
// repair. deleteGroup cascades, and the tests assert the invariants hold after
// every operation above.
struct HierarchyViolation {
    enum Kind { DeadMember = 0, MultipleParents, SelfReference, Cycle } kind;
    core::ObjectId group;
    core::ObjectId member;
    core::ObjectId otherGroup;
    HierarchyViolation() : kind(DeadMember), group(core::kInvalidObjectId),
                           member(core::kInvalidObjectId), otherGroup(core::kInvalidObjectId) {}
};
bool validateHierarchy(const core::Document& document,
                       std::vector<HierarchyViolation>* violations);

// Apply the structural repairs the model permits without guessing intent:
// dead members are dropped, a second parent link is dropped from the NEWER
// group, and a self reference is dropped. Cycles are broken at the deepest
// back edge. Returns the number of repairs.
uint64_t repairHierarchy(core::Document& document, uint64_t* droppedMembers);

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_GROUPING_HPP
