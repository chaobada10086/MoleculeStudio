#ifndef R62_NEXT_ADVANCED_CLEANUP_HPP
#define R62_NEXT_ADVANCED_CLEANUP_HPP
// =============================================================================
// next/advanced/cleanup.hpp  --  T17 deterministic geometry cleanup / layout
//
// WHAT IT DOES
// -----------------------------------------------------------------------------
//   * idealises an ISOLATED ring (a ring whose every atom has exactly two
//     bonds) to a regular polygon, keeping the ring's centroid and the angle of
//     its first atom, so the result is reproducible and orientation is kept;
//   * pushes apart atoms closer than a minimum separation, never moving a fixed
//     anchor or a locked object;
//   * keeps atoms out of caption ink boxes (text avoidance);
//   * optionally arranges disconnected components on a deterministic grid/row;
//   * preserves every non-coordinate attribute, because the ONLY mutator it can
//     issue is setAtomPosition.
//
// WHAT IT DELIBERATELY DOES NOT DO
// -----------------------------------------------------------------------------
//   * It does not change elements, bond orders, bond display, stereo, query
//     flags, labels, charges or topology. There is no code path that could:
//     applyCleanupCommand() calls setAtomPosition and nothing else.
//   * It does not invent a 2D layout for a molecule from a name or a formula.
//     That is a structure generator, and T17 declares it unsupported.
//   * It does not idealise FUSED rings. A fused system has no single free
//     variable per ring, and a naive per-ring idealisation would move shared
//     atoms twice. Those rings are reported in the note and left alone.
//
// THE PLAN IS A VALUE
// -----------------------------------------------------------------------------
// A plan is produced by a pure function over a LayoutInput snapshot and then
// applied by a separate command. That split is what makes the "stale result"
// rule enforceable: the command refuses a plan whose source revision is not the
// document's current revision.
// =============================================================================

#include "next/advanced/common.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace advanced {

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
struct CleanupOptions {
    bool   idealizeRings;        // default true
    bool   resolveOverlaps;      // default true
    bool   avoidText;            // default true
    bool   arrangeComponents;    // default false (a separate tool action)
    bool   preserveAnchors;      // default true; false == anchors may move
    bool   recentre;             // default false
    bool   enforceFreshPlan;     // default true; see applyCleanupCommand

    int    ringMinSize;          // inclusive, default 3
    int    ringMaxSize;          // inclusive, default 8
    int    overlapIterations;    // default 24
    double minAtomSeparation;    // 0 == derived (0.62 * bond length)
    double componentSpacing;     // 0 == derived (2.0 * bond length)
    double idealBondLength;      // 0 == document default bond length

    core::Vec2 pivot;
    bool   usePivot;
    std::vector<core::ObjectId> scope;   // empty == every alive atom

    CleanupOptions()
        : idealizeRings(true), resolveOverlaps(true), avoidText(true),
          arrangeComponents(false), preserveAnchors(true), recentre(false),
          enforceFreshPlan(true), ringMinSize(3), ringMaxSize(8),
          overlapIterations(24), minAtomSeparation(0.0), componentSpacing(0.0),
          idealBondLength(0.0), pivot(0.0, 0.0), usePivot(false) {}
};

// ---------------------------------------------------------------------------
// Plan
// ---------------------------------------------------------------------------
struct CleanupPlan {
    bool          valid;
    core::Error   error;
    core::DocumentId documentId;   // the document the plan was computed for
    uint64_t      revision;        // the revision the plan was computed at
    uint64_t      structureSerial;

    std::vector<core::ObjectId> atoms;      // every considered atom, ascending
    std::vector<core::Vec2>     positions;  // planned position of atoms[i]
    std::vector<core::ObjectId> moved;      // atoms whose planned != original
    std::vector<core::ObjectId> anchors;    // preserved anchors actually seen
    std::vector<core::ObjectId> locked;     // preserved locked atoms
    std::vector<core::ObjectId> ringAtoms;  // atoms of idealised rings
    std::vector<core::ObjectId> componentsRepresentative; // first atom per component

    int    idealisedRingCount;
    int    overlapPairsResolved;
    int    textAvoidanceMoves;
    int    skippedFusedRingCount;
    std::string note;

    CleanupPlan()
        : valid(false), documentId(0), revision(0), structureSerial(0),
          idealisedRingCount(0), overlapPairsResolved(0), textAvoidanceMoves(0),
          skippedFusedRingCount(0) {}

    // Planned position of an atom, or false when the plan does not cover it.
    bool plannedPosition(core::ObjectId id, core::Vec2* out) const;
    size_t movedCount() const { return moved.size(); }
};

// Pure planning over a snapshot. Deterministic: identical input -> identical
// output, including the order of every vector.
core::Error planCleanup(const LayoutInput& input, const CleanupOptions& options,
                        CleanupPlan* out);
// Convenience: capture then plan on the calling thread.
core::Error planCleanup(const core::Document& doc, const CleanupOptions& options,
                        CleanupPlan* out);

// Apply a plan. The command writes ONLY atom positions. When
// options.enforceFreshPlan is set (default) it refuses a plan whose
// documentId/revision does not match the live document, which is how a layout
// computed before an intervening edit is prevented from landing.
core::Command applyCleanupCommand(const CleanupPlan& plan, bool enforceFreshPlan = true);

// Human-readable one-line summary used by RESULTS/coverage.
std::string describeCleanupPlan(const CleanupPlan& plan);

// ---------------------------------------------------------------------------
// Verification helper used by the acceptance tests and by the demo.
// ---------------------------------------------------------------------------
// Applies the plan through the document mutators WITHOUT any transaction, so a
// caller can compare structureSignature() before and after. Returns the number
// of atoms actually moved.
size_t applyPlanInPlaceQuiet(core::Document& doc, const CleanupPlan& plan);

// Snap a structure signature computed from a plan's geometry into a canonical
// form? Not needed: the signature ignores coordinates by construction. Kept as
// an explicit note for reviewers rather than as dead API.

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_CLEANUP_HPP
