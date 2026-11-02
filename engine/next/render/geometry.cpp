// =============================================================================
// re_round62_viewer/next/render/geometry.cpp
//
// T03 - CPU geometry kernel implementation.
//
// Rules that are load-bearing and therefore stated once, here:
//   * y grows DOWN in document space. Glyph outlines use +Y up, so layoutText
//     flips y in exactly one place.
//   * Every tolerance is a DOCUMENT-space scalar derived from the view; no
//     function in this file reads a global or a device pixel.
//   * Degenerate input never throws and never produces NaN vertices; it
//     produces fewer vertices. A renderer that emits NaN kills the whole batch.
// =============================================================================

#include "next/render/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace r62 {
namespace render {

static const double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Style bridges
// ---------------------------------------------------------------------------
Style styleFromLineStyle(const core::LineStyle& ls, double viewDefaultWidth) {
    Style s;
    s.stroke.rgba = ls.colorRGBA;
    s.stroke.enabled = true;
    s.strokeWidth = ls.width > 0.0 ? ls.width : viewDefaultWidth;
    s.cap = CapStyle::Butt;
    s.join = JoinStyle::Miter;
    switch (static_cast<core::DashStyle>(ls.dash)) {
        case core::DashStyle::Solid:  break;
        case core::DashStyle::Dashed: s.dash = DashPattern(3.0, 2.0); break;
        case core::DashStyle::Dotted: s.dash = DashPattern(0.8, 1.6); break;
        case core::DashStyle::Wavy:
            // Wavy is a wave in this renderer, not a dash. Amplitude is set to
            // a fraction of the width so it scales with the line.
            s.wavyAmplitude = s.strokeWidth > 0.0 ? s.strokeWidth * 1.1 : 0.0;
            break;
    }
    return s;
}

core::DashStyle dashStyleOf(const Style& style) {
    if (style.wavyAmplitude > 0.0) return core::DashStyle::Wavy;
    if (style.dash.solid())        return core::DashStyle::Solid;
    // A dot pattern is a short-on / long-off pattern; classify by ratio so the
    // reverse mapping is stable.
    if (style.dash.on < style.dash.off) return core::DashStyle::Dotted;
    return core::DashStyle::Dashed;
}

const char* primKindName(PrimKind kind) {
    switch (kind) {
        case PrimKind::Segment:   return "Segment";
        case PrimKind::Polyline:  return "Polyline";
        case PrimKind::Polygon:   return "Polygon";
        case PrimKind::Curve:     return "Curve";
        case PrimKind::Text:      return "Text";
        case PrimKind::Arrowhead: return "Arrowhead";
        case PrimKind::Marker:    return "Marker";
        case PrimKind::Rect:      return "Rect";
        case PrimKind::Ellipse:   return "Ellipse";
    }
    return "Unknown";
}

core::Box Scene::bounds() const {
    core::Box b;
    bool any = false;
    for (size_t i = 0; i < prims_.size(); ++i) {
        const core::Box& pb = prims_[i].bounds;
        if (pb.empty() && pb.min == pb.max) continue;
        if (!any) { b = pb; any = true; }
        else b.add(pb.min), b.add(pb.max);
    }
    return b;
}

bool OverlayScene::add(const Primitive& p) {
    if (!isOverlayLayer(p.layer)) return false;
    scene_.add(p);
    ++generation_;
    return true;
}

namespace geom {

// ---------------------------------------------------------------------------
// Small numeric helpers
// ---------------------------------------------------------------------------
static inline bool finite(const core::Vec2& p) { return p.finite(); }
static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline core::Vec2 lerp(const core::Vec2& a, const core::Vec2& b, double t) {
    return core::Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}
static inline core::Vec2 normalized(const core::Vec2& v) {
    const double l = core::length(v);
    return l > 1e-12 ? core::Vec2(v.x / l, v.y / l) : core::Vec2(0.0, 0.0);
}
static inline core::Vec2 perp(const core::Vec2& v) { return core::Vec2(-v.y, v.x); }

// Effective stroke width used everywhere. The view default is the fallback, and
// a non-finite width is treated as the default rather than propagated.
static inline double effectiveWidth(const Style& s, double def) {
    const double w = s.strokeWidth > 0.0 ? s.strokeWidth : def;
    return (std::isfinite(w) && w > 0.0) ? w : 0.6;
}

// ---------------------------------------------------------------------------
// Curve subdivision
// ---------------------------------------------------------------------------
int quadSegments(const core::Vec2& p0, const core::Vec2& c, const core::Vec2& p1,
                 double tolerance, int maxSegments) {
    if (maxSegments < 1) maxSegments = 1;
    if (!(tolerance > 0.0)) tolerance = 0.05;
    // Max deviation of a quadratic from its chord is |p0 - 2c + p1| / 4.
    const core::Vec2 d(p0.x - 2.0 * c.x + p1.x, p0.y - 2.0 * c.y + p1.y);
    const double bend = core::length(d) * 0.25;
    if (!(bend > tolerance)) return 1;
    // For n equal parameter steps the sagitta scales as bend / n^2.
    int n = static_cast<int>(std::ceil(std::sqrt(bend / tolerance)));
    if (n < 1) n = 1;
    return n > maxSegments ? maxSegments : n;
}

int cubicSegments(const core::Vec2& p0, const core::Vec2& c0, const core::Vec2& c1,
                  const core::Vec2& p1, double tolerance, int maxSegments) {
    if (maxSegments < 1) maxSegments = 1;
    if (!(tolerance > 0.0)) tolerance = 0.05;
    // Second-difference bound: |p0 - 3c0 + 3c1 - p1| * 3/4.
    const core::Vec2 d(p0.x - 3.0 * c0.x + 3.0 * c1.x - p1.x,
                       p0.y - 3.0 * c0.y + 3.0 * c1.y - p1.y);
    const double bend = core::length(d) * 0.75;
    if (!(bend > tolerance)) return 1;
    int n = static_cast<int>(std::ceil(std::sqrt(bend / (4.0 * tolerance))));
    if (n < 1) n = 1;
    return n > maxSegments ? maxSegments : n;
}

// ---------------------------------------------------------------------------
// Ellipse / rect helper points used by flattenPath and by the rasterizer
// ---------------------------------------------------------------------------
static void ellipsePoints(const core::Vec2& c, double rx, double ry,
                          double tolerance, std::vector<core::Vec2>* out) {
    if (!(rx > 0.0)) rx = 0.0;
    if (!(ry > 0.0)) ry = 0.0;
    if (rx <= 0.0 && ry <= 0.0) { out->push_back(c); return; }
    const double r = std::max(rx, ry);
    const double tol = (tolerance > 0.0) ? tolerance : 0.05;
    // Chord sagitta r*(1-cos(t/2)) <= tol  ->  t <= 2*acos(1 - tol/r).
    double step = 2.0 * std::acos(clampd(1.0 - tol / r, -1.0, 1.0));
    if (!(step > 1e-6)) step = kPi / 4.0;
    int n = static_cast<int>(std::ceil(2.0 * kPi / step));
    if (n < 8) n = 8;
    if (n > 720) n = 720;
    out->reserve(out->size() + static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * kPi * i / n;
        out->push_back(core::Vec2(c.x + rx * std::cos(t), c.y + ry * std::sin(t)));
    }
}

bool flattenPath(const Primitive& prim, const FlattenOptions& options,
                 std::vector<core::Vec2>* out, bool* closed) {
    if (!out || !closed) return false;
    out->clear();
    *closed = false;
    const double tol = options.tolerance > 0.0 ? options.tolerance : 0.05;
    const int cap = options.maxSegments > 0 ? options.maxSegments : 256;

    switch (prim.kind) {
        case PrimKind::Segment:
            if (prim.points.size() < 2) return false;
            out->push_back(prim.points[0]);
            out->push_back(prim.points[1]);
            return true;
        case PrimKind::Polyline:
        case PrimKind::Polygon: {
            if (prim.points.size() < 2) return false;
            *out = prim.points;
            *closed = (prim.kind == PrimKind::Polygon);
            // Sanitise non-finite points instead of emitting NaN into the batch.
            for (size_t i = 0; i < out->size(); ++i)
                if (!finite((*out)[i])) return false;
            return true;
        }
        case PrimKind::Rect: {
            if (prim.points.size() < 2) return false;
            const core::Vec2 a = prim.points[0], b = prim.points[1];
            out->push_back(core::Vec2(a.x, a.y));
            out->push_back(core::Vec2(b.x, a.y));
            out->push_back(core::Vec2(b.x, b.y));
            out->push_back(core::Vec2(a.x, b.y));
            *closed = true;
            return true;
        }
        case PrimKind::Ellipse: {
            if (prim.points.size() < 1) return false;
            const double rx = prim.points.size() > 1 ? prim.points[1].x : prim.size;
            const double ry = prim.points.size() > 1 ? prim.points[1].y : prim.size;
            ellipsePoints(prim.points[0], rx, ry, tol, out);
            *closed = true;
            return out->size() >= 3;
        }
        case PrimKind::Curve: {
            const std::vector<core::Vec2>& p = prim.points;
            if (p.size() < 2) return false;
            const std::vector<uint8_t>& k = prim.pointKind;
            out->push_back(p[0]);
            size_t i = 1;
            while (i < p.size()) {
                const uint8_t kind = i < k.size() ? k[i] : static_cast<uint8_t>(PointKind::Corner);
                if (kind == static_cast<uint8_t>(PointKind::CubicControl)) {
                    if (i + 2 >= p.size()) break;
                    const core::Vec2 c0 = p[i], c1 = p[i + 1], pe = p[i + 2];
                    const core::Vec2 a = out->back();
                    const int n = cubicSegments(a, c0, c1, pe, tol, cap);
                    for (int s = 1; s <= n; ++s) {
                        const double t = static_cast<double>(s) / n, u = 1.0 - t;
                        out->push_back(core::Vec2(
                            u*u*u*a.x + 3*u*u*t*c0.x + 3*u*t*t*c1.x + t*t*t*pe.x,
                            u*u*u*a.y + 3*u*u*t*c0.y + 3*u*t*t*c1.y + t*t*t*pe.y));
                    }
                    i += 3;
                } else if (kind == static_cast<uint8_t>(PointKind::QuadControl)) {
                    if (i + 1 >= p.size()) break;
                    const core::Vec2 c = p[i], pe = p[i + 1];
                    const core::Vec2 a = out->back();
                    const int n = quadSegments(a, c, pe, tol, cap);
                    for (int s = 1; s <= n; ++s) {
                        const double t = static_cast<double>(s) / n, u = 1.0 - t;
                        out->push_back(core::Vec2(u*u*a.x + 2*u*t*c.x + t*t*pe.x,
                                                  u*u*a.y + 2*u*t*c.y + t*t*pe.y));
                    }
                    i += 2;
                } else {
                    out->push_back(p[i]);
                    i += 1;
                }
            }
            *closed = false;
            return out->size() >= 2;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Dash
// ---------------------------------------------------------------------------
// Arc-length parameterised so the pattern is uniform regardless of vertex
// density: a curve subdivided finely and the same curve subdivided coarsely
// produce the same dashes.
void applyDash(const std::vector<core::Vec2>& path, bool closed,
               const DashPattern& pattern, double width,
               std::vector<std::vector<core::Vec2> >* out) {
    out->clear();
    if (path.size() < 2) return;
    if (pattern.solid()) { out->push_back(path); return; }

    const double w = (std::isfinite(width) && width > 0.0) ? width : 0.6;
    const double onLen  = std::max(pattern.on  * w, 1e-6);
    const double offLen = std::max(pattern.off * w, 1e-6);
    const double period = onLen + offLen;

    std::vector<core::Vec2> pts = path;
    if (closed && pts.size() >= 2 && pts.front() != pts.back()) pts.push_back(pts.front());

    std::vector<core::Vec2> current;
    double travelled = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const core::Vec2 a = pts[i], b = pts[i + 1];
        const double segLen = core::length(core::Vec2(b.x - a.x, b.y - a.y));
        if (!(segLen > 1e-12)) continue;
        double consumed = 0.0;
        int guard = 0;
        while (consumed < segLen && guard++ < 1000000) {
            const double phase = std::fmod(travelled, period);
            const bool on = phase < onLen;
            // The remainder of the CURRENT run. `phase` is measured from the start
            // of the period, so during the off-run it already includes the whole
            // on-length: subtracting onLen first is what makes the walk advance by
            // the true remaining off-length instead of a negative number.
            double remaining = on ? (onLen - phase) : (offLen - (phase - onLen));
            // A boundary landing exactly on a run end yields 0; treat it as a full
            // remaining run so `consumed` never stalls or reverses.
            if (!(remaining > 1e-12)) remaining = on ? onLen : offLen;
            const double step = std::min(remaining, segLen - consumed);
            const double t0 = consumed / segLen;
            const double t1 = (consumed + step) / segLen;
            const core::Vec2 p0 = lerp(a, b, t0);
            const core::Vec2 p1 = lerp(a, b, t1);
            if (on) {
                if (current.empty()) current.push_back(p0);
                current.push_back(p1);
            } else if (!current.empty()) {
                out->push_back(current);
                current.clear();
            }
            consumed += step;
            travelled += step;
            if (step <= 1e-12) break;
        }
    }
    if (!current.empty() && current.size() >= 2) out->push_back(current);
}

// ---------------------------------------------------------------------------
// Wave
// ---------------------------------------------------------------------------
void applyWave(const std::vector<core::Vec2>& path, bool closed,
               double amplitude, double wavelength, double tolerance,
               std::vector<core::Vec2>* out) {
    out->clear();
    if (path.size() < 2 || !(amplitude > 0.0)) { *out = path; return; }
    const double lambda = wavelength > 0.0 ? wavelength : amplitude * 4.0;
    const double tol = tolerance > 0.0 ? tolerance : 0.05;
    // Sample the sine finely enough that the wave itself is smooth: the sagitta
    // bound of a sine with amplitude A over a step h is about A*(pi*h/lambda)^2/2.
    double step = lambda / 8.0;
    if (tol > 0.0 && amplitude > 0.0) {
        const double k = std::sqrt(2.0 * tol / amplitude) * lambda / kPi;
        if (k > 0.0) step = std::min(step, k);
    }
    if (!(step > 1e-6)) step = lambda / 8.0;

    std::vector<core::Vec2> pts = path;
    if (closed && pts.size() >= 2 && pts.front() != pts.back()) pts.push_back(pts.front());

    double travelled = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const core::Vec2 a = pts[i], b = pts[i + 1];
        const core::Vec2 d(b.x - a.x, b.y - a.y);
        const double segLen = core::length(d);
        if (!(segLen > 1e-12)) continue;
        const core::Vec2 dir(d.x / segLen, d.y / segLen);
        const core::Vec2 n = perp(dir);
        if (out->empty()) out->push_back(a);
        const int nSteps = std::max(1, static_cast<int>(std::ceil(segLen / step)));
        for (int s = 1; s <= nSteps; ++s) {
            const double t = static_cast<double>(s) / nSteps;
            const double along = travelled + segLen * t;
            const double off = amplitude * std::sin(2.0 * kPi * along / lambda);
            const core::Vec2 base = lerp(a, b, t);
            out->push_back(core::Vec2(base.x + n.x * off, base.y + n.y * off));
        }
        travelled += segLen;
    }
}

} // namespace geom
} // namespace render
} // namespace r62
