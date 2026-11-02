#include "hotspot.hpp"

#include <cmath>
#include <stdexcept>

namespace r62 {
namespace interaction {

const char* hotspotKindName(HotspotKind kind) {
    switch (kind) {
    case HotspotKind::None: return "None";
    case HotspotKind::Atom: return "Atom";
    case HotspotKind::Bond: return "Bond";
    }
    return "Unknown";
}

const char* candidateKindName(CandidateKind kind) {
    switch (kind) {
    case CandidateKind::Atom: return "Atom";
    case CandidateKind::Bond: return "Bond";
    case CandidateKind::Endpoint: return "Endpoint";
    case CandidateKind::ControlHandle: return "ControlHandle";
    case CandidateKind::LabelHotspot: return "LabelHotspot";
    }
    return "Unknown";
}

namespace {
const double kPi = 3.14159265358979323846;

// Deterministic tie break. Independent of BVH traversal order, which is the
// property SOURCES_AND_LIMITS.md records as a past defect ('the same distance
// must resolve by a smaller stable id'). kind is compared first because a bond
// and an atom can legitimately share a numeric id in different slabs.
bool betterCandidate(const SnapCandidate& candidate, double candidateDistance,
                     const SnapCandidate& best, double bestDistance) {
    if (candidateDistance < bestDistance) return true;
    if (candidateDistance > bestDistance) return false;
    if (candidate.kind != best.kind) return candidate.kind < best.kind;
    return candidate.ref.id < best.ref.id;
}

// A join target must be a connectable atom. A bond body, an endpoint handle or a
// control point is a HOTSPOT but never a join target: merging a new bond into a
// selection handle would look like the new bond disappeared.
bool joinable(const SnapCandidate& c) {
    return c.eligible &&
           (c.kind == CandidateKind::Atom || c.kind == CandidateKind::LabelHotspot);
}

double effectiveRadius(const SnapProfile& profile, double zoom, bool tolerant) {
    const double base = profile.captureRadiusDevice;
    return (tolerant ? base * profile.tolerantRadiusScale : base) *
           (zoom == 0.0 ? 0.0 : 1.0 / zoom);
}

double releaseRadius(const SnapProfile& profile, double zoom, bool tolerant) {
    const double base = profile.releaseRadiusDevice;
    return (tolerant ? base * profile.tolerantRadiusScale : base) *
           (zoom == 0.0 ? 0.0 : 1.0 / zoom);
}
} // namespace

// ---------------------------------------------------------------------------
// SnapProfile
// ---------------------------------------------------------------------------
SnapProfile::SnapProfile()
    : fixedLength(true),
      fixedAngles(true),
      // [G92-93] the guide states fixed angles in 15-degree increments.
      angleStepDegrees(15.0),
      // [PROJECT] our own usability choice. NOT a documented ChemDraw constant.
      captureRadiusDevice(10.0),
      // [PROJECT] the capture/release GAP is the hysteresis that stops jitter.
      releaseRadiusDevice(13.0),
      // [PROJECT] label hotspots and handles are smaller than an atom.
      tolerantRadiusScale(0.6),
      // [PROJECT] off: the raw pointer is authoritative for join intent.
      snapConstrainedEndpoint(false),
      // [PROJECT] on: retaining the previous target inside the release ring is
      // what makes the boundary stable instead of flickering.
      retainPreviousTarget(true),
      // [PROJECT] the legacy editor's 4-pixel click/drag split, now named.
      clickSlopDevice(4.0) {}

void SnapProfile::validate(double zoom) const {
    std::string why;
    if (!validProfile(*this, zoom, &why)) throw std::invalid_argument(why);
}

bool validProfile(const SnapProfile& profile, double zoom, std::string* why) {
    if (!std::isfinite(zoom) || zoom <= 0.0) {
        if (why) *why = "zoom must be finite and positive";
        return false;
    }
    if (!std::isfinite(profile.angleStepDegrees) || profile.angleStepDegrees <= 0.0 ||
        profile.angleStepDegrees > 360.0) {
        if (why) *why = "angleStepDegrees must be in (0, 360]";
        return false;
    }
    if (!std::isfinite(profile.captureRadiusDevice) || profile.captureRadiusDevice < 0.0) {
        if (why) *why = "captureRadiusDevice must be finite and >= 0";
        return false;
    }
    if (!std::isfinite(profile.releaseRadiusDevice) ||
        profile.releaseRadiusDevice < profile.captureRadiusDevice) {
        // A release radius BELOW the capture radius would make hysteresis negative:
        // the previously retained target would be dropped the instant it was
        // captured. Rejecting it here is why the boundary cannot flicker.
        if (why) *why = "releaseRadiusDevice must be >= captureRadiusDevice";
        return false;
    }
    if (!std::isfinite(profile.tolerantRadiusScale) ||
        profile.tolerantRadiusScale <= 0.0 || profile.tolerantRadiusScale > 1.0) {
        if (why) *why = "tolerantRadiusScale must be in (0, 1]";
        return false;
    }
    if (!std::isfinite(profile.clickSlopDevice) || profile.clickSlopDevice < 0.0) {
        if (why) *why = "clickSlopDevice must be finite and >= 0";
        return false;
    }
    return true;
}

double queryRadius(const SnapProfile& profile, double zoom) {
    profile.validate(zoom);
    // The CONSERVATIVE half-extent: the release radius, because a retained target
    // is still a valid answer out to that distance. A caller that queried only
    // the capture radius would silently lose the hysteresis it configured.
    const double r = profile.releaseRadiusDevice / zoom;
    if (!std::isfinite(r)) throw std::invalid_argument("query radius overflow");
    return r;
}

// ---------------------------------------------------------------------------
// SnapResolver::findHotspot
// ---------------------------------------------------------------------------
Hotspot SnapResolver::findHotspot(const SnapProfile& profile,
                                  const core::Vec2& pointer, double zoom,
                                  CandidateSink& sink,
                                  const HysteresisState& previous,
                                  QueryStats* stats) {
    profile.validate(zoom);
    if (!pointer.finite()) throw std::invalid_argument("Nonfinite pointer");

    Hotspot result;

    // If the document identity changed, the retained hotspot belongs to a
    // DIFFERENT document and its ObjectId may now address an unrelated object.
    // Discarding it here is the 'old ID must not modify a new document' rule.
    const bool sameDocument = previous.documentId != 0 &&
                              sink.geometrySerial() == previous.geometrySerial;

    const double radius = queryRadius(profile, zoom);
    core::Box box(core::Vec2(pointer.x - radius, pointer.y - radius),
                  core::Vec2(pointer.x + radius, pointer.y + radius));
    std::vector<SnapCandidate> candidates;
    sink.query(box, &candidates, stats);

    const SnapCandidate* best = 0;
    double bestDistance = 0.0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const SnapCandidate& c = candidates[i];
        if (!c.point.finite()) throw std::invalid_argument("Nonfinite candidate");
        if (c.ref.id == core::kInvalidObjectId) continue;
        const double d = std::hypot(pointer.x - c.point.x, pointer.y - c.point.y);
        const double capture = effectiveRadius(profile, zoom, c.tolerant);
        if (d > capture) continue;
        if (!best || betterCandidate(c, d, *best, bestDistance)) {
            best = &c;
            bestDistance = d;
        }
    }
    if (stats) stats->candidatesTested += candidates.size();

    // Hysteresis pass: a previously captured object still wins while it is inside
    // the RELEASE ring, even if a different object is now marginally nearer. That
    // is precisely the anti-flicker rule at the boundary.
    if (profile.retainPreviousTarget && sameDocument &&
        previous.previousKind != HotspotKind::None &&
        previous.previous.valid()) {
        for (size_t i = 0; i < candidates.size(); ++i) {
            const SnapCandidate& c = candidates[i];
            if (c.ref != previous.previous) continue;
            if (!c.eligible) break;
            const double d = std::hypot(pointer.x - c.point.x, pointer.y - c.point.y);
            const double release = releaseRadius(profile, zoom, c.tolerant);
            if (d > release) break;
            result.kind = (c.kind == CandidateKind::Bond) ? HotspotKind::Bond : HotspotKind::Atom;
            result.object = c.ref;
            result.point = c.point;
            result.distance = d;
            result.tolerant = c.tolerant;
            result.retained = true;
            return result;
        }
    }

    if (!best) return result;
    result.kind = (best->kind == CandidateKind::Bond) ? HotspotKind::Bond : HotspotKind::Atom;
    result.object = best->ref;
    result.point = best->point;
    result.distance = bestDistance;
    result.tolerant = best->tolerant;
    result.retained = false;
    return result;
}

// ---------------------------------------------------------------------------
// SnapResolver::resolveBond
// ---------------------------------------------------------------------------
SnapResult SnapResolver::resolveBond(const SnapProfile& profile,
                                     const core::Vec2& origin,
                                     const core::Vec2& pointer, double bondLength,
                                     double zoom, CandidateSink& sink,
                                     uint32_t modifiers,
                                     const core::ObjectRef& originRef,
                                     const HysteresisState& previous,
                                     QueryStats* stats) {
    SnapResult result;
    result.endpoint = pointer;

    std::string why;
    if (!validProfile(profile, zoom, &why)) {
        result.valid = false;
        result.rejectReason = why;
        return result;
    }
    if (!origin.finite() || !pointer.finite()) {
        result.valid = false;
        result.rejectReason = "nonfinite origin or pointer";
        return result;
    }
    if (!std::isfinite(bondLength) || bondLength <= 0.0) {
        result.valid = false;
        result.rejectReason = "bondLength must be finite and positive";
        return result;
    }

    const bool alt = (modifiers & kModAlt) != 0u;

    // --- capture pass ------------------------------------------------------
    const double radius = queryRadius(profile, zoom);
    core::Box box(core::Vec2(pointer.x - radius, pointer.y - radius),
                  core::Vec2(pointer.x + radius, pointer.y + radius));
    std::vector<SnapCandidate> candidates;
    sink.query(box, &candidates, stats);
    if (stats) stats->candidatesTested += candidates.size();

    const SnapCandidate* best = 0;
    double bestDistance = 0.0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const SnapCandidate& c = candidates[i];
        if (!c.point.finite()) {
            result.valid = false;
            result.rejectReason = "nonfinite candidate";
            return result;
        }
        if (!joinable(c)) continue;
        // ORIGIN EXCLUSION. A bond may not be drawn back onto its own start atom:
        // that is a zero-length bond, which the document refuses, and a preview
        // that shows one is a preview that cannot be committed.
        if (originRef.valid() && c.ref == originRef) continue;
        const double d = std::hypot(pointer.x - c.point.x, pointer.y - c.point.y);
        if (d > effectiveRadius(profile, zoom, c.tolerant)) continue;
        if (!best || betterCandidate(c, d, *best, bestDistance)) {
            best = &c;
            bestDistance = d;
        }
    }

    // Hysteresis for the join target as well, so a drag near a boundary does not
    // alternate between joining and free placement frame to frame.
    if (profile.retainPreviousTarget && previous.previous.valid() &&
        previous.documentId != 0 &&
        sink.geometrySerial() == previous.geometrySerial) {
        for (size_t i = 0; i < candidates.size(); ++i) {
            const SnapCandidate& c = candidates[i];
            if (c.ref != previous.previous) continue;
            if (!joinable(c)) break;
            if (originRef.valid() && c.ref == originRef) break;
            const double d = std::hypot(pointer.x - c.point.x, pointer.y - c.point.y);
            if (d > releaseRadius(profile, zoom, c.tolerant)) break;
            best = &c;
            bestDistance = d;
            break;
        }
    }

    if (!best) {
        // --- constraint pass ----------------------------------------------
        const double dx = pointer.x - origin.x;
        const double dy = pointer.y - origin.y;
        const double rawLength = std::hypot(dx, dy);
        if (!std::isfinite(rawLength)) {
            result.valid = false;
            result.rejectReason = "bond vector overflow";
            return result;
        }
        // A zero-length drag is NOT given a manufactured direction. Click-to-sprout
        // is a separate caller policy that can inspect neighbouring bond angles;
        // inventing a direction here would make a stray click create a bond.
        if (rawLength == 0.0) {
            result.endpoint = origin;
            return result;
        }
        double angle = std::atan2(dy, dx);
        double lengthValue = rawLength;
        // [G92-93] Alt temporarily releases BOTH the angle and the length
        // constraint. Shift and Ctrl have NO documented effect on bond geometry,
        // so they are deliberately not consulted here: inventing a Shift-snap
        // would be an unverified compatibility claim.
        if (!alt && profile.fixedAngles) {
            const double step = profile.angleStepDegrees * kPi / 180.0;
            angle = std::round(angle / step) * step;
            result.angleConstrained = true;
        }
        if (!alt && profile.fixedLength) {
            lengthValue = bondLength;
            result.lengthConstrained = true;
        }
        result.endpoint = core::Vec2(origin.x + lengthValue * std::cos(angle),
                                     origin.y + lengthValue * std::sin(angle));
        if (!result.endpoint.finite()) {
            result.valid = false;
            result.rejectReason = "bond endpoint overflow";
            return result;
        }
        // OPTIONAL constrained-endpoint acquisition. Off by default because the
        // raw pointer is authoritative for join intent; when enabled the caller
        // must ALSO query around the constrained endpoint, which is why this is
        // an explicit opt-in rather than a hidden extra query.
        if (profile.snapConstrainedEndpoint) {
            const double r2 = queryRadius(profile, zoom);
            core::Box box2(core::Vec2(result.endpoint.x - r2, result.endpoint.y - r2),
                           core::Vec2(result.endpoint.x + r2, result.endpoint.y + r2));
            std::vector<SnapCandidate> near;
            sink.query(box2, &near, stats);
            if (stats) stats->candidatesTested += near.size();
            const SnapCandidate* second = 0;
            double secondDistance = 0.0;
            for (size_t i = 0; i < near.size(); ++i) {
                const SnapCandidate& c = near[i];
                if (!joinable(c)) continue;
                if (originRef.valid() && c.ref == originRef) continue;
                const double d = std::hypot(result.endpoint.x - c.point.x,
                                            result.endpoint.y - c.point.y);
                if (d > effectiveRadius(profile, zoom, c.tolerant)) continue;
                if (!second || betterCandidate(c, d, *second, secondDistance)) {
                    second = &c;
                    secondDistance = d;
                }
            }
            if (second) {
                result.endpoint = second->point;
                result.target = second->ref;
                result.joined = true;
                result.tolerantTarget = second->tolerant;
                result.angleConstrained = result.lengthConstrained = false;
            }
        }
        return result;
    }

    // A join ALWAYS takes the existing atom's exact stored coordinate. Using the
    // pointer position instead is the classic 'new atom lands 0.4 pt away from
    // the old one and looks like a separate atom' defect.
    result.endpoint = best->point;
    result.target = best->ref;
    result.joined = true;
    result.tolerantTarget = best->tolerant;
    result.angleConstrained = false;
    result.lengthConstrained = false;
    return result;
}

// ---------------------------------------------------------------------------
// SnapResolver::resolveFree
// ---------------------------------------------------------------------------
SnapResult SnapResolver::resolveFree(const SnapProfile& profile,
                                     const core::Vec2& origin,
                                     const core::Vec2& pointer, double bondLength,
                                     double zoom, uint32_t modifiers) {
    SnapResult result;
    result.endpoint = pointer;
    std::string why;
    if (!validProfile(profile, zoom, &why)) {
        result.valid = false;
        result.rejectReason = why;
        return result;
    }
    if (!origin.finite() || !pointer.finite()) {
        result.valid = false;
        result.rejectReason = "nonfinite origin or pointer";
        return result;
    }
    if (!std::isfinite(bondLength) || bondLength <= 0.0) {
        result.valid = false;
        result.rejectReason = "bondLength must be finite and positive";
        return result;
    }
    const bool alt = (modifiers & kModAlt) != 0u;
    const double dx = pointer.x - origin.x;
    const double dy = pointer.y - origin.y;
    const double rawLength = std::hypot(dx, dy);
    if (rawLength == 0.0) {
        result.endpoint = origin;
        return result;
    }
    double angle = std::atan2(dy, dx);
    double lengthValue = rawLength;
    if (!alt && profile.fixedAngles) {
        const double step = profile.angleStepDegrees * kPi / 180.0;
        angle = std::round(angle / step) * step;
        result.angleConstrained = true;
    }
    if (!alt && profile.fixedLength) {
        lengthValue = bondLength;
        result.lengthConstrained = true;
    }
    result.endpoint = core::Vec2(origin.x + lengthValue * std::cos(angle),
                                 origin.y + lengthValue * std::sin(angle));
    if (!result.endpoint.finite()) {
        result.valid = false;
        result.rejectReason = "bond endpoint overflow";
    }
    // NO candidate lookup at all. This is the entry point a functional-group
    // hotkey uses so that sprouting a carbonyl onto an atom with a nearby
    // carbon cannot silently become a C=C bond.
    return result;
}

// ---------------------------------------------------------------------------
// SnapResolver::constrainToAxis
// ---------------------------------------------------------------------------
core::Vec2 SnapResolver::constrainToAxis(const core::Vec2& origin,
                                         const core::Vec2& pointer) {
    // [G82][G124] Ctrl+Shift constrains a copy-drag to horizontal or vertical.
    // The dominant delta axis wins; an exact 45-degree tie resolves to the
    // horizontal so the behaviour is deterministic rather than driver- or
    // float-order-dependent.
    const double dx = pointer.x - origin.x;
    const double dy = pointer.y - origin.y;
    if (std::fabs(dx) >= std::fabs(dy)) return core::Vec2(pointer.x, origin.y);
    return core::Vec2(origin.x, pointer.y);
}

} // namespace interaction
} // namespace r62
