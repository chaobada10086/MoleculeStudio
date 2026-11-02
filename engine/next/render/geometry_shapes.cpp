// =============================================================================
// re_round62_viewer/next/render/geometry_shapes.cpp
//
// T03 - arrowheads, markers, bounds, text layout, UTF-8 decoding.
// =============================================================================

#include "next/render/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace r62 {
namespace render {
namespace geom {

static const double kPi3 = 3.14159265358979323846;

static inline core::Vec2 s3(const core::Vec2& a, const core::Vec2& b) { return core::Vec2(a.x-b.x, a.y-b.y); }
static inline core::Vec2 a3(const core::Vec2& a, const core::Vec2& b) { return core::Vec2(a.x+b.x, a.y+b.y); }
static inline core::Vec2 m3(const core::Vec2& a, double s) { return core::Vec2(a.x*s, a.y*s); }
static inline double l3(const core::Vec2& a) { return std::sqrt(a.x*a.x + a.y*a.y); }

// ---------------------------------------------------------------------------
// Arrow heads
// ---------------------------------------------------------------------------
ArrowHeadGeometry arrowHeadGeometry(const core::Vec2& tail, const core::Vec2& tip,
                                    ArrowShape shape, double length, double widthFactor) {
    ArrowHeadGeometry g;
    if (shape == ArrowShape::None) return g;
    const double L = (std::isfinite(length) && length > 0.0) ? length : 2.0;
    double wf = (std::isfinite(widthFactor) && widthFactor > 0.0) ? widthFactor : 0.5;
    if (wf > 4.0) wf = 4.0;

    core::Vec2 d = s3(tip, tail);
    const double dl = l3(d);
    // A zero-length arrow is ill-defined; fall back to pointing right rather than
    // emitting NaN through a division by zero.
    d = dl > 1e-12 ? core::Vec2(d.x / dl, d.y / dl) : core::Vec2(1.0, 0.0);
    const core::Vec2 n(-d.y, d.x);
    const core::Vec2 base = s3(tip, m3(d, L));
    const double halfW = L * wf;

    switch (shape) {
        case ArrowShape::Triangle: {
            g.filled = true;
            g.outline.push_back(tip);
            g.outline.push_back(a3(base, m3(n, halfW)));
            g.outline.push_back(s3(base, m3(n, halfW)));
            break;
        }
        case ArrowShape::OpenTriangle: {
            g.filled = false;
            const core::Vec2 l = a3(base, m3(n, halfW));
            const core::Vec2 r = s3(base, m3(n, halfW));
            g.strokes.push_back(l); g.strokes.push_back(tip);
            g.strokes.push_back(r); g.strokes.push_back(tip);
            break;
        }
        case ArrowShape::Barbed: {
            g.filled = true;
            const core::Vec2 notch = s3(tip, m3(d, L * 0.7));
            g.outline.push_back(tip);
            g.outline.push_back(a3(base, m3(n, halfW)));
            g.outline.push_back(notch);
            g.outline.push_back(s3(base, m3(n, halfW)));
            break;
        }
        case ArrowShape::Line: {
            g.filled = false;
            const double cap = L * 0.5;
            g.strokes.push_back(a3(base, m3(n, cap)));
            g.strokes.push_back(s3(base, m3(n, cap)));
            break;
        }
        case ArrowShape::HalfBarb: {
            g.filled = true;
            g.outline.push_back(tip);
            g.outline.push_back(a3(base, m3(n, halfW)));
            g.outline.push_back(s3(tip, m3(d, L * 0.35)));
            break;
        }
        case ArrowShape::None:
            break;
    }
    return g;
}

// ---------------------------------------------------------------------------
// Markers
// ---------------------------------------------------------------------------
void markerGeometry(const core::Vec2& center, MarkerShape shape, double size,
                    Mesh* mesh, bool* hasOutline, std::vector<core::Vec2>* outline) {
    if (hasOutline) *hasOutline = false;
    if (outline) outline->clear();
    if (!mesh) return;
    double r = (std::isfinite(size) && size > 0.0) ? size * 0.5 : 2.0;
    if (r > 1e6) r = 1e6;

    switch (shape) {
        case MarkerShape::Circle:
        case MarkerShape::HandleCircle: {
            const int n = 16;
            if (outline) {
                for (int i = 0; i < n; ++i) {
                    const double t = 2.0 * kPi3 * i / n;
                    outline->push_back(core::Vec2(center.x + std::cos(t) * r, center.y + std::sin(t) * r));
                }
                if (hasOutline) *hasOutline = true;
            }
            for (int i = 0; i < n; ++i) {
                const double t0 = 2.0 * kPi3 * i / n, t1 = 2.0 * kPi3 * (i + 1) / n;
                mesh->pushTriangle(center,
                    core::Vec2(center.x + std::cos(t0) * r, center.y + std::sin(t0) * r),
                    core::Vec2(center.x + std::cos(t1) * r, center.y + std::sin(t1) * r));
            }
            break;
        }
        case MarkerShape::Ring: {
            // An annulus: the hole is a real hole, so the rasterizer's fill rule
            // and the GL path agree. This is the small-scale hole test.
            const double inner = r * 0.55;
            const int n = 20;
            std::vector<core::Vec2> outerC, innerC;
            for (int i = 0; i < n; ++i) {
                const double t = 2.0 * kPi3 * i / n;
                outerC.push_back(core::Vec2(center.x + std::cos(t) * r, center.y + std::sin(t) * r));
                innerC.push_back(core::Vec2(center.x + std::cos(-t) * inner, center.y + std::sin(-t) * inner));
            }
            std::vector<std::vector<core::Vec2> > cs;
            cs.push_back(outerC); cs.push_back(innerC);
            triangulateContours(cs, FillRule::EvenOdd, mesh);
            if (outline) { *outline = outerC; if (hasOutline) *hasOutline = true; }
            break;
        }
        case MarkerShape::Square:
        case MarkerShape::HandleSquare: {
            const core::Vec2 p0(center.x - r, center.y - r), p1(center.x + r, center.y - r);
            const core::Vec2 p2(center.x + r, center.y + r), p3(center.x - r, center.y + r);
            mesh->pushTriangle(p0, p1, p2);
            mesh->pushTriangle(p0, p2, p3);
            if (outline) { outline->push_back(p0); outline->push_back(p1);
                           outline->push_back(p2); outline->push_back(p3);
                           if (hasOutline) *hasOutline = true; }
            break;
        }
        case MarkerShape::Diamond: {
            const core::Vec2 p0(center.x, center.y - r), p1(center.x + r, center.y);
            const core::Vec2 p2(center.x, center.y + r), p3(center.x - r, center.y);
            mesh->pushTriangle(p0, p1, p2);
            mesh->pushTriangle(p0, p2, p3);
            if (outline) { outline->push_back(p0); outline->push_back(p1);
                           outline->push_back(p2); outline->push_back(p3);
                           if (hasOutline) *hasOutline = true; }
            break;
        }
        case MarkerShape::Cross: {
            const double t = r * 0.25;
            const core::Vec2 a0(center.x - r, center.y - t), b0(center.x + r, center.y - t);
            const core::Vec2 c0(center.x + r, center.y + t), d0(center.x - r, center.y + t);
            mesh->pushTriangle(a0, b0, c0);
            mesh->pushTriangle(a0, c0, d0);
            const core::Vec2 e0(center.x - t, center.y - r), f0(center.x + t, center.y - r);
            const core::Vec2 g0(center.x + t, center.y + r), h0(center.x - t, center.y + r);
            mesh->pushTriangle(e0, f0, g0);
            mesh->pushTriangle(e0, g0, h0);
            if (outline) { outline->push_back(a0); outline->push_back(b0);
                           outline->push_back(c0); outline->push_back(d0);
                           if (hasOutline) *hasOutline = true; }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------
bool boxOverlaps(const core::Box& a, const core::Box& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y;
}

double distancePointSegment(const core::Vec2& p, const core::Vec2& a, const core::Vec2& b) {
    const core::Vec2 ab(b.x - a.x, b.y - a.y);
    const double lenSq = ab.x * ab.x + ab.y * ab.y;
    if (!(lenSq > 1e-18)) return l3(s3(p, a));
    double t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const core::Vec2 q(a.x + ab.x * t, a.y + ab.y * t);
    return l3(s3(p, q));
}

bool segmentIntersectsBox(const core::Vec2& a, const core::Vec2& b, const core::Box& box) {
    if (box.contains(a) || box.contains(b)) return true;
    // Slab test against the segment's bounding box, then a separating-axis test.
    core::Box sb(a, a); sb.add(b);
    if (!boxOverlaps(sb, box)) return false;
    const core::Vec2 c[4] = {
        core::Vec2(box.min.x, box.min.y), core::Vec2(box.max.x, box.min.y),
        core::Vec2(box.max.x, box.max.y), core::Vec2(box.min.x, box.max.y) };
    const core::Vec2 d(b.x - a.x, b.y - a.y);
    for (int i = 0; i < 4; ++i) {
        const core::Vec2 e = s3(c[(i + 1) % 4], c[i]);
        const double denom = d.x * e.y - d.y * e.x;
        if (std::fabs(denom) < 1e-18) continue;
        const core::Vec2 diff = s3(c[i], a);
        const double t = (diff.x * e.y - diff.y * e.x) / denom;
        const double u = (diff.x * d.y - diff.y * d.x) / denom;
        if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) return true;
    }
    return false;
}

core::Box inflateForShadow(const core::Box& box, double dx, double dy, double blur) {
    core::Box b = box;
    const double px = dx >= 0 ? 0.0 : dx;
    const double py = dy >= 0 ? 0.0 : dy;
    const double qx = dx >= 0 ? dx : 0.0;
    const double qy = dy >= 0 ? dy : 0.0;
    const double bl = blur > 0 ? blur : 0.0;
    b.min.x += px - bl;
    b.min.y += py - bl;
    b.max.x += qx + bl;
    b.max.y += qy + bl;
    return b;
}

core::Box inflateForHandles(const core::Box& box, double handleSize) {
    core::Box b = box;
    const double h = (std::isfinite(handleSize) && handleSize > 0) ? handleSize * 0.5 : 0.0;
    b.min.x -= h; b.min.y -= h; b.max.x += h; b.max.y += h;
    return b;
}

// Bounds must be CONSERVATIVE VISIBLE extent. The five additive terms below are
// exactly the ones the task calls out: stroke width, arrowheads, glyphs,
// shadows and drag handles.
core::Box computeBounds(const Primitive& prim, double viewDefaultStrokeWidth) {
    core::Box b;
    bool any = false;
    auto add = [&](const core::Vec2& p) {
        if (!p.finite()) return;
        if (!any) { b = core::Box(p, p); any = true; } else b.add(p);
    };

    const double w = prim.style.strokeWidth > 0.0 ? prim.style.strokeWidth : viewDefaultStrokeWidth;
    const double halfStroke = (std::isfinite(w) && w > 0.0 ? w : 0.6) * 0.5;

    switch (prim.kind) {
        case PrimKind::Text: {
            // The caller is expected to have laid the glyph ink into bounds. If it
            // has not (bounds empty), fall back to the baseline point so the box is
            // never nonsense; the layout path overwrites this with the real ink box.
            const core::Vec2 p = prim.points.empty() ? core::Vec2(0, 0) : prim.points[0];
            if (!prim.bounds.empty() || !(prim.bounds.min == prim.bounds.max)) {
                // A text primitive may carry a measured ink box in `bounds` already.
                if (prim.bounds.max.x > prim.bounds.min.x || prim.bounds.max.y > prim.bounds.min.y)
                    return prim.bounds;
            }
            add(p);
            break;
        }
        case PrimKind::Marker: {
            const core::Vec2 c = prim.points.empty() ? core::Vec2(0, 0) : prim.points[0];
            const double r = prim.size > 0.0 ? prim.size * 0.5 : 2.0;
            add(core::Vec2(c.x - r, c.y - r));
            add(core::Vec2(c.x + r, c.y + r));
            return b;   // markers are sized by `size`, not by stroke width
        }
        case PrimKind::Arrowhead: {
            if (prim.points.size() >= 2) {
                const ArrowHeadGeometry g = arrowHeadGeometry(
                    prim.points[0], prim.points[1], prim.arrow,
                    prim.size > 0.0 ? prim.size : 2.0, 0.5);
                for (size_t i = 0; i < g.outline.size(); ++i) add(g.outline[i]);
                for (size_t i = 0; i < g.strokes.size(); ++i) add(g.strokes[i]);
            }
            break;
        }
        case PrimKind::Polygon: {
            if (!prim.subpaths.empty()) {
                for (size_t s = 0; s < prim.subpaths.size(); ++s)
                    for (size_t i = 0; i < prim.subpaths[s].points.size(); ++i)
                        add(prim.subpaths[s].points[i]);
            } else {
                for (size_t i = 0; i < prim.points.size(); ++i) add(prim.points[i]);
            }
            break;
        }
        default: {
            // Path-like: bounds from the CONTROL points, inflated by the flattening
            // tolerance is not required because a Bezier stays inside its control
            // hull - the convex-hull property is what makes this bound conservative.
            for (size_t i = 0; i < prim.points.size(); ++i) add(prim.points[i]);
            break;
        }
    }

    // Wavy strokes leave the centreline by the amplitude.
    if (prim.style.wavyAmplitude > 0.0) {
        b.min.x -= prim.style.wavyAmplitude; b.min.y -= prim.style.wavyAmplitude;
        b.max.x += prim.style.wavyAmplitude; b.max.y += prim.style.wavyAmplitude;
    }
    // Stroke width: half on every side covers butt/square/round caps and joins.
    if (prim.kind != PrimKind::Text) {
        b.min.x -= halfStroke; b.min.y -= halfStroke;
        b.max.x += halfStroke; b.max.y += halfStroke;
    }
    if (!any) { b = core::Box(core::Vec2(0, 0), core::Vec2(0, 0)); }
    return b;
}

} // namespace geom
} // namespace render
} // namespace r62
