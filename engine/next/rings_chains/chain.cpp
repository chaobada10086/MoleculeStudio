// =============================================================================
// re_round62_viewer/next/rings_chains/chain.cpp
//
// T08 - the two carbon-chain tools.
//
// PROJECT vs GUIDE, stated up front because the task requires it:
//
//   GUIDE  (bundled ChemDraw User Guide, PDF pp. 92-93, transcribed in
//          SOURCES_AND_LIMITS.md): the standard chain angle is 120 degrees;
//          fixed angles move in 15 degree increments; Alt temporarily releases
//          BOTH the fixed length and the fixed angle constraint.
//
//   PROJECT (ours, labelled): everything about HOW the chain picks its turn
//          sign, what reverse means at the pixel level, and the default
//          direction when there is no drag. The guide does not publish those.
//
// -----------------------------------------------------------------------------
// ACYCLIC CHAIN  (chooseAlkaneTool_Acyclic_Chain)
// -----------------------------------------------------------------------------
// Every bond leaves along the SAME direction, which is the drag direction. With
// fixed length on, each bond is exactly bondLength long; with fixed angles on,
// the direction is quantised to the platform angle step. Alt releases both, in
// which case the chain follows the pointer for its FIRST bond and continues
// straight from there - a straight chain is straight by definition, so releasing
// the angle constraint cannot make it bend. That limit is stated in RESULTS.md
// rather than hidden behind a claim that Alt works everywhere.
//
// -----------------------------------------------------------------------------
// SNAKING CHAIN  (chooseAlkaneTool_Snaking_Chain)
// -----------------------------------------------------------------------------
// After every bond the RUNNING DIRECTION turns by +/- (180 - chainAngleDegrees),
// so the angle between two consecutive bonds - the interior bond angle the
// drawing shows - is exactly chainAngleDegrees (GUIDE 120).
// The SIGN is fixed by the drag: if the pointer ends up on the counter-clockwise
// side of the first bond extension the chain zigs, otherwise it zags. Dragging
// back the other way therefore MIRRORS the whole chain instead of drawing a
// different shape.
//
// EQUIVALENT VIEW, and the one this implementation uses: a zig-zag chain whose
// interior bond angle is a = chainAngleDegrees starting from direction d is the
// sequence
//     d, d + s*t, d, d + s*t, ...
// where t = 180 - a is the turn between consecutive bonds and s = +/-1. The
// running direction therefore alternates between exactly two rays. A test
// asserts every interior bond angle is exactly a AND that the chain alternates
// between two directions (the real zig-zag), not merely that it looks like one.
//
// THE 120-DEGREE CHAIN ANGLE AND THE SPROUT DIRECTION ARE DIFFERENT RULES.
// snakingTurnDegrees() here is the CHAIN rule. The sprout rule in sprout.cpp is
// a separate heuristic (find the widest free gap). Keeping them apart is what
// stops the chain tool and the sprout hotkey from silently sharing a behaviour
// neither of them documents.
// =============================================================================

#include "next/rings_chains/internal.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace rings_chains {
namespace detail {

namespace {

const double kPi = 3.14159265358979323846;

// Quantise an angle to the fixed-angle step. GUIDE: 15 degree increments.
double quantiseAngle(double deg, double step) {
    if (!(step > 0.0) || !std::isfinite(step)) return normalizeDeg(deg);
    const double q = deg / step;
    const double nearest = (q < 0.0) ? std::ceil(q - 0.5) : std::floor(q + 0.5);
    return normalizeDeg(nearest * step);
}

} // namespace

} // namespace detail

double snakingTurnDegrees(int k, double chainAngleDegrees, bool reverse) {
    if (k <= 0) return 0.0;   // bond 0 uses the base direction
    // chainAngleDegrees is the INTERIOR bond angle the drawing must show, so the
    // turn applied to the running direction is its supplement.
    const double a = 180.0 - chainAngleDegrees;
    const double s = reverse ? -1.0 : 1.0;
    return ((k % 2) == 1) ? (s * a) : (-s * a);
}

namespace detail {

core::Error resolveChainPlanImpl(const core::Document& document,
                                 const ChainRequest& request,
                                 ChainPlan* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");

    ChainPlan plan;
    plan.origin = request.origin;
    plan.originAtom = request.originAtom;
    plan.documentId = document.id();
    plan.documentRevision = document.revision();

    if (request.options.atomCount < 1) {
        plan.valid = false;
        plan.rejectReason = "a chain needs at least one atom";
        *out = plan;
        return core::Error(core::ErrorCode::InvalidArgument, plan.rejectReason);
    }
    if (request.options.atomCount > 4096) {
        plan.valid = false;
        plan.rejectReason = "chain length above the 4096 atom guard";
        *out = plan;
        return core::Error(core::ErrorCode::LimitExceeded, plan.rejectReason);
    }
    if (request.originAtom != core::kInvalidObjectId &&
        !document.atoms().alive(request.originAtom)) {
        plan.valid = false;
        plan.rejectReason = "chain origin atom does not exist or is deleted";
        *out = plan;
        return core::Error(core::ErrorCode::NotFound, plan.rejectReason);
    }

    const bool alt = (request.modifiers & core::kModAlt) != 0u;
    const bool fixedLength = request.options.fixedLength && !alt;
    const bool fixedAngles = request.options.fixedAngles && !alt;
    plan.lengthConstrained = fixedLength;
    plan.anglesReleased = alt;

    const double bondLength = resolvedBondLength(document, request.options.bondLength);

    double baseDeg = request.defaultDirectionDeg;
    if (request.haveDirection) baseDeg = core::angleOf(request.pointer - request.origin);
    if (fixedAngles) baseDeg = quantiseAngle(baseDeg, request.options.angleStepDegrees);

    bool reverse = request.options.reverse;
    if (request.options.snaking && !request.options.reverse && request.haveDirection) {
        const double pointerDeg = core::angleOf(request.pointer - request.origin);
        double delta = normalizeDeg(pointerDeg - baseDeg);
        if (delta > 180.0) delta -= 360.0;
        reverse = (delta > 0.0);
    }
    plan.reversed = reverse;

    double stepLength = bondLength;
    if (!fixedLength && request.haveDirection) {
        const double d = core::length(request.pointer - request.origin);
        if (d > 0.0 && std::isfinite(d)) stepLength = d;
    }
    if (!(stepLength > 0.0) || !std::isfinite(stepLength)) {
        plan.valid = false;
        plan.rejectReason = "chain step length is not a positive finite number";
        *out = plan;
        return core::Error(core::ErrorCode::InvalidGeometry, plan.rejectReason);
    }

    plan.atoms.clear();
    plan.bonds.clear();
    plan.atoms.reserve(static_cast<size_t>(request.options.atomCount));
    plan.bonds.reserve(static_cast<size_t>(request.options.atomCount));

    const bool turnSign = request.options.reverse ? !reverse : reverse;
    double dir = baseDeg;
    core::Vec2 cursor = request.origin;
    for (int i = 0; i < request.options.atomCount; ++i) {
        if (i > 0) {
            if (request.options.snaking) {
                dir = normalizeDeg(dir + snakingTurnDegrees(
                    i, request.options.chainAngleDegrees, turnSign));
            } else {
                dir = baseDeg;
            }
            if (fixedAngles) dir = quantiseAngle(dir, request.options.angleStepDegrees);
        }
        const core::Vec2 step = core::direction(dir) * stepLength;
        const core::Vec2 pos = cursor + step;

        ChainAtom a;
        a.position = pos;
        a.element = 6;
        plan.atoms.push_back(a);

        ChainBond b;
        b.fromNewIndex = (i == 0) ? -1 : (i - 1);
        b.toNewIndex = i;
        b.order = core::BondOrder::Single;
        plan.bonds.push_back(b);

        cursor = pos;
    }
    plan.endDirectionDeg = dir;
    plan.pointCount = request.options.atomCount + 1;

    for (size_t i = 0; i < plan.atoms.size(); ++i) {
        const core::Vec2 prev = (i == 0) ? request.origin : plan.atoms[i - 1].position;
        if (core::length(plan.atoms[i].position - prev) <= 0.0) {
            plan.valid = false;
            plan.rejectReason = "chain step has zero length";
            *out = plan;
            return core::Error(core::ErrorCode::InvalidGeometry, plan.rejectReason);
        }
    }

    plan.valid = true;
    *out = plan;
    return core::Error::success();
}

} // namespace detail

ChainRequest chainRequestFor(const core::Document& document,
                             const core::Vec2& origin,
                             const core::Vec2& pointer,
                             const PlacementPolicy& policy) {
    ChainRequest r;
    r.origin = origin;
    r.pointer = pointer;
    r.modifiers = policy.modifiers;
    r.documentId = document.id();
    r.documentRevision = document.revision();
    if (policy.anchorAtom != core::kInvalidObjectId &&
        document.atoms().alive(policy.anchorAtom)) {
        r.originAtom = policy.anchorAtom;
        const core::AtomRecord* a = document.atoms().alive(policy.anchorAtom);
        if (a) r.origin = a->position;
    }
    r.options.atomCount = policy.chainAtomCount < 1 ? 1 : policy.chainAtomCount;
    r.options.bondLength = policy.bondLength;
    r.options.fixedLength = policy.fixedLength;
    r.options.fixedAngles = policy.fixedAngles;
    r.options.angleStepDegrees = policy.angleStepDegrees;
    r.options.reverse = policy.reverseChain;
    r.haveDirection = (core::length(pointer - r.origin) > 0.0);
    r.defaultDirectionDeg = policy.pointerDirectionDeg;
    return r;
}

core::Error resolveChainPlan(const core::Document& document,
                             const ChainRequest& request,
                             ChainPlan* out) {
    return detail::resolveChainPlanImpl(document, request, out);
}

} // namespace rings_chains
} // namespace r62
