// =============================================================================
// re_round62_viewer/next/rings_chains/sprout.cpp
//
// T08 - the sprout DIRECTION heuristic.
//
// THE SPLIT WITH T06, STATED SO IT CANNOT DRIFT
// -----------------------------------------------------------------------------
// T06 (next/chemistry/edit.hpp) owns the CHEMICAL decision: which key builds
// what (atom hotspot 1 = a carbon, 2 = an environment-dependent carbonyl, 3 =
// phenyl, 6/7 = rings, ...), and it returns a SproutPlan of atoms and bonds
// relative to the hotspot. T06 does NOT own ring geometry or angular layout.
//
// T08 owns the DIRECTION and the PLACEMENT: given the hotspot atom's existing
// bond directions and a preferred direction, where does the new group go?
// resolveSproutDirection() answers exactly that and nothing else. It creates no
// atoms, holds no document pointer, and makes no chemical claim.
//
// -----------------------------------------------------------------------------
// THE HEURISTIC, AND WHY IT IS LABELLED PROJECT
// -----------------------------------------------------------------------------
// The verified material states:
//   GUIDE (User Guide PDF 92-93) - the standard CHAIN angle is 120 degrees.
//   GUIDE (PDF 70-75)            - the sprout hotkeys are environment
//                                  dependent, i.e. the direction is NOT a
//                                  fixed table the guide publishes.
// The guide does NOT publish the angular rule. Everything below is therefore
// our own rule and is recorded as a DIFFERENCE from the original software in
// RESULTS.md, section SproutHeuristic, rather than presented as compatibility.
//
//   0 neighbours  -> the caller's preferred direction (pointer direction).
//                    There is nothing to avoid.
//   1 neighbour   -> 120 degrees away from the existing bond, chosen on the
//                    side that keeps the new bond FARTHER from the pointer
//                    when both are geometrically free. The 120 degrees is the
//                    GUIDE chain angle; using it here is our choice.
//   2 neighbours  -> 120 degrees from the NEAREST neighbour, again taking the
//                    side with the larger gap to the other bond.
//   3+ neighbours -> the centre of the LARGEST angular gap between consecutive
//                    neighbour directions. This is the only rule that scales,
//                    and it is the one a chemist applies by eye.
//   any           -> if the largest gap is smaller than minGapDegrees the sprout
//                    is REFUSED with a reason. Silently overlapping an existing
//                    bond would look plausible in a picture and be wrong.
//
// The 1- and 2-neighbour branches deliberately DIFFER from the gap rule even
// though the gap rule could produce an answer there: at 120 degrees the new
// group matches the guide chain angle, which is what a user drawing a chain one
// carbon at a time expects. That divergence is the documented difference.
// =============================================================================

#include "next/rings_chains/internal.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace rings_chains {

namespace detail {

SproutEnvironment buildSproutEnvironmentImpl(const core::Document& document,
                                             core::ObjectId atom,
                                             double minGapDegrees) {
    SproutEnvironment env;
    env.minGapDegrees = minGapDegrees;
    const core::AtomRecord* self = document.atoms().alive(atom);
    if (!self) return env;
    env.position = self->position;
    const std::vector<core::ObjectId>& incident = document.incidentBonds(atom);
    for (size_t i = 0; i < incident.size(); ++i) {
        const core::BondRecord* b = document.bonds().alive(incident[i]);
        if (!b) continue;
        const core::ObjectId other = (b->beginAtom == atom) ? b->endAtom : b->beginAtom;
        if (other == atom) continue;
        const core::AtomRecord* n = document.atoms().alive(other);
        if (!n) continue;
        // A zero-length bond has no direction; it is skipped rather than
        // contributing a bogus 0-degree ray.
        if (core::lengthSq(n->position - self->position) <= 0.0) continue;
        env.neighbourDirectionsDeg.push_back(
            normalizeDeg(core::angleOf(n->position - self->position)));
    }
    std::sort(env.neighbourDirectionsDeg.begin(), env.neighbourDirectionsDeg.end());
    return env;
}

SproutDirection resolveSproutDirection(const SproutEnvironment& environment,
                                       double preferredDeg) {
    SproutDirection r;
    const std::vector<double>& dirs = environment.neighbourDirectionsDeg;
    r.neighbourCount = static_cast<int>(dirs.size());

    if (dirs.empty()) {
        r.ok = true;
        r.directionDeg = normalizeDeg(preferredDeg);
        r.largestGapDeg = 360.0;
        return r;
    }

    // Largest gap, computed for every branch so the diagnosis is always real.
    double           largestGap = -1.0;
    double           largestCentre = 0.0;
    std::vector<double> sorted(dirs);
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() == 1) {
        largestGap = 360.0;
        largestCentre = normalizeDeg(sorted[0] + 180.0);
    } else {
        for (size_t i = 0; i < sorted.size(); ++i) {
            const double a = sorted[i];
            const double b = sorted[(i + 1) % sorted.size()];
            double gap = normalizeDeg(b - a);
            if (gap <= 0.0) gap += 360.0;
            if (gap > largestGap) {
                largestGap = gap;
                largestCentre = normalizeDeg(a + gap * 0.5);
            }
        }
    }
    r.largestGapDeg = largestGap;

    if (largestGap < environment.minGapDegrees) {
        r.ok = false;
        r.rejectReason = "no free direction: the largest angular gap around this "
                         "atom is smaller than the minimum gap";
        return r;
    }

    if (dirs.size() == 1 || dirs.size() == 2) {
        // Branch on the nearest neighbour, then pick the side farther from the
        // other neighbour (or from the pointer when there is only one).
        double nearest = sorted[0];
        double nearestDist = 1e300;
        for (size_t i = 0; i < sorted.size(); ++i) {
            const double d = angleGapDeg(sorted[i], preferredDeg);
            if (d < nearestDist) { nearestDist = d; nearest = sorted[i]; }
        }
        const double candA = normalizeDeg(nearest + 120.0);
        const double candB = normalizeDeg(nearest - 120.0);

        double refA = preferredDeg;
        double refB = preferredDeg;
        if (sorted.size() == 2) {
            for (size_t i = 0; i < sorted.size(); ++i) {
                if (angleGapDeg(sorted[i], nearest) < 1e-9) continue;
                refA = sorted[i];
                refB = sorted[i];
            }
        }
        const double gapA = angleGapDeg(candA, refA);
        const double gapB = angleGapDeg(candB, refB);
        r.directionDeg = (gapA >= gapB) ? candA : candB;
        r.fromSingleNeighbour = true;
        r.ok = true;
        return r;
    }

    r.directionDeg = largestCentre;
    r.fromSingleNeighbour = false;
    r.ok = true;
    return r;
}

} // namespace detail

SproutEnvironment buildSproutEnvironment(const core::Document& document,
                                         core::ObjectId atom,
                                         double minGapDegrees) {
    return detail::buildSproutEnvironmentImpl(document, atom, minGapDegrees);
}

SproutDirection resolveSproutDirection(const SproutEnvironment& environment,
                                       double preferredDeg) {
    return detail::resolveSproutDirection(environment, preferredDeg);
}

} // namespace rings_chains
} // namespace r62
