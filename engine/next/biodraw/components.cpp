// =============================================================================
// re_round62_viewer/next/biodraw/components.cpp
//
// T14 - component expansion implementation.
// =============================================================================

#include "next/biodraw/components.hpp"

#include <cmath>

namespace r62 {
namespace biodraw {

namespace {
const double kPi = 3.14159265358979323846;
double deg2rad(double d) { return d * kPi / 180.0; }
}

// ---------------------------------------------------------------------------
// HostPath
// ---------------------------------------------------------------------------
double HostPath::length() const {
    switch (kind) {
        case PathKind::Straight: {
            const double dx = end.x - start.x, dy = end.y - start.y;
            return std::sqrt(dx * dx + dy * dy);
        }
        case PathKind::Arc:
            return std::fabs(deg2rad(sweepDeg)) * radius;
        case PathKind::Ellipse:
        case PathKind::Ring: {
            // Ramanujan's second approximation: accurate to <0.02% for any
            // eccentricity, monotone in the ratio, and EXACT for a circle.
            const double a = radius, b = (kind == PathKind::Ring || radiusV <= 0.0) ? radius : radiusV;
            if (!(a > 0.0) || !(b > 0.0)) return 0.0;
            const double h = ((a - b) * (a - b)) / ((a + b) * (a + b));
            return kPi * (a + b) * (1.0 + (3.0 * h) / (10.0 + std::sqrt(4.0 - 3.0 * h)));
        }
    }
    return 0.0;
}

core::Vec2 HostPath::pointAt(double t) const {
    switch (kind) {
        case PathKind::Straight:
            return core::Vec2(start.x + t * (end.x - start.x),
                              start.y + t * (end.y - start.y));
        case PathKind::Arc: {
            const double angle = deg2rad(startDeg + t * sweepDeg);
            // core::direction(): x = cos, y = -sin (y is DOWN in document space).
            return core::Vec2(centre.x + radius * std::cos(angle),
                              centre.y - radius * std::sin(angle));
        }
        case PathKind::Ellipse:
        case PathKind::Ring: {
            const double b = (kind == PathKind::Ring || radiusV <= 0.0) ? radius : radiusV;
            const double angle = deg2rad(startDeg + t * 360.0);
            return core::Vec2(centre.x + radius * std::cos(angle),
                              centre.y - b * std::sin(angle));
        }
    }
    return start;
}

core::Vec2 HostPath::tangentAt(double t) const {
    // A central difference is used rather than a closed form because it is
    // correct for ALL four kinds including the ellipse, and the step is small
    // enough that the resulting orientation error is below a printer's dot.
    const double h = 1e-4;
    double lo = t - h, hi = t + h;
    if (kind == PathKind::Straight) { lo = 0.0; hi = 1.0; }
    else { if (lo < 0.0) { lo = 0.0; hi = 2.0 * h; } if (hi > 1.0) { hi = 1.0; lo = 1.0 - 2.0 * h; } }
    const core::Vec2 a = pointAt(lo), b = pointAt(hi);
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-12) return core::Vec2(1.0, 0.0);
    return core::Vec2(dx / len, dy / len);
}

// ---------------------------------------------------------------------------
// Boundary rules
// ---------------------------------------------------------------------------
ComponentSpec normalizeComponent(const ComponentSpec& spec, bool* clamped) {
    ComponentSpec out = spec;
    bool didClamp = false;
    if (out.count < 0) { out.count = 0; didClamp = true; }
    if (out.count > kMaxComponentCount) { out.count = kMaxComponentCount; didClamp = true; }
    if (!(out.spacing > 0.0)) { out.spacing = 0.1; didClamp = true; }
    if (!(out.unitSize >= kMinComponentUnitSize)) { out.unitSize = kMinComponentUnitSize; didClamp = true; }
    if (!(out.phase >= 0.0) || out.phase >= 1.0) {
        double wrapped = std::fmod(out.phase, 1.0);
        if (wrapped < 0.0) wrapped += 1.0;
        out.phase = wrapped;
        didClamp = true;
    }
    if (clamped) *clamped = didClamp;
    return out;
}

int drawnUnitCount(const ComponentSpec& spec) {
    if (spec.kind == ComponentKind::None) return 0;
    bool clamped = false;
    const ComponentSpec n = normalizeComponent(spec, &clamped);
    return n.count;
}

BioShapeParams normalizeComponents(const BioShapeParams& params, bool* clamped) {
    BioShapeParams out = params;
    bool didClamp = false;
    for (size_t i = 0; i < out.components.size(); ++i) {
        bool one = false;
        out.components[i] = normalizeComponent(out.components[i], &one);
        if (one) didClamp = true;
    }
    if (clamped) *clamped = didClamp;
    return out;
}

uint64_t totalUnitCount(const BioShapeParams& params) {
    uint64_t total = 0;
    for (size_t i = 0; i < params.components.size(); ++i)
        total += static_cast<uint64_t>(drawnUnitCount(params.components[i]));
    return total;
}

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------
std::vector<PlacedUnit> expandUnits(const BioShapeParams& params,
                                    size_t componentIndex,
                                    const HostPath& path) {
    std::vector<PlacedUnit> out;
    if (componentIndex >= params.components.size()) return out;

    bool clamped = false;
    const ComponentSpec spec = normalizeComponent(params.components[componentIndex], &clamped);
    const int count = drawnUnitCount(spec);
    if (count <= 0) return out;

    const double pathLength = path.length();
    // A path that has collapsed to a point cannot space units; place a single
    // unit at the point rather than dividing by zero.
    if (!(pathLength > 1e-9)) {
        PlacedUnit unit;
        unit.key = subElementKeyForUnit(static_cast<uint32_t>(componentIndex), 0u);
        unit.componentIndex = static_cast<uint32_t>(componentIndex);
        unit.unitIndex = 0;
        unit.kind = spec.kind;
        unit.centre = path.pointAt(0.0);
        unit.axisU = core::Vec2(spec.unitSize, 0.0);
        unit.axisV = core::Vec2(0.0, spec.unitSize);
        unit.t = 0.0;
        unit.outlineOnly = spec.kind == ComponentKind::LipidHead;
        out.push_back(unit);
        return out;
    }

    const double span = static_cast<double>(count);
    for (int i = 0; i < count; ++i) {
        double t = 0.0;
        if (count == 1) {
            // ONE unit is CENTRED. Placing it at t=0 would put a single lipid at
            // the very end of its membrane, which reads as a bug.
            t = 0.5;
        } else if (spec.evenlySpaced) {
            // FIT mode: first at 0, last at 1, evenly between. `spacing` acts as
            // a minimum pitch and is NOT allowed to thin the count.
            t = static_cast<double>(i) / (span - 1.0);
        } else {
            // PITCH mode: absolute step from the start. A step larger than the
            // path runs off the end, so the unit is CLAMPED to t == 1 rather than
            // wrapped -- wrapping would draw a lipid back at the start.
            const double distance = static_cast<double>(i) * spec.spacing;
            t = distance / pathLength;
            if (t > 1.0) t = 1.0;
        }
        t += spec.phase;
        if (path.kind == PathKind::Straight) {
            // A straight path has no wrap: phase pushes units off the end and
            // they are clamped, not wrapped, so the visible order is preserved.
            if (t > 1.0) t = 1.0;
            if (t < 0.0) t = 0.0;
        } else {
            t = std::fmod(t, 1.0);
            if (t < 0.0) t += 1.0;
        }

        PlacedUnit unit;
        unit.key = subElementKeyForUnit(static_cast<uint32_t>(componentIndex),
                                        static_cast<uint32_t>(i));
        unit.componentIndex = static_cast<uint32_t>(componentIndex);
        unit.unitIndex = static_cast<uint32_t>(i);
        unit.kind = spec.kind;
        unit.centre = path.pointAt(t);
        const core::Vec2 tangent = path.tangentAt(t);
        // The unit's local +x follows the path tangent, so a unit on a CURVED
        // membrane leans with the curve instead of all standing upright.
        unit.axisU = core::Vec2(tangent.x * spec.unitSize, tangent.y * spec.unitSize);
        unit.axisV = core::Vec2(-tangent.y * spec.unitSize, tangent.x * spec.unitSize);
        unit.t = t;
        unit.outlineOnly = spec.kind == ComponentKind::LipidHead ||
                           spec.kind == ComponentKind::PoreSubunit;
        out.push_back(unit);
    }
    return out;
}

std::vector<PlacedUnit> expandAllUnits(const BioShapeParams& params,
                                       const std::vector<HostPath>& paths,
                                       uint64_t* skipped) {
    std::vector<PlacedUnit> out;
    uint64_t skipCount = 0;
    for (size_t i = 0; i < params.components.size(); ++i) {
        if (i >= paths.size() || !(paths[i].length() > 0.0)) { ++skipCount; continue; }
        const std::vector<PlacedUnit> units = expandUnits(params, i, paths[i]);
        out.insert(out.end(), units.begin(), units.end());
    }
    if (skipped) *skipped = skipCount;
    return out;
}

} // namespace biodraw
} // namespace r62
