#include "next/graphics/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace r62 {
namespace graphics {
namespace {

const double kPi = 3.14159265358979323846;

// [PROJ] how many segments a full ellipse is approximated with. 96 half-degree
// steps keeps the maximum sagitta of a 24pt-radius ellipse well under 0.01pt,
// which is below the rasterizer grid at any zoom the editor allows.
const int kEllipseSegments = 96;
// [PROJ] segments per quadratic brace curve.
const int kBraceSegments = 8;

double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

double deg2rad(double d) { return d * kPi / 180.0; }

// Signed area, used to normalise an outline direction for the fill rule.
double signedArea(const std::vector<core::Vec2>& p) {
    double a = 0.0;
    const size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
        const core::Vec2& u = p[i];
        const core::Vec2& v = p[(i + 1) % n];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5;
}

void add(int v, std::ostringstream& o, const char* k) { o << k << "=" << v << ";"; }
void addd(double v, std::ostringstream& o, const char* k) {
    o.precision(17);
    o << k << "=" << v << ";";
}

bool parseInt(const std::string& s, int* out) {
    if (s.empty()) return false;
    char* end = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != 0) return false;
    *out = static_cast<int>(v);
    return true;
}
bool parseDouble(const std::string& s, double* out) {
    if (s.empty()) return false;
    char* end = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end != 0) return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

} // namespace

const char* const kShapeParamsKey   = "gfx.shape.params.1";
const char* const kBracketParamsKey = "gfx.bracket.params.1";
const char* const kTableParamsKey   = "gfx.table.params.1";
const char* const kEmbedKeyPrefix   = "gfx.embed.";

// ===========================================================================
// Stroke / style helpers
// ===========================================================================
StrokeSpec strokeSpecFor(const ShapeParams& p, double documentLineWidth) {
    StrokeSpec s;
    s.width = p.style.width > 0.0 ? p.style.width : documentLineWidth;
    if (s.width <= 0.0) s.width = 0.6;
    s.dashed = appearanceDashed(p.appearance) || p.line == LineVariant::Dashed;
    s.wavy   = (p.form == ShapeForm::Line || p.form == ShapeForm::Arc) &&
               p.line == LineVariant::Wavy;
    if (p.line == LineVariant::Bold) s.width *= 2.5;
    s.wavyAmplitude = s.wavy ? s.width * 2.0 : 0.0;
    return s;
}

StrokeSpec strokeSpecFor(const BracketParams& p, double documentLineWidth) {
    StrokeSpec s;
    s.width = p.style.width > 0.0 ? p.style.width : documentLineWidth;
    if (s.width <= 0.0) s.width = 0.6;
    return s;
}

core::Vec2 shadowOffsetFor(const ShapeFrame& frame) {
    const double w = frame.halfWidth(), h = frame.halfHeight();
    const double base = std::min(w, h);
    const double d = std::max(1.0, base * 0.18);
    return core::Vec2(d, d);
}

double shadowBlurFor(const ShapeFrame& frame) {
    const double base = std::min(frame.halfWidth(), frame.halfHeight());
    return std::max(0.5, base * 0.10);
}

// ===========================================================================
// Small helpers
// ===========================================================================
bool pointInBox(const core::Box& b, const core::Vec2& p) {
    return p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y;
}

core::Box inflateBox(const core::Box& b, double amount) {
    return core::Box(core::Vec2(b.min.x - amount, b.min.y - amount),
                     core::Vec2(b.max.x + amount, b.max.y + amount));
}

double distancePointSegment(const core::Vec2& p, const core::Vec2& a, const core::Vec2& b) {
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double wx = p.x - a.x, wy = p.y - a.y;
    const double len2 = vx * vx + vy * vy;
    if (len2 <= 1e-18) return std::sqrt(wx * wx + wy * wy);
    double t = (wx * vx + wy * vy) / len2;
    t = clampd(t, 0.0, 1.0);
    const double dx = wx - t * vx, dy = wy - t * vy;
    return std::sqrt(dx * dx + dy * dy);
}

bool pointInPolygon(const std::vector<core::Vec2>& poly, const core::Vec2& p) {
    const size_t n = poly.size();
    if (n < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const core::Vec2& a = poly[i];
        const core::Vec2& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

std::vector<core::Vec2> flattenQuadChain(const Contour& c, int segmentsPerCurve) {
    std::vector<core::Vec2> out;
    if (c.points.size() < 2) { out = c.points; return out; }
    if (c.kind.size() != c.points.size()) { out = c.points; return out; }
    out.push_back(c.points[0]);
    size_t i = 1;
    while (i < c.points.size()) {
        const bool control = c.kind[i] == 1;
        if (control && i + 1 < c.points.size()) {
            const core::Vec2 a = out.back();
            const core::Vec2 ctrl = c.points[i];
            const core::Vec2 b = c.points[i + 1];
            const int seg = segmentsPerCurve < 2 ? 2 : segmentsPerCurve;
            for (int s = 1; s <= seg; ++s) {
                const double t = static_cast<double>(s) / static_cast<double>(seg);
                const double mt = 1.0 - t;
                out.push_back(core::Vec2(mt * mt * a.x + 2 * mt * t * ctrl.x + t * t * b.x,
                                         mt * mt * a.y + 2 * mt * t * ctrl.y + t * t * b.y));
            }
            i += 2;
        } else {
            out.push_back(c.points[i]);
            ++i;
        }
    }
    return out;
}

// ===========================================================================
// Outline construction
// ===========================================================================
namespace {

// Dense polyline of a closed form. `segments` controls the ellipse/roundrect
// smoothness; 0 means "use the default".
std::vector<core::Vec2> closedFormOutline(const ShapeParams& p, int segments) {
    std::vector<core::Vec2> out;
    const ShapeFrame& f = p.frame;
    const int seg = segments > 0 ? segments : kEllipseSegments;
    switch (p.form) {
    case ShapeForm::Rectangle: {
        core::Vec2 c[4];
        f.corners(c);
        for (int i = 0; i < 4; ++i) out.push_back(c[i]);
        break;
    }
    case ShapeForm::RoundRectangle: {
        const double r = clampd(p.cornerFraction, 0.0, 1.0) *
                         std::min(f.halfWidth(), f.halfHeight());
        if (r <= 1e-9) {
            core::Vec2 c[4];
            f.corners(c);
            for (int i = 0; i < 4; ++i) out.push_back(c[i]);
            break;
        }
        // Corner centres in the unit square, then a quarter arc around each.
        // Walk order matches ShapeFrame::corners (TL, TR, BR, BL).
        const double ux = f.halfWidth() > 0.0 ? r / f.halfWidth() : 0.0;
        const double uy = f.halfHeight() > 0.0 ? r / f.halfHeight() : 0.0;
        const double cu[4] = { -1.0 + ux,  1.0 - ux,  1.0 - ux, -1.0 + ux };
        const double cv[4] = { -1.0 + uy, -1.0 + uy,  1.0 - uy,  1.0 - uy };
        const double start[4] = { 180.0, 270.0, 0.0, 90.0 };
        const int perCorner = 6;
        for (int k = 0; k < 4; ++k) {
            for (int s = 0; s <= perCorner; ++s) {
                const double a = deg2rad(start[k] + 90.0 * s / perCorner);
                const double u = cu[k] + ux * std::cos(a);
                // v=-1 is the TOP edge. At 270 degrees the top-left/top-right
                // arcs must reach v=-1, so v advances with +sin, not -sin.
                const double v = cv[k] + uy * std::sin(a);
                out.push_back(f.pointAt(u, v));
            }
        }
        break;
    }
    case ShapeForm::Circle:
    case ShapeForm::Oval: {
        for (int i = 0; i < seg; ++i) {
            const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(seg);
            // core::direction() convention (y down), applied through the frame.
            out.push_back(f.pointAt(std::cos(a), -std::sin(a)));
        }
        break;
    }
    default:
        break;
    }
    return out;
}

// Point on the frame ellipse at a parametric angle, using core::direction()
// convention so a positive angle sweeps visually counter-clockwise.
core::Vec2 ellipsePoint(const ShapeFrame& f, double degrees) {
    const core::Vec2 d = core::direction(degrees);
    return f.pointAt(d.x, d.y);
}

std::vector<core::Vec2> arcOutline(const ShapeParams& p) {
    std::vector<core::Vec2> out;
    double span = p.arcSpanDeg;
    if (std::fabs(span) < 1e-9) {
        span = static_cast<double>(p.arcSweepDeg > 0 ? p.arcSweepDeg : 90);
    }
    // Keep the sweep inside one turn; a full turn is a closed ellipse.
    span = clampd(span, -359.999, 359.999);
    const int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / 3.0)));
    for (int i = 0; i <= steps; ++i) {
        const double a = p.arcStartDeg + span * static_cast<double>(i) / steps;
        out.push_back(ellipsePoint(p.frame, a));
    }
    return out;
}

// A wavy rendering of a polyline. Only used for Line_Wavy; the amplitude comes
// from the stroke spec so the hit test and the renderer agree.
std::vector<core::Vec2> wavePolyline(const std::vector<core::Vec2>& in, double amplitude,
                                     double wavelength) {
    std::vector<core::Vec2> out;
    if (in.size() < 2) return in;
    if (amplitude <= 0.0) return in;
    const double wl = wavelength > 0.0 ? wavelength : amplitude * 4.0;
    for (size_t seg = 0; seg + 1 < in.size(); ++seg) {
        const core::Vec2 a = in[seg], b = in[seg + 1];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len <= 1e-12) continue;
        const double nx = -dy / len, ny = dx / len;
        const int steps = std::max(2, static_cast<int>(std::ceil(len / (wl / 8.0))));
        if (seg == 0) out.push_back(a);
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const double s = t * len;
            const double off = amplitude * std::sin(2.0 * kPi * s / wl);
            out.push_back(core::Vec2(a.x + dx * t + nx * off, a.y + dy * t + ny * off));
        }
    }
    return out;
}

void appendContourAsStroke(std::vector<Contour>& out, const std::vector<core::Vec2>& pts,
                           bool closed) {
    if (pts.size() < 2) return;
    Contour c;
    c.points = pts;
    c.kind.assign(pts.size(), 0);
    c.closed = closed;
    out.push_back(c);
}

} // namespace

std::vector<core::Vec2> shapeOutline(const ShapeParams& p) {
    if (p.form == ShapeForm::Line) {
        // A line is NOT a rectangle with four corners: it has exactly two ends,
        // centre - axisU and centre + axisU. Reading the ends off the frame's
        // diagonal instead would fold axisV into them, so every line would start
        // and end one perpendicular away from where the user dragged - and, before
        // lineFrame() stored a UNIT axisV, that offset was the stroke half-width.
        // axisU is the half-displacement, so these are exactly begin and current
        // for either drag direction and for any stroke width.
        std::vector<core::Vec2> line;
        line.push_back(core::Vec2(p.frame.centre.x - p.frame.axisU.x,
                                  p.frame.centre.y - p.frame.axisU.y));
        line.push_back(core::Vec2(p.frame.centre.x + p.frame.axisU.x,
                                  p.frame.centre.y + p.frame.axisU.y));
        return line;
    }
    if (p.form == ShapeForm::Arc) return arcOutline(p);
    return closedFormOutline(p, 0);
}

// ===========================================================================
// Shape geometry
// ===========================================================================
namespace {

core::Error buildShapeGeometryImpl(const ShapeParams& p, const StrokeSpec& stroke,
                                   ShapeGeometry* out) {
    if (!p.frame.centre.finite() || !p.frame.axisU.finite() || !p.frame.axisV.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "Shape frame must be finite");
    if (!(p.scale > 0.0))
        return core::Error(core::ErrorCode::InvalidGeometry, "Shape scale must be positive");

    out->params = p;
    out->stroke = stroke;
    out->hasFill = p.hasInterior();
    out->hasShadow = appearanceShadowed(p.appearance);

    std::vector<core::Vec2> outline = shapeOutline(p);
    if (outline.empty())
        return core::Error(core::ErrorCode::InvalidGeometry, "Shape produced no outline");

    // ---- fill ----
    if (out->hasFill) {
        Contour fill;
        fill.points = outline;
        fill.kind.assign(outline.size(), 0);
        fill.closed = true;
        // A clockwise outline (positive signed area in y-down space) lets T03
        // use NonZero and EvenOdd interchangeably for a simple contour.
        if (signedArea(fill.points) < 0.0)
            std::reverse(fill.points.begin(), fill.points.end());
        out->fills.push_back(fill);
    }

    // ---- stroke ----
    std::vector<core::Vec2> stroked = outline;
    if (stroke.wavy)
        stroked = wavePolyline(outline, stroke.wavyAmplitude, stroke.wavyAmplitude * 4.0);
    const bool closedStroke = (p.form != ShapeForm::Line && p.form != ShapeForm::Arc);
    appendContourAsStroke(out->strokes, stroked, closedStroke);

    // ---- shadow ----
    if (out->hasShadow) {
        const core::Vec2 off = shadowOffsetFor(p.frame);
        Contour sh;
        for (size_t i = 0; i < stroked.size(); ++i)
            sh.points.push_back(core::Vec2(stroked[i].x + off.x, stroked[i].y + off.y));
        sh.kind.assign(sh.points.size(), 0);
        sh.closed = closedStroke;
        out->shadows.push_back(sh);
    }

    // ---- bounds ----
    out->fillBounds = p.frame.aabb();
    if (p.form == ShapeForm::Arc || p.form == ShapeForm::Line) {
        core::Box b(stroked[0], stroked[0]);
        for (size_t i = 1; i < stroked.size(); ++i) b.add(stroked[i]);
        out->fillBounds = b;
    }
    out->hitBounds = inflateBox(out->fillBounds, stroke.width * 0.5);
    if (stroke.wavyAmplitude > 0.0)
        out->hitBounds = inflateBox(out->hitBounds, stroke.wavyAmplitude);
    if (out->hasShadow) {
        const core::Vec2 off = shadowOffsetFor(p.frame);
        const double blur = shadowBlurFor(p.frame);
        core::Box sh = out->fillBounds;
        sh.min.x += off.x; sh.min.y += off.y;
        sh.max.x += off.x; sh.max.y += off.y;
        sh = inflateBox(sh, stroke.width * 0.5 + blur);
        out->hitBounds.add(sh.min);
        out->hitBounds.add(sh.max);
    }
    return core::Error::success();
}

} // namespace

core::Error buildShapeGeometry(const ShapeParams& p, const StrokeSpec& stroke,
                               ShapeGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    *out = ShapeGeometry();
    return buildShapeGeometryImpl(p, stroke, out);
}

core::Box shapeBounds(const ShapeParams& p, const StrokeSpec& stroke) {
    ShapeGeometry g;
    if (!buildShapeGeometry(p, stroke, &g).ok())
        return p.frame.aabb();
    return g.hitBounds;
}

// ===========================================================================
// Hit test
// ===========================================================================
namespace {

bool nearPolyline(const std::vector<core::Vec2>& pts, bool closed, const core::Vec2& p,
                  double tol) {
    if (pts.empty()) return false;
    if (pts.size() == 1) return core::length(pts[0] - p) <= tol;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        if (distancePointSegment(p, pts[i], pts[i + 1]) <= tol) return true;
    if (closed && distancePointSegment(p, pts.back(), pts.front()) <= tol) return true;
    return false;
}

} // namespace

bool shapeHitTest(const ShapeParams& p, const core::Vec2& point, double tolerance) {
    if (tolerance < 0.0) tolerance = 0.0;
    const StrokeSpec stroke = strokeSpecFor(p, 0.6);
    const double band = tolerance + stroke.width * 0.5;

    std::vector<core::Vec2> outline = shapeOutline(p);
    if (outline.empty()) return false;

    // 1. Filled interior. This is the deliberate difference between a hollow and
    //    a filled shape.
    if (p.hasInterior() || (appearanceFills(p.appearance) && p.form != ShapeForm::Line &&
                            p.form != ShapeForm::Arc)) {
        if (pointInPolygon(outline, point)) return true;
    }

    // 2. The stroke band. Dash-aware testing is NOT implemented in v1: a dashed
    //    stroke hits anywhere the solid one would. Documented, not hidden.
    std::vector<core::Vec2> stroked = outline;
    if (stroke.wavy)
        stroked = wavePolyline(outline, stroke.wavyAmplitude, stroke.wavyAmplitude * 4.0);
    const bool closedStroke = (p.form != ShapeForm::Line && p.form != ShapeForm::Arc);
    if (nearPolyline(stroked, closedStroke, point, band)) return true;

    // 3. The shadow region of a Shadowed shape (offset copy, filled + stroked).
    if (appearanceShadowed(p.appearance)) {
        const core::Vec2 off = shadowOffsetFor(p.frame);
        std::vector<core::Vec2> sh;
        sh.reserve(outline.size());
        for (size_t i = 0; i < outline.size(); ++i)
            sh.push_back(core::Vec2(outline[i].x + off.x, outline[i].y + off.y));
        const double blur = shadowBlurFor(p.frame);
        if (closedStroke && pointInPolygon(sh, point)) return true;
        if (nearPolyline(sh, closedStroke, point, band + blur)) return true;
    }
    return false;
}

// ===========================================================================
// Brackets
// ===========================================================================
core::Vec2 bracketLabelPosition(const BracketParams& p, const BracketLabelAnchor& a) {
    return p.frame.pointAt(clampd(a.u, -2.0, 2.0), clampd(a.v, -2.0, 2.0));
}

namespace {

// The rectangular span of the bracket in its own frame: u is the "opening" axis,
// v the "span" axis. A vertical bracket spans v in [-1,1] at u = -1 (left side).
void squareSide(const ShapeFrame& f, double u, Contour* out) {
    Contour c;
    c.closed = false;
    c.points.push_back(f.pointAt(u, -1.0));
    c.points.push_back(f.pointAt(u,  0.0));
    c.points.push_back(f.pointAt(u,  1.0));
    // Tick marks: the short horizontal strokes that make it a SQUARE bracket
    // rather than a bare vertical line.
    const double tick = 0.30;
    c.points.push_back(f.pointAt(u + (u < 0.0 ? tick : -tick), 1.0));
    c.points.push_back(f.pointAt(u, 1.0));
    c.points.push_back(f.pointAt(u, -1.0));
    c.points.push_back(f.pointAt(u + (u < 0.0 ? tick : -tick), -1.0));
    c.kind.assign(c.points.size(), 0);
    out->points = c.points;
    out->kind = c.kind;
    out->closed = false;
}

// A parenthesis is a quadratic bulge from (u,-1) to (u,1) through a control
// point pushed outward by `bulge` on the u axis.
void parenthesisSide(const ShapeFrame& f, double u, Contour* out) {
    const double bulge = 0.55;
    Contour c;
    c.closed = false;
    c.points.push_back(f.pointAt(u, -1.0));
    c.points.push_back(f.pointAt(u + (u < 0.0 ? -bulge : bulge), 0.0));
    c.points.push_back(f.pointAt(u, 1.0));
    c.kind.push_back(0);
    c.kind.push_back(1);   // quad control
    c.kind.push_back(0);
    out->points = c.points;
    out->kind = c.kind;
    out->closed = false;
}

// A curly brace: two mirrored halves, each a quad chain with a cusp at the
// middle. The cusp points AWAY from the enclosed content for a left brace.
void curlySide(const ShapeFrame& f, double u, Contour* out) {
    const double mid = u + (u < 0.0 ? -0.45 : 0.45);
    const double shoulder = u * 0.55;   // between the outer edge and the cusp
    Contour c;
    c.closed = false;
    c.points.push_back(f.pointAt(u, -1.0));
    c.points.push_back(f.pointAt(shoulder, -0.75));
    c.points.push_back(f.pointAt(shoulder, -0.25));
    c.points.push_back(f.pointAt(mid, -0.08));
    c.points.push_back(f.pointAt(mid, 0.0));
    c.points.push_back(f.pointAt(mid, 0.08));
    c.points.push_back(f.pointAt(shoulder, 0.25));
    c.points.push_back(f.pointAt(shoulder, 0.75));
    c.points.push_back(f.pointAt(u, 1.0));
    c.kind.push_back(0);
    c.kind.push_back(1); c.kind.push_back(0);
    c.kind.push_back(1); c.kind.push_back(0);
    c.kind.push_back(1); c.kind.push_back(0);
    c.kind.push_back(1); c.kind.push_back(0);
    out->points = c.points;
    out->kind = c.kind;
    out->closed = false;
}

// Single dagger / double dagger. A dagger is drawn at the CENTRE of the frame
// spanning v, with a cross bar; the double dagger has two bars.
void daggerMark(const ShapeFrame& f, bool doubleBar, Contour* out) {
    Contour c;
    c.closed = false;
    c.points.push_back(f.pointAt(0.0, -1.0));
    c.points.push_back(f.pointAt(0.0,  1.0));
    const double bar = 0.45;
    const double y1 = doubleBar ? -0.28 : -0.10;
    const double y2 = doubleBar ?  0.28 :  0.10;
    // Pen never lifts: the bars are appended as separate strokes by the caller.
    c.kind.assign(c.points.size(), 0);
    out->points = c.points;
    out->kind = c.kind;
    out->closed = false;
    // Encode the bars as extra points with a marker kind 2 (bar start). The
    // renderer bridge expands kind 2 into a separate polyline; flattenQuadChain
    // treats them as corners, which is also correct for hit testing.
    c.points.push_back(f.pointAt(-bar, y1));
    c.points.push_back(f.pointAt( bar, y1));
    c.kind.push_back(2);
    c.kind.push_back(2);
    if (doubleBar) {
        c.points.push_back(f.pointAt(-bar, y2));
        c.points.push_back(f.pointAt( bar, y2));
        c.kind.push_back(2);
        c.kind.push_back(2);
    }
    out->points = c.points;
    out->kind = c.kind;
}

void appendSide(const BracketParams& p, double u, std::vector<Contour>* out) {
    Contour c;
    switch (p.family) {
    case BracketFamily::Square:      squareSide(p.frame, u, &c); break;
    case BracketFamily::Parenthesis: parenthesisSide(p.frame, u, &c); break;
    case BracketFamily::Curly:       curlySide(p.frame, u, &c); break;
    case BracketFamily::Dagger:      daggerMark(p.frame, false, &c); break;
    }
    if (!c.points.empty()) out->push_back(c);
}

core::Error buildBracketGeometryImpl(const BracketParams& p, const StrokeSpec& stroke,
                                     double labelTextSize, BracketGeometry* out) {
    if (!p.frame.centre.finite() || !p.frame.axisU.finite() || !p.frame.axisV.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "Bracket frame must be finite");
    out->params = p;
    out->stroke = stroke;

    // The frame's u axis is the opening axis. Horizontal brackets swap the axes
    // so the same code draws a rotated pair. This is a rotation of the FRAME,
    // not a special case in the shape routines.
    ShapeFrame f = p.frame;
    BracketParams local = p;
    if (p.direction == BracketDirection::Horizontal) {
        f = ShapeFrame(p.frame.centre, p.frame.axisV, p.frame.axisU);
        local.frame = f;
    }

    if (p.family == BracketFamily::Dagger) {
        // A dagger is never paired and never sits on a side.
        Contour c;
        daggerMark(f, p.presetId == 3u /* DoubleDagger */, &c);
        out->runs.push_back(c);
    } else if (p.side == BracketSide::Pair) {
        appendSide(local, -1.0, &out->runs);
        appendSide(local,  1.0, &out->runs);
    } else if (p.side == BracketSide::Left) {
        appendSide(local, -1.0, &out->runs);
    } else {
        appendSide(local,  1.0, &out->runs);
    }

    if (out->runs.empty())
        return core::Error(core::ErrorCode::InvalidGeometry, "Bracket produced no geometry");

    // Bounds from the flattened runs (a curly brace bulges outside the frame).
    core::Box b;
    bool first = true;
    for (size_t i = 0; i < out->runs.size(); ++i) {
        const std::vector<core::Vec2> flat = flattenQuadChain(out->runs[i], kBraceSegments);
        for (size_t k = 0; k < flat.size(); ++k) {
            if (first) { b = core::Box(flat[k], flat[k]); first = false; }
            else b.add(flat[k]);
        }
    }
    if (first) b = p.frame.aabb();
    out->hitBounds = inflateBox(b, stroke.width * 0.5);

    // Label positions, recomputed from the CURRENT frame every time.
    out->labelPositions.clear();
    for (size_t i = 0; i < p.labels.size(); ++i) {
        const core::Vec2 pos = bracketLabelPosition(local, p.labels[i].anchor);
        out->labelPositions.push_back(pos);
        const double size = p.labels[i].size > 0.0 ? p.labels[i].size : labelTextSize;
        const double half = std::max(size, 1.0);
        const core::Box lb(core::Vec2(pos.x - half, pos.y - half),
                           core::Vec2(pos.x + half, pos.y + half));
        out->hitBounds.add(lb.min);
        out->hitBounds.add(lb.max);
    }
    return core::Error::success();
}

} // namespace

core::Error buildBracketGeometry(const BracketParams& p, const StrokeSpec& stroke,
                                 double labelTextSize, BracketGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    *out = BracketGeometry();
    return buildBracketGeometryImpl(p, stroke, labelTextSize, out);
}

core::Box bracketBounds(const BracketParams& p, const StrokeSpec& stroke,
                        double labelTextSize) {
    BracketGeometry g;
    if (!buildBracketGeometry(p, stroke, labelTextSize, &g).ok())
        return p.frame.aabb();
    return g.hitBounds;
}

bool bracketHitTest(const BracketParams& p, const core::Vec2& point, double tolerance,
                    double labelTextSize) {
    if (tolerance < 0.0) tolerance = 0.0;
    const StrokeSpec stroke = strokeSpecFor(p, 0.6);
    const double band = tolerance + stroke.width * 0.5;
    BracketGeometry g;
    if (!buildBracketGeometry(p, stroke, labelTextSize, &g).ok()) return false;
    for (size_t i = 0; i < g.runs.size(); ++i) {
        const Contour& c = g.runs[i];
        const std::vector<core::Vec2> flat = flattenQuadChain(c, kBraceSegments);
        if (nearPolyline(flat, false, point, band)) return true;
        if (p.family == BracketFamily::Dagger) {
            // Dagger bars are encoded as kind==2 markers: pairs of points.
            for (size_t k = 0; k + 1 < c.points.size(); ++k) {
                if (c.kind[k] == 2 && c.kind[k + 1] == 2) {
                    if (distancePointSegment(point, c.points[k], c.points[k + 1]) <= band)
                        return true;
                    ++k;
                }
            }
        }
    }
    // A label is part of the object: clicking it selects the bracket.
    for (size_t i = 0; i < g.labelPositions.size(); ++i) {
        const double size = p.labels[i].size > 0.0 ? p.labels[i].size : labelTextSize;
        const double half = std::max(size, 1.0) * 0.6;
        if (std::fabs(point.x - g.labelPositions[i].x) <= half + tolerance &&
            std::fabs(point.y - g.labelPositions[i].y) <= half + tolerance)
            return true;
    }
    return false;
}

// ===========================================================================
// Encoding
// ===========================================================================
std::string encodeShapeParams(const ShapeParams& p) {
    std::ostringstream o;
    add(static_cast<int>(p.presetId), o, "preset");
    add(static_cast<int>(p.form), o, "form");
    add(static_cast<int>(p.appearance), o, "app");
    add(static_cast<int>(p.line), o, "line");
    add(p.arcSweepDeg, o, "sweep");
    addd(p.frame.centre.x, o, "cx");
    addd(p.frame.centre.y, o, "cy");
    addd(p.frame.axisU.x, o, "ux");
    addd(p.frame.axisU.y, o, "uy");
    addd(p.frame.axisV.x, o, "vx");
    addd(p.frame.axisV.y, o, "vy");
    addd(p.cornerFraction, o, "corner");
    addd(p.arcStartDeg, o, "astart");
    addd(p.arcSpanDeg, o, "aspan");
    addd(p.scale, o, "scale");
    addd(p.style.width, o, "lw");
    add(static_cast<int>(p.style.dash), o, "dash");
    {
        std::ostringstream c;
        c << "0x" << std::hex << p.style.colorRGBA << std::dec;
        o << "linecolor=" << c.str() << ";";
    }
    {
        std::ostringstream c;
        c << "0x" << std::hex << p.fillRGBA << std::dec;
        o << "fill=" << c.str() << ";";
    }
    return o.str();
}

ShapeParams decodeShapeParams(const std::string& text, bool* ok) {
    ShapeParams out;
    bool good = true;
    int seen = 0;
    const int kRequired = 19;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t semi = text.find(';', pos);
        const std::string tok = text.substr(pos, semi == std::string::npos ? std::string::npos
                                                                    : semi - pos);
        pos = (semi == std::string::npos) ? text.size() : semi + 1;
        if (tok.empty()) continue;
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) { good = false; break; }
        const std::string k = tok.substr(0, eq);
        const std::string v = tok.substr(eq + 1);
        int iv = 0;
        double dv = 0.0;
        unsigned int uv = 0;
        bool handled = true;
        if (k == "preset") { if (!parseInt(v, &iv)) { good = false; break; } out.presetId = static_cast<uint32_t>(iv); }
        else if (k == "form") { if (!parseInt(v, &iv)) { good = false; break; } out.form = static_cast<ShapeForm>(iv); }
        else if (k == "app") { if (!parseInt(v, &iv)) { good = false; break; } out.appearance = static_cast<ShapeAppearance>(iv); }
        else if (k == "line") { if (!parseInt(v, &iv)) { good = false; break; } out.line = static_cast<LineVariant>(iv); }
        else if (k == "sweep") { if (!parseInt(v, &iv)) { good = false; break; } out.arcSweepDeg = iv; }
        else if (k == "cx") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.centre.x = dv; }
        else if (k == "cy") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.centre.y = dv; }
        else if (k == "ux") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisU.x = dv; }
        else if (k == "uy") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisU.y = dv; }
        else if (k == "vx") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisV.x = dv; }
        else if (k == "vy") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisV.y = dv; }
        else if (k == "corner") { if (!parseDouble(v, &dv)) { good = false; break; } out.cornerFraction = dv; }
        else if (k == "astart") { if (!parseDouble(v, &dv)) { good = false; break; } out.arcStartDeg = dv; }
        else if (k == "aspan") { if (!parseDouble(v, &dv)) { good = false; break; } out.arcSpanDeg = dv; }
        else if (k == "scale") { if (!parseDouble(v, &dv)) { good = false; break; } out.scale = dv; }
        else if (k == "lw") { if (!parseDouble(v, &dv)) { good = false; break; } out.style.width = dv; }
        else if (k == "dash") { if (!parseInt(v, &iv)) { good = false; break; } out.style.dash = static_cast<uint8_t>(iv); }
        else if (k == "linecolor") { if (std::sscanf(v.c_str(), "0x%x", &uv) != 1) { good = false; break; } out.style.colorRGBA = uv; }
        else if (k == "fill") { if (std::sscanf(v.c_str(), "0x%x", &uv) != 1) { good = false; break; } out.fillRGBA = uv; }
        else handled = false;   // unknown key: ignored, not an error
        if (handled) ++seen;
    }
    if (seen < kRequired) good = false;
    if (ok) *ok = good;
    if (!good) return ShapeParams();
    return out;
}

std::string encodeBracketParams(const BracketParams& p) {
    std::ostringstream o;
    add(static_cast<int>(p.presetId), o, "preset");
    add(static_cast<int>(p.family), o, "family");
    add(static_cast<int>(p.side), o, "side");
    add(static_cast<int>(p.direction), o, "dir");
    addd(p.frame.centre.x, o, "cx");
    addd(p.frame.centre.y, o, "cy");
    addd(p.frame.axisU.x, o, "ux");
    addd(p.frame.axisU.y, o, "uy");
    addd(p.frame.axisV.x, o, "vx");
    addd(p.frame.axisV.y, o, "vy");
    addd(p.scale, o, "scale");
    addd(p.style.width, o, "lw");
    add(static_cast<int>(p.style.dash), o, "dash");
    {
        std::ostringstream c;
        c << "0x" << std::hex << p.style.colorRGBA << std::dec;
        o << "linecolor=" << c.str() << ";";
    }
    add(static_cast<int>(p.labels.size()), o, "labels");
    for (size_t i = 0; i < p.labels.size(); ++i) {
        std::ostringstream pre;
        pre << "L" << i << "_";
        addd(p.labels[i].anchor.u, o, (pre.str() + "u").c_str());
        addd(p.labels[i].anchor.v, o, (pre.str() + "v").c_str());
        addd(p.labels[i].size, o, (pre.str() + "size").c_str());
        // The text is the remainder of the token; ';' cannot appear because the
        // writer escapes nothing and the tokenizer splits on ';'. A semicolon in
        // a label is therefore rejected at the model layer (see model.cpp).
        o << pre.str() << "text=" << p.labels[i].text << ";";
    }
    return o.str();
}

BracketParams decodeBracketParams(const std::string& text, bool* ok) {
    BracketParams out;
    bool good = true;
    int seen = 0;
    const int kRequired = 15;
    int labelCount = -1;
    struct RawLabel { double u, v, size; std::string text; };
    std::vector<RawLabel> raw;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t semi = text.find(';', pos);
        const std::string tok = text.substr(pos, semi == std::string::npos ? std::string::npos
                                                                    : semi - pos);
        pos = (semi == std::string::npos) ? text.size() : semi + 1;
        if (tok.empty()) continue;
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) { good = false; break; }
        const std::string k = tok.substr(0, eq);
        const std::string v = tok.substr(eq + 1);
        int iv = 0;
        double dv = 0.0;
        unsigned int uv = 0;
        bool handled = true;
        if (k == "preset") { if (!parseInt(v, &iv)) { good = false; break; } out.presetId = static_cast<uint32_t>(iv); }
        else if (k == "family") { if (!parseInt(v, &iv)) { good = false; break; } out.family = static_cast<BracketFamily>(iv); }
        else if (k == "side") { if (!parseInt(v, &iv)) { good = false; break; } out.side = static_cast<BracketSide>(iv); }
        else if (k == "dir") { if (!parseInt(v, &iv)) { good = false; break; } out.direction = static_cast<BracketDirection>(iv); }
        else if (k == "cx") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.centre.x = dv; }
        else if (k == "cy") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.centre.y = dv; }
        else if (k == "ux") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisU.x = dv; }
        else if (k == "uy") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisU.y = dv; }
        else if (k == "vx") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisV.x = dv; }
        else if (k == "vy") { if (!parseDouble(v, &dv)) { good = false; break; } out.frame.axisV.y = dv; }
        else if (k == "scale") { if (!parseDouble(v, &dv)) { good = false; break; } out.scale = dv; }
        else if (k == "lw") { if (!parseDouble(v, &dv)) { good = false; break; } out.style.width = dv; }
        else if (k == "dash") { if (!parseInt(v, &iv)) { good = false; break; } out.style.dash = static_cast<uint8_t>(iv); }
        else if (k == "linecolor") { if (std::sscanf(v.c_str(), "0x%x", &uv) != 1) { good = false; break; } out.style.colorRGBA = uv; }
        else if (k == "labels") { if (!parseInt(v, &iv) || iv < 0 || iv > 64) { good = false; break; } labelCount = iv; raw.resize(static_cast<size_t>(iv)); }
        else if (k.size() > 3 && k[0] == 'L') {
            const size_t us = k.find('_');
            if (us == std::string::npos) { handled = false; }
            else {
                int idx = -1;
                if (!parseInt(k.substr(1, us - 1), &idx) || idx < 0 ||
                    static_cast<size_t>(idx) >= raw.size()) { good = false; break; }
                const std::string field = k.substr(us + 1);
                RawLabel& rl = raw[static_cast<size_t>(idx)];
                if (field == "u") { if (!parseDouble(v, &dv)) { good = false; break; } rl.u = dv; }
                else if (field == "v") { if (!parseDouble(v, &dv)) { good = false; break; } rl.v = dv; }
                else if (field == "size") { if (!parseDouble(v, &dv)) { good = false; break; } rl.size = dv; }
                else if (field == "text") { rl.text = v; }
                else handled = false;
            }
        }
        else handled = false;
        if (handled) ++seen;
    }
    if (labelCount < 0) good = false;
    if (seen < kRequired) good = false;
    if (ok) *ok = good;
    if (!good) return BracketParams();
    out.labels.clear();
    for (size_t i = 0; i < raw.size(); ++i) {
        BracketLabel l;
        l.anchor = BracketLabelAnchor(raw[i].u, raw[i].v);
        l.size = raw[i].size;
        l.text = raw[i].text;
        out.labels.push_back(l);
    }
    return out;
}

// ===========================================================================
// Signatures
// ===========================================================================
namespace {
uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}
uint64_t bits(double d) {
    uint64_t u = 0;
    static_assert(sizeof(double) == sizeof(uint64_t), "double must be 8 bytes");
    std::memcpy(&u, &d, sizeof(u));
    return u;
}
} // namespace

uint64_t shapeSignature(const ShapeParams& p, double documentLineWidth) {
    uint64_t h = 0xC0FFEEull;
    h = mix(h, p.presetId);
    h = mix(h, static_cast<uint64_t>(p.form));
    h = mix(h, static_cast<uint64_t>(p.appearance));
    h = mix(h, static_cast<uint64_t>(p.line));
    h = mix(h, static_cast<uint64_t>(p.arcSweepDeg + 1000));
    h = mix(h, bits(p.frame.centre.x)); h = mix(h, bits(p.frame.centre.y));
    h = mix(h, bits(p.frame.axisU.x));  h = mix(h, bits(p.frame.axisU.y));
    h = mix(h, bits(p.frame.axisV.x));  h = mix(h, bits(p.frame.axisV.y));
    h = mix(h, bits(p.cornerFraction));
    h = mix(h, bits(p.arcStartDeg)); h = mix(h, bits(p.arcSpanDeg));
    h = mix(h, bits(p.scale));
    h = mix(h, bits(p.style.width));
    h = mix(h, p.style.colorRGBA);
    h = mix(h, p.style.dash);
    h = mix(h, p.fillRGBA);
    h = mix(h, bits(documentLineWidth));
    return h;
}

uint64_t bracketSignature(const BracketParams& p, double documentLineWidth) {
    uint64_t h = 0xB0ACull;
    h = mix(h, p.presetId);
    h = mix(h, static_cast<uint64_t>(p.family));
    h = mix(h, static_cast<uint64_t>(p.side));
    h = mix(h, static_cast<uint64_t>(p.direction));
    h = mix(h, bits(p.frame.centre.x)); h = mix(h, bits(p.frame.centre.y));
    h = mix(h, bits(p.frame.axisU.x));  h = mix(h, bits(p.frame.axisU.y));
    h = mix(h, bits(p.frame.axisV.x));  h = mix(h, bits(p.frame.axisV.y));
    h = mix(h, bits(p.scale));
    h = mix(h, bits(p.style.width));
    h = mix(h, p.style.colorRGBA);
    h = mix(h, bits(documentLineWidth));
    for (size_t i = 0; i < p.labels.size(); ++i) {
        h = mix(h, bits(p.labels[i].anchor.u));
        h = mix(h, bits(p.labels[i].anchor.v));
        h = mix(h, bits(p.labels[i].size));
        for (size_t k = 0; k < p.labels[i].text.size(); ++k)
            h = mix(h, static_cast<uint64_t>(static_cast<unsigned char>(p.labels[i].text[k])));
        h = mix(h, 0x5EEDull);
    }
    h = mix(h, bits(documentLineWidth));
    return h;
}

} // namespace graphics
} // namespace r62
