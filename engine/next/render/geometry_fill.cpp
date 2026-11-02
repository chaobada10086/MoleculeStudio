// =============================================================================
// re_round62_viewer/next/render/geometry_fill.cpp
//
// T03 - stroke expansion and hole-aware triangulation.
// =============================================================================

#include "next/render/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace r62 {
namespace render {
namespace geom {

static const double kPi2 = 3.14159265358979323846;

static inline core::Vec2 sub2(const core::Vec2& a, const core::Vec2& b) { return core::Vec2(a.x-b.x, a.y-b.y); }
static inline core::Vec2 add2(const core::Vec2& a, const core::Vec2& b) { return core::Vec2(a.x+b.x, a.y+b.y); }
static inline core::Vec2 mul2(const core::Vec2& a, double s) { return core::Vec2(a.x*s, a.y*s); }
static inline double len2(const core::Vec2& a) { return std::sqrt(a.x*a.x + a.y*a.y); }
static inline double cross2(const core::Vec2& a, const core::Vec2& b) { return a.x*b.y - a.y*b.x; }

// ---------------------------------------------------------------------------
// Stroke -> triangles
// ---------------------------------------------------------------------------
// A round join needs a fan; everything else emits a quad per span plus a
// mitre/bevel wedge. Segments are counted so a test can assert that halving a
// curve tolerance increases the emitted segment count (the visible effect is
// smoother curves at higher zoom).
void strokePolyline(const std::vector<core::Vec2>& path, const StrokeOptions& options,
                    Mesh* mesh, size_t* segmentCount) {
    if (segmentCount) *segmentCount = 0;
    if (!mesh) return;
    if (path.size() < 2) return;
    const double hw = std::max(options.width, 0.0) * 0.5;
    if (!(hw > 0.0)) return;

    // Drop exactly-duplicate consecutive vertices; they produce zero-length
    // normals and therefore zero-area (or NaN) quads.
    std::vector<core::Vec2> pts;
    pts.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        if (!path[i].finite()) continue;
        if (!pts.empty() && len2(sub2(path[i], pts.back())) < 1e-12) continue;
        pts.push_back(path[i]);
    }
    if (options.closed && pts.size() >= 3) {
        if (len2(sub2(pts.front(), pts.back())) >= 1e-12) pts.push_back(pts.front());
    }
    if (pts.size() < 2) return;

    const double capExtend =
        (options.cap == CapStyle::Square) ? hw : 0.0;

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        core::Vec2 a = pts[i], b = pts[i + 1];
        core::Vec2 d = sub2(b, a);
        double l = len2(d);
        if (!(l > 1e-12)) continue;
        core::Vec2 dir(d.x / l, d.y / l);
        core::Vec2 n = core::Vec2(-dir.y, dir.x);

        // Extend the ends for a square cap. Round caps are handled by the circle
        // fans appended below, and deliberately do NOT extend the quad, because
        // the fan covers the cap.
        core::Vec2 a2 = a, b2 = b;
        if (i == 0 && !options.closed && options.cap == CapStyle::Square)
            a2 = sub2(a, mul2(dir, capExtend));
        if (i + 2 == pts.size() && !options.closed && options.cap == CapStyle::Square)
            b2 = add2(b, mul2(dir, capExtend));

        const core::Vec2 q0 = add2(a2, mul2(n, hw));
        const core::Vec2 q1 = add2(b2, mul2(n, hw));
        const core::Vec2 q2 = sub2(b2, mul2(n, hw));
        const core::Vec2 q3 = sub2(a2, mul2(n, hw));
        mesh->pushTriangle(q0, q1, q2);
        mesh->pushTriangle(q0, q2, q3);
        if (segmentCount) *segmentCount += 2;

        // Join with the NEXT span.
        const bool hasNext = (i + 2 < pts.size());
        if (hasNext) {
            const core::Vec2 c = pts[i + 1];
            core::Vec2 d2 = sub2(pts[i + 2], c);
            const double l2 = len2(d2);
            if (l2 > 1e-12) {
                d2 = core::Vec2(d2.x / l2, d2.y / l2);
                const core::Vec2 n2 = core::Vec2(-d2.y, d2.x);
                const double cros = cross2(dir, d2);
                const double dotp = dir.x * d2.x + dir.y * d2.y;
                if (options.join == JoinStyle::Round) {
                    // Fan across the turn. Sample count from the turn angle so a
                    // 180-degree reversal still gets a smooth semicircle.
                    const double turn = std::atan2(cros, dotp);
                    const int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(turn) / (kPi2 / 8.0))));
                    const core::Vec2 start = mul2(n, hw);
                    for (int s = 0; s < steps; ++s) {
                        const double t0 = turn * s / steps;
                        const double t1 = turn * (s + 1) / steps;
                        const core::Vec2 r0(std::cos(t0) * start.x - std::sin(t0) * start.y,
                                            std::sin(t0) * start.x + std::cos(t0) * start.y);
                        const core::Vec2 r1(std::cos(t1) * start.x - std::sin(t1) * start.y,
                                            std::sin(t1) * start.x + std::cos(t1) * start.y);
                        mesh->pushTriangle(c, add2(c, r0), add2(c, r1));
                    }
                } else {
                    // Miter where the mitre length stays inside the limit, else bevel.
                    const double denom = 1.0 + dotp;
                    bool mitred = false;
                    if (options.join == JoinStyle::Miter && denom > 1e-6) {
                        const double miterLen = 1.0 / denom;
                        if (miterLen <= options.miterLimit) {
                            const core::Vec2 m = mul2(add2(n, n2), hw * miterLen);
                            // The outer side is where the two offset points diverge;
                            // emitting both windings keeps the wedge filled whichever
                            // side is outer, at the cost of one degenerate triangle.
                            mesh->pushTriangle(c, add2(c, mul2(n, hw)), add2(c, m));
                            mesh->pushTriangle(c, add2(c, m), add2(c, mul2(n2, hw)));
                            mesh->pushTriangle(c, sub2(c, mul2(n, hw)), sub2(c, m));
                            mesh->pushTriangle(c, sub2(c, m), sub2(c, mul2(n2, hw)));
                            mitred = true;
                        }
                    }
                    if (!mitred) {
                        mesh->pushTriangle(c, add2(c, mul2(n, hw)), add2(c, mul2(n2, hw)));
                        mesh->pushTriangle(c, sub2(c, mul2(n, hw)), sub2(c, mul2(n2, hw)));
                    }
                }
            }
        }
    }

    // Round caps: a semicircle fan at each open end.
    if (!options.closed && options.cap == CapStyle::Round) {
        for (int e = 0; e < 2; ++e) {
            const size_t idx = e == 0 ? 0 : pts.size() - 1;
            const core::Vec2 c = pts[idx];
            const core::Vec2 other = e == 0 ? pts[1] : pts[pts.size() - 2];
            core::Vec2 d = sub2(c, other);
            const double l = len2(d);
            if (!(l > 1e-12)) continue;
            d = core::Vec2(d.x / l, d.y / l);
            const double base = std::atan2(d.y, d.x);
            const int steps = 8;
            for (int s = 0; s < steps; ++s) {
                const double t0 = base - kPi2 * 0.5 + kPi2 * s / steps;
                const double t1 = base - kPi2 * 0.5 + kPi2 * (s + 1) / steps;
                mesh->pushTriangle(c,
                    core::Vec2(c.x + std::cos(t0) * hw, c.y + std::sin(t0) * hw),
                    core::Vec2(c.x + std::cos(t1) * hw, c.y + std::sin(t1) * hw));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Point in contours
// ---------------------------------------------------------------------------
static bool pointInContour(const std::vector<core::Vec2>& c, const core::Vec2& p) {
    bool inside = false;
    if (c.size() < 3) return false;
    for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
        const core::Vec2& a = c[i];
        const core::Vec2& b = c[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

bool pointInContours(const std::vector<std::vector<core::Vec2> >& contours,
                     FillRule rule, const core::Vec2& p) {
    if (rule == FillRule::EvenOdd) {
        bool inside = false;
        for (size_t i = 0; i < contours.size(); ++i)
            if (pointInContour(contours[i], p)) inside = !inside;
        return inside;
    }
    // Non-zero: sum the signed winding contributions.
    int winding = 0;
    for (size_t k = 0; k < contours.size(); ++k) {
        const std::vector<core::Vec2>& c = contours[k];
        for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
            const core::Vec2& a = c[i];
            const core::Vec2& b = c[j];
            if (a.y <= p.y) {
                if (b.y > p.y && cross2(sub2(b, a), sub2(p, a)) > 0.0) ++winding;
            } else {
                if (b.y <= p.y && cross2(sub2(b, a), sub2(p, a)) < 0.0) --winding;
            }
        }
    }
    return winding != 0;
}

static double signedArea(const std::vector<core::Vec2>& c) {
    double a = 0.0;
    for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++)
        a += (c[j].x * c[i].y - c[i].x * c[j].y);
    return a * 0.5;
}

// ---------------------------------------------------------------------------
// Triangulate with holes
// ---------------------------------------------------------------------------
// Ear clipping over hole-bridged simple polygons. Holes are bridged to the outer
// contour by the standard "rightmost hole vertex to the nearest visible outer
// vertex" cut, producing one degenerate-but-valid simple polygon per hole.
// This is deliberately self-contained: GLU tessellation is unavailable on some
// drivers and unavailable at all in the headless rasterizer, and the SAME
// triangles must come out of both paths.

// Same-position test. The bridge step deliberately introduces COINCIDENT
// duplicate vertices (hole[hi] appears twice), so any containment test used by
// the ear clipper must compare POSITIONS, not indices: comparing indices leaves
// the duplicate always looking like an intruder and no ear is ever clipped.
static inline bool samePoint(const core::Vec2& p, const core::Vec2& q) {
    return std::fabs(p.x - q.x) <= 1e-12 && std::fabs(p.y - q.y) <= 1e-12;
}

// STRICT interior test: a point on an edge is NOT inside. Using a non-strict test
// here is what makes a bridged polygon fail to clip, because the bridge's two
// collinear spokes put many vertices exactly on candidate ear edges.
static bool pointInsideTriangle(const core::Vec2& p, const core::Vec2& a,
                                const core::Vec2& b, const core::Vec2& c) {
    const double d1 = cross2(sub2(b, a), sub2(p, a));
    const double d2 = cross2(sub2(c, b), sub2(p, b));
    const double d3 = cross2(sub2(a, c), sub2(p, c));
    const bool neg = (d1 < -1e-12) || (d2 < -1e-12) || (d3 < -1e-12);
    const bool pos = (d1 > 1e-12) || (d2 > 1e-12) || (d3 > 1e-12);
    return !(neg && pos);
}

static void bridgeHole(std::vector<core::Vec2>* outer, const std::vector<core::Vec2>& hole) {
    if (outer->empty() || hole.size() < 3) return;
    // Rightmost hole vertex.
    size_t hi = 0;
    for (size_t i = 1; i < hole.size(); ++i) if (hole[i].x > hole[hi].x) hi = i;
    const core::Vec2 hp = hole[hi];
    // Nearest outer vertex to its right that is not inside the hole segment.
    size_t best = 0;
    double bestD = std::numeric_limits<double>::max();
    for (size_t i = 0; i < outer->size(); ++i) {
        const double dx = (*outer)[i].x - hp.x;
        if (dx < 0.0) continue;
        const double dy = (*outer)[i].y - hp.y;
        const double d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    if (bestD == std::numeric_limits<double>::max()) {
        for (size_t i = 0; i < outer->size(); ++i) {
            const double dx = (*outer)[i].x - hp.x, dy = (*outer)[i].y - hp.y;
            const double d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    // Insert: outer[0..best], hole[hi..], hole[..hi], outer[best..]
    std::vector<core::Vec2> merged;
    merged.reserve(outer->size() + hole.size() + 2);
    for (size_t i = 0; i <= best; ++i) merged.push_back((*outer)[i]);
    for (size_t i = 0; i < hole.size(); ++i)
        merged.push_back(hole[(hi + i) % hole.size()]);
    merged.push_back(hole[hi]);
    for (size_t i = best; i < outer->size(); ++i) merged.push_back((*outer)[i]);
    outer->swap(merged);
}

static bool earClip(const std::vector<core::Vec2>& poly, bool ccw, Mesh* mesh, int depth) {
    if (poly.size() < 3) return true;
    if (depth > 4096) return false;
    std::vector<size_t> idx(poly.size());
    for (size_t i = 0; i < poly.size(); ++i) idx[i] = i;
    size_t guard = 0;
    const size_t guardMax = poly.size() * poly.size() + 16;
    while (idx.size() > 3 && guard++ < guardMax) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            const size_t i0 = idx[(i + idx.size() - 1) % idx.size()];
            const size_t i1 = idx[i];
            const size_t i2 = idx[(i + 1) % idx.size()];
            const core::Vec2& a = poly[i0];
            const core::Vec2& b = poly[i1];
            const core::Vec2& c = poly[i2];
            const double area = cross2(sub2(b, a), sub2(c, b));
            if (ccw ? !(area > 0.0) : !(area < 0.0)) continue;   // reflex or degenerate
            bool contains = false;
            for (size_t k = 0; k < idx.size(); ++k) {
                if (idx[k] == i0 || idx[k] == i1 || idx[k] == i2) continue;
                const core::Vec2& q = poly[idx[k]];
                // A vertex coincident with any corner of the candidate ear is not
                // an intruder; the bridge introduces such duplicates on purpose.
                if (samePoint(q, a) || samePoint(q, b) || samePoint(q, c)) continue;
                if (pointInsideTriangle(q, a, b, c)) { contains = true; break; }
            }
            if (contains) continue;
            mesh->pushTriangle(a, b, c);
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            // No ear found (self-intersecting or numerically hard input). Fall back
            // to a fan so the caller still gets a filled region rather than nothing.
            for (size_t i = 1; i + 1 < idx.size(); ++i)
                mesh->pushTriangle(poly[idx[0]], poly[idx[i]], poly[idx[i + 1]]);
            return true;
        }
    }
    if (idx.size() == 3) mesh->pushTriangle(poly[idx[0]], poly[idx[1]], poly[idx[2]]);
    return true;
}

core::Error triangulateContours(const std::vector<std::vector<core::Vec2> >& contours,
                                FillRule rule, Mesh* mesh) {
    if (!mesh) return core::Error(core::ErrorCode::InvalidArgument, "null mesh");
    if (contours.empty()) return core::Error(core::ErrorCode::InvalidGeometry, "no contours");
    std::vector<std::vector<core::Vec2> > clean;
    for (size_t i = 0; i < contours.size(); ++i) {
        std::vector<core::Vec2> c = contours[i];
        if (c.size() >= 2 && c.front() == c.back()) c.pop_back();
        if (c.size() < 3) continue;
        bool bad = false;
        for (size_t k = 0; k < c.size(); ++k) if (!c[k].finite()) { bad = true; break; }
        if (bad) continue;
        clean.push_back(c);
    }
    if (clean.empty()) return core::Error(core::ErrorCode::InvalidGeometry, "degenerate contours");
    if (clean.size() == 1) {
        const bool ccw = signedArea(clean[0]) > 0.0;
        earClip(clean[0], ccw, mesh, 0);
        return core::Error();
    }

    // Choose the outer contour as the one with the largest absolute area, and
    // orient it counter-clockwise for the ear clipper.
    size_t outerIdx = 0;
    double bestArea = 0.0;
    for (size_t i = 0; i < clean.size(); ++i) {
        const double a = std::fabs(signedArea(clean[i]));
        if (a > bestArea) { bestArea = a; outerIdx = i; }
    }
    std::vector<core::Vec2> outer = clean[outerIdx];
    if (signedArea(outer) < 0.0) std::reverse(outer.begin(), outer.end());

    for (size_t i = 0; i < clean.size(); ++i) {
        if (i == outerIdx) continue;
        std::vector<core::Vec2> hole = clean[i];
        const bool isHole = (rule == FillRule::EvenOdd)
            ? pointInContour(outer, hole[0])
            : true;
        if (!isHole) continue;
        if (signedArea(hole) > 0.0) std::reverse(hole.begin(), hole.end());
        bridgeHole(&outer, hole);
    }
    earClip(outer, true, mesh, 0);
    return core::Error();
}

} // namespace geom
} // namespace render
} // namespace r62
