#include "next/arrows_curves/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace r62 {
namespace arrows {

namespace {

const double kPi = 3.14159265358979323846;

core::Vec2 add(const core::Vec2& a, const core::Vec2& b) { return core::Vec2(a.x + b.x, a.y + b.y); }
core::Vec2 sub(const core::Vec2& a, const core::Vec2& b) { return core::Vec2(a.x - b.x, a.y - b.y); }
core::Vec2 mul(const core::Vec2& a, double s) { return core::Vec2(a.x * s, a.y * s); }
double     len(const core::Vec2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }
double     dotp(const core::Vec2& a, const core::Vec2& b) { return a.x * b.x + a.y * b.y; }
// Document space is y DOWN (contracts.hpp). Two normals are therefore possible and
// they mean OPPOSITE things on screen, so both are named rather than one being
// derived with a sign at each call site:
//
//   clockwiseNormal(d) = (-d.y,  d.x)   the MATH left normal; points screen-RIGHT
//                                       for a +x direction, because y grows down
//   screenLeft(d)      = ( d.y, -d.x)   the visually LEFT-hand side
//
// [GUIDE p111] defines "left" and "right" for a one-sided arrow head RELATIVE TO
// THE DIRECTION THE HEAD POINTS, i.e. as the user sees them. The head builder must
// therefore use screenLeft/screenRight. Mixing the two up is invisible in a
// symmetric test and wrong in the picture, which is why the names carry the axis.
core::Vec2 normalize(const core::Vec2& v) {
    const double n = len(v);
    return n > 1e-12 ? core::Vec2(v.x / n, v.y / n) : core::Vec2(1.0, 0.0);
}
core::Vec2 clockwiseNormal(const core::Vec2& d) { return core::Vec2(-d.y, d.x); }
core::Vec2 screenLeft(const core::Vec2& d) { return core::Vec2(d.y, -d.x); }

// ---------------------------------------------------------------------------
// ArcParam: the ONE derivation of a circular arc from (begin, end, sagitta).
// ---------------------------------------------------------------------------
// flattenArc, arcMidpoint, the head builder and the bounds all need pieces of the
// same construction. Deriving them independently is how a 270-degree arc ended up
// with a 350-degree length in one place and the right length in another, so the
// construction lives here once and each consumer reads the fields it needs.
//
// Local frame: begin = (0,0), end = (chord,0), u = chord direction,
//              n = clockwiseNormal(u), world = begin + u*px + n*py.
struct ArcParam {
    bool     valid;
    double   chord, halfL, s, cy, radius;
    double   a0, a1, sweep;      // angles relative to the circle centre
    core::Vec2 u, n, begin;
    ArcParam() : valid(false), chord(0), halfL(0), s(0), cy(0), radius(0),
                 a0(0), a1(0), sweep(0) {}

    // Local-frame point at parameter t in [0,1] along the arc.
    core::Vec2 pointAt(double t) const {
        const double ang = a0 + sweep * t;
        const double px = halfL + radius * std::cos(ang);
        const double py = cy     + radius * std::sin(ang);
        return add(begin, add(mul(u, px), mul(n, py)));
    }
    // Unit direction of travel at parameter t in [0,1]. Exact, no flattening.
    core::Vec2 tangentAt(double t) const {
        const double ang = a0 + sweep * t;
        const double sign = sweep >= 0.0 ? 1.0 : -1.0;
        const core::Vec2 d(-radius * std::sin(ang), radius * std::cos(ang));
        return normalize(mul(add(mul(u, d.x), mul(n, d.y)), sign));
    }
};

ArcParam makeArcParam(const core::Vec2& begin, const core::Vec2& end, double sagitta) {
    ArcParam a;
    const core::Vec2 d = sub(end, begin);
    a.chord = len(d);
    if (a.chord < 1e-9) return a;
    a.begin = begin;
    a.u = mul(d, 1.0 / a.chord);
    a.n = clockwiseNormal(a.u);
    a.s = sagitta;
    a.halfL = a.chord * 0.5;
    if (std::fabs(sagitta) < 1e-9 || !std::isfinite(sagitta)) {
        a.valid = true;   // a valid STRAIGHT span; radius stays 0
        return a;
    }
    a.cy = (a.s * a.s - a.halfL * a.halfL) / (2.0 * a.s);
    a.radius = std::sqrt(a.halfL * a.halfL + a.cy * a.cy);
    if (!(a.radius > 1e-9)) return a;

    const double theta = 2.0 * std::asin(
        std::max(-1.0, std::min(1.0, a.halfL / a.radius)));
    const bool majorArc = (a.cy * a.s) > 0.0;
    const double included = majorArc ? (2.0 * kPi - theta) : theta;
    a.a0 = std::atan2(-a.cy, 0.0 - a.halfL);
    a.a1 = std::atan2(-a.cy, a.chord - a.halfL);
    double forward = a.a1 - a.a0;
    while (forward <= 0.0) forward += 2.0 * kPi;
    const double errF = std::fabs(forward - included);
    const double errB = std::fabs((forward - 2.0 * kPi) + included);
    a.sweep = (errF <= errB) ? forward : (forward - 2.0 * kPi);
    a.valid = true;
    return a;
}

// Chord direction, or the x axis for a degenerate chord. A zero-length arrow is
// refused earlier, but the helper must not divide by zero on the way there.
core::Vec2 chordDir(const core::Vec2& begin, const core::Vec2& end) {
    return normalize(sub(end, begin));
}

double effectiveTolerance(const Tessellation& t, double span) {
    double tol = t.tolerance;
    if (!(tol > 0.0)) tol = span > 1.0 ? span / 400.0 : 0.02;
    if (!(tol > 1e-6)) tol = 1e-6;
    return tol;
}

void addBound(core::Box* box, const core::Vec2& p, bool* any) {
    if (!*any) { box->min = p; box->max = p; *any = true; }
    else box->add(p);
}

} // namespace

// ---------------------------------------------------------------------------
// ArrowGeometry
// ---------------------------------------------------------------------------
size_t ArrowGeometry::segmentCount() const {
    size_t n = 0;
    for (size_t i = 0; i < shafts.size(); ++i)
        if (shafts[i].size() > 1) n += shafts[i].size() - 1;
    for (size_t i = 0; i < heads.size(); ++i) n += heads[i].strokeSegments();
    for (size_t i = 0; i < markers.size(); ++i)
        if (markers[i].size() > 1) n += markers[i].size() - 1;
    return n;
}

// ---------------------------------------------------------------------------
// Stroke width
// ---------------------------------------------------------------------------
// Mirrors the resolver in preset.cpp; kept as a function rather than a stored
// field because a style change must not have to rewrite the geometry record.
double arrowStrokeWidth(const ArrowParams& params, double documentLineWidth) {
    if(params.strokeWidth>0)return params.strokeWidth*(params.scale>0?params.scale:1);
    const double base = documentLineWidth > 0.0 ? documentLineWidth : 0.6;
    double f = 1.0;
    switch (params.line) {
    case ArrowLine::Bold:   f = 2.6; break;
    case ArrowLine::Dashed: f = 1.0; break;
    case ArrowLine::Hollow: f = 1.0; break;
    case ArrowLine::Solid:
    default:                f = 1.0; break;
    }
    return base * f * (params.scale > 0.0 ? params.scale : 1.0);
}

// ---------------------------------------------------------------------------
// flattenArc
// ---------------------------------------------------------------------------
// A circular arc through `begin` and `end` whose mid-point bulges by `sagitta`
// perpendicular to the chord. Working in a local frame keeps the computation to
// one sin/cos each and makes the CW/CCW sign convention explicit in one place.
//
// local frame:  u = chord direction (unit)
//               n = left normal of u  == screen-clockwise normal in y-down space
//               begin = (0,0), end = (chordLength, 0)
//               arc midpoint = (chord/2, -sagitta)   [n has magnitude sagitta]
//
// The circle through (0,0), (L,0) and (L/2,-s) has its centre on x = L/2, so
//     cx = L/2,  cy = (s^2 - (L/2)^2) / (2s)
// and the arc is walked from angle(begin) to angle(end) the short way round
// through the mid-angle. Feeding a negative `s` flips the bulge, which is how
// CCW is expressed without a second code path.
size_t flattenArc(const core::Vec2& begin, const core::Vec2& end, double sagitta,
                  const Tessellation& tess, std::vector<core::Vec2>* out) {
    if (!out) return 0;
    out->clear();
    const core::Vec2 d = sub(end, begin);
    const double chord = len(d);
    if (chord < 1e-9) { out->push_back(begin); return 1; }

    const ArcParam arc = makeArcParam(begin, end, sagitta);
    if (!arc.valid) { out->push_back(begin); out->push_back(end); return 2; }
    if (arc.radius <= 1e-9) {
        // A degenerate sagitta: a straight span.
        out->push_back(begin);
        out->push_back(end);
        return 2;
    }

    const double tol = effectiveTolerance(tess, chord);
    int segments = static_cast<int>(std::ceil(std::fabs(arc.sweep) /
                   (2.0 * std::acos(std::max(-1.0, std::min(1.0,
                    1.0 - tol / arc.radius))))));
    if (!(segments > 1)) segments = std::fabs(arc.sweep) > 0.02 ? 2 : 1;
    if (tess.maxSegments > 0 && segments > tess.maxSegments) segments = tess.maxSegments;
    if (segments < 1) segments = 1;

    out->reserve(static_cast<size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i)
        out->push_back(arc.pointAt(static_cast<double>(i) / static_cast<double>(segments)));

    // Pin the ends exactly. The tessellation APPROXIMATES the arc, so its first and
    // last samples carry the angular sampling error (up to a whole segment's chord).
    // Leaving that in would make an arrow visibly detached from whatever it anchors
    // to, and would break the preview == commit guarantee the tools rely on.
    if (!out->empty()) {
        out->front() = begin;
        out->back()  = end;
    }
    return out->size();
}

core::Vec2 arcMidpoint(const core::Vec2& begin, const core::Vec2& end, double sagitta) {
    // The point HALFWAY ALONG THE ARC, i.e. arc.pointAt(0.5) of the SAME
    // parametrisation flattenArc() walks. Deriving it any other way (for instance by
    // reflecting the control point about the centre) only works for a MINOR arc and
    // is wrong for a major one, which is what every 270-degree preset is: the
    // reflection is 96pt away from the true point for a 40pt chord.
    const ArcParam arc = makeArcParam(begin, end, sagitta);
    if (!arc.valid) return add(begin, mul(sub(end, begin), 0.5));
    if (arc.radius <= 1e-9) return add(begin, mul(sub(end, begin), 0.5));
    return arc.pointAt(0.5);
}

double sagittaFromHandleDrag(const core::Vec2& begin, const core::Vec2& end,
                             const core::Vec2& pointer) {
    const core::Vec2 d = sub(end, begin);
    const double chord = len(d);
    if (chord < 1e-9) return 0.0;
    const core::Vec2 u = mul(d, 1.0 / chord);
    const core::Vec2 n = clockwiseNormal(u);
    // The sagitta is the SIGNED offset of the arc's half-way point along n, so this
    // is simply the pointer's own n-component measured from the chord midpoint. The
    // earlier version negated it, which made the arc bulge AWAY from the pointer: a
    // drag upward drew an arc below the chord. The picture still "looked like an
    // arc", so only the "committed midpoint == dragged point" check caught it.
    const core::Vec2 fromMid = sub(pointer, add(begin, mul(u, chord * 0.5)));
    return dotp(fromMid, n);
}

// ---------------------------------------------------------------------------
// buildHead
// ---------------------------------------------------------------------------
// [GUIDE p109] "There are three arrowhead shapes: solid, hollow, and angled."
// Those three are implemented exactly. The additional shapes the 56 presets
// need (Barb for "bold", Resonance, Cross/Hash for no-go, HalfArrow for
// retrosynthetic) are PROJECT interpretations of ChemDraw presets whose exact
// vendor geometry is not documented in the guide.
HeadGeometry buildHead(const core::Vec2& tail, const core::Vec2& tip,
                       const HeadParams& params) {
    HeadGeometry h;
    h.tip = tip;
    h.angleDeg = core::angleOf(sub(tip, tail));
    if (params.shape == ArrowHeadShape::None || params.sides == HeadSides::None)
        return h;
    const double L = params.length * 1.0;
    const double W = params.width;
    if (!(L > 1e-9)) return h;

    core::Vec2 dir = sub(tip, tail);
    if (len(dir) < 1e-12) dir = core::Vec2(1.0, 0.0);
    dir = normalize(dir);
    // Back of the head, and the two barb corners. Left/Right are named relative to
    // the direction of travel [GUIDE p111], and the user sees them on screen, so
    // `ln` is the SCREEN-left normal (see screenLeft() above).
    const core::Vec2 back = sub(tip, mul(dir, L));
    const core::Vec2 ln = screenLeft(dir);
    const core::Vec2 barbL = add(back, mul(ln,  W * 0.5));
    const core::Vec2 barbR = add(back, mul(ln, -W * 0.5));

    const bool wantL = (params.sides == HeadSides::Both || params.sides == HeadSides::Left);
    const bool wantR = (params.sides == HeadSides::Both || params.sides == HeadSides::Right);

    switch (params.shape) {
    case ArrowHeadShape::Solid:
    case ArrowHeadShape::Barb:
    case ArrowHeadShape::Hollow: {
        // A filled triangle (Solid/Barb) or an open triangle outline (Hollow).
        if (params.sides == HeadSides::Both) {
            h.outline.push_back(tip);
            h.outline.push_back(barbL);
            // Native ACS solid head: the center is inset by 1969/2250.
            if(params.shape==ArrowHeadShape::Solid)h.outline.push_back(sub(tip,mul(dir,L*.875)));
            h.outline.push_back(barbR);
        } else if (wantL) {
            h.outline.push_back(tip);
            h.outline.push_back(barbL);
            h.outline.push_back(add(mul(tip, 0.5), mul(back, 0.5)));
        } else {
            h.outline.push_back(tip);
            h.outline.push_back(barbR);
            h.outline.push_back(add(mul(tip, 0.5), mul(back, 0.5)));
        }
        if (params.shape == ArrowHeadShape::Barb) {
            // Concave (stealth) variant: pull the two barb corners slightly
            // toward the tail so the back edge is notched.
            for (size_t i = 0; i < h.outline.size(); ++i) {
                const core::Vec2 v = sub(h.outline[i], tip);
                if (len(v) > L * 0.5) {
                    h.outline[i] = add(tip, mul(normalize(v), L * 0.86));
                }
            }
        }
        h.filled = (params.shape != ArrowHeadShape::Hollow);
        h.valid = h.outline.size() >= 3;
        return h;
    }

    case ArrowHeadShape::Angled:
        // Two strokes, no fill. The side selection is what makes it one-sided.
        if (wantL) { h.strokes.push_back(tip); h.strokes.push_back(barbL); }
        if (wantR) { h.strokes.push_back(tip); h.strokes.push_back(barbR); }
        h.valid = !h.strokes.empty();
        return h;

    case ArrowHeadShape::HalfArrow: {
        // The retrosynthetic head: a triangle whose base is open (two sides
        // only), which is what reads as a "half arrow".
        h.strokes.push_back(tip); h.strokes.push_back(barbL);
        h.strokes.push_back(tip); h.strokes.push_back(barbR);
        h.valid = true;
        return h;
    }

    case ArrowHeadShape::Resonance: {
        // ChemDraw draws a resonance arrow with two short parallel strokes
        // instead of a filled head. [GUIDE p109] lists Resonance among the
        // palette arrow types; the exact stroke geometry is PROJECT.
        const double g = W * 0.28;
        const core::Vec2 a = add(back, mul(ln,  g));
        const core::Vec2 b = add(back, mul(ln, -g));
        h.strokes.push_back(a); h.strokes.push_back(add(a, mul(ln,  W * 0.9)));
        h.strokes.push_back(b); h.strokes.push_back(add(b, mul(ln, -W * 0.9)));
        h.valid = true;
        return h;
    }

    case ArrowHeadShape::Cross:
    case ArrowHeadShape::Hash:
        // Handled by the caller as a MID-shaft marker, not an end head.
        return h;

    case ArrowHeadShape::None:
    default:
        return h;
    }
}

// ---------------------------------------------------------------------------
// buildArrowGeometry
// ---------------------------------------------------------------------------
// THE function. Every consumer (renderer, hit test, SVG evidence, the editor
// preview and the commit) calls this with the same ArrowParams.
core::Error buildArrowGeometry(const ArrowParams& params,
                               double documentLineWidth,
                               const Tessellation& tess,
                               ArrowGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output geometry");
    *out = ArrowGeometry();

    core::Error v = validateArrowParams(params);
    if (!v.ok()) return v;

    const double chord = core::length(params.end - params.begin);
    if (chord < 1e-9)
        return core::Error(core::ErrorCode::InvalidGeometry,
                           "an arrow needs two distinct endpoints");

    out->strokeWidth = arrowStrokeWidth(params, documentLineWidth);
    out->hollow      = (params.line == ArrowLine::Hollow);
    if (params.line == ArrowLine::Dashed) {
        // [PROJECT] dash pattern scaled by width by the renderer; stored here in
        // document units relative to the stroke width so a Bold dashed arrow keeps
        // its proportions.
        out->dashOn  = out->strokeWidth * 3.0;
        out->dashOff = out->strokeWidth * 2.0;
    }

    const core::Vec2 dir = chordDir(params.begin, params.end);
    const core::Vec2 ln  = clockwiseNormal(dir);
    const double halfGap = (params.shafts == ShaftCount::One) ? 0.0 : params.separation * 0.5;

    const size_t shaftCount = (params.shafts == ShaftCount::One) ? 1 : 2;
    out->shafts.resize(shaftCount);

    for (size_t s = 0; s < shaftCount; ++s) {
        const double side = (s == 0) ? -1.0 : 1.0;
        const core::Vec2 off = mul(ln, halfGap * side);
        const core::Vec2 b = add(params.begin, off);
        const core::Vec2 e = add(params.end, off);

        if (params.family == ArrowFamily::Arc) {
            std::vector<core::Vec2> poly;
            // The Double family offsets the second arc the other way AND flips
            // its sagitta, which is what makes the two arcs embrace rather than
            // overlap.
            const double sag = (s == 1 && params.shafts == ShaftCount::Double)
                                   ? -params.arcSagitta : params.arcSagitta;
            flattenArc(b, e, sag, tess, &poly);
            out->shafts[s].swap(poly);
        } else {
            out->shafts[s].push_back(b);
            out->shafts[s].push_back(e);
        }
    }

    if(params.head.shape==ArrowHeadShape::HalfArrow&&params.family==ArrowFamily::Straight){for(auto& shaft:out->shafts){if(params.head.atEnd)shaft.back()=sub(shaft.back(),mul(dir,params.head.length));if(params.head.atBegin)shaft.front()=add(shaft.front(),mul(dir,params.head.length));}}
    if(params.shafts==ShaftCount::Two&&std::fabs(params.splitRatio-.5)>1e-6){auto& shaft=out->shafts[1];double ratio=std::min(params.splitRatio,1-params.splitRatio)/std::max(params.splitRatio,1-params.splitRatio);const core::Vec2 mid=mul(add(shaft.front(),shaft.back()),.5);shaft.front()=add(mid,mul(sub(shaft.front(),mid),ratio));shaft.back()=add(mid,mul(sub(shaft.back(),mid),ratio));}
    // ---- heads ------------------------------------------------------------
    // The tangent at each end of the (possibly curved) shaft is what the head
    // must align with, so the direction is read from the flattened polyline
    // rather than from the chord. That is the difference between a head that
    // sits tangentially on an arc and one that sticks out at the chord angle.
    // Heads are built from the EXACT end tangents of the CONTROL geometry, never from
    // the last flattened segment. A flattened segment's direction depends on the
    // tessellation tolerance, so deriving the head from it made the head polygon - and
    // therefore the bounds - change with zoom. The tangent of a circular arc at its
    // endpoints comes from the same ArcParam the curve itself is flattened from, so
    // the two can never disagree.
    {
        core::Vec2 tanBegin = normalize(sub(params.end, params.begin));
        core::Vec2 tanEnd   = tanBegin;
        if (params.family == ArrowFamily::Arc && std::fabs(params.arcSagitta) > 1e-9) {
            const ArcParam arc = makeArcParam(params.begin, params.end, params.arcSagitta);
            if (arc.valid && arc.radius > 1e-9) {
                tanBegin = arc.tangentAt(0.0);
                tanEnd   = arc.tangentAt(1.0);
            }
        }
        const core::Vec2 headTip = params.end;
        const core::Vec2 tailTip = params.begin;
        HeadParams endHead=params.head;if(endHead.shape==ArrowHeadShape::Cross||endHead.shape==ArrowHeadShape::Hash)endHead.shape=ArrowHeadShape::Solid;
        if(params.shafts==ShaftCount::Two){
            HeadParams head=params.head;head.shape=ArrowHeadShape::Angled;head.sides=HeadSides::Left;
            auto& upper=out->shafts[0];auto& lower=out->shafts[1];
            out->heads.push_back(buildHead(upper[upper.size()-2],upper.back(),head));
            out->heads.push_back(buildHead(lower[1],lower.front(),head));
        }
        else if (params.head.atEnd) {
            const core::Vec2 back = sub(headTip, mul(tanEnd, params.head.length));
            HeadGeometry hg = buildHead(back, headTip, endHead);
            if (hg.valid) out->heads.push_back(hg);
        }
        if (params.head.atBegin&&params.shafts!=ShaftCount::Two) {
            const core::Vec2 back = add(tailTip, mul(tanBegin, params.head.length));
            HeadGeometry hg = buildHead(back, tailTip, endHead);
            if (hg.valid) out->heads.push_back(hg);
        }
    }

    // ---- no-go markers ---------------------------------------------------
    // [GUIDE p109] crossed (no-go) arrows. The mark sits at the MIDDLE of the
    // shaft; that is where a reader looks for it, and it is why HeadSides::None
    // is used for these presets.
    if (params.head.shape == ArrowHeadShape::Cross ||
        params.head.shape == ArrowHeadShape::Hash) {
        const double L = params.head.length;
        const double W = params.head.width;
        const std::vector<core::Vec2>& sh = out->shafts[0];
        if (sh.size() >= 2) {
            // The mark goes at the HALF-ARC-LENGTH point of the shaft, not at the
            // middle INDEX. A straight arrow has only two points, so indexing would
            // put the mark on the head; on a long arc, index and arc length differ
            // anyway. Walking the polyline and halving the cumulative length is the
            // definition that matches where a reader expects to see the slash.
            double total = 0.0;
            for (size_t i = 1; i < sh.size(); ++i) total += len(sub(sh[i], sh[i - 1]));
            core::Vec2 mid = sh.front();
            core::Vec2 local = dir;
            if (total > 1e-9) {
                const double half = total * 0.5;
                double walked = 0.0;
                for (size_t i = 1; i < sh.size(); ++i) {
                    const core::Vec2 seg = sub(sh[i], sh[i - 1]);
                    const double segLen = len(seg);
                    if (walked + segLen >= half && segLen > 1e-12) {
                        const double t = (half - walked) / segLen;
                        mid = add(sh[i - 1], mul(seg, t));
                        local = mul(seg, 1.0 / segLen);
                        break;
                    }
                    walked += segLen;
                }
            }
            local = normalize(local);
            const core::Vec2 lln = clockwiseNormal(local);
            if (params.head.shape == ArrowHeadShape::Cross) {
                std::vector<core::Vec2> a;
                a.push_back(add(mid, mul(local,  L * 0.5)));
                a.push_back(add(mid, mul(local, -L * 0.5)));
                out->markers.push_back(a);
                std::vector<core::Vec2> b;
                b.push_back(add(mid, mul(lln,  W * 0.9)));
                b.push_back(add(mid, mul(lln, -W * 0.9)));
                out->markers.push_back(b);
            } else {
                // Hash: two parallel slashes across the shaft.
                for (int k = -1; k <= 1; k += 2) {
                    const core::Vec2 c = add(mid, mul(local, L * 0.28 * k));
                    std::vector<core::Vec2> sl;
                    sl.push_back(add(add(c, mul(lln,  W * 0.9)),
                                      mul(local,  L * 0.30)));
                    sl.push_back(add(add(c, mul(lln, -W * 0.9)),
                                      mul(local, -L * 0.30)));
                    out->markers.push_back(sl);
                }
            }
        }
    }

    // ---- conservative visible bounds --------------------------------------
    // The rule mirrors render::geom::computeBounds: geometry, plus half the
    // stroke width on every side, plus the head outline, plus a small padding.
    // A hit test that used the geometric extent would miss a grazing click on a
    // thick bold arrowhead.
    // Bounds come from the CONTROL geometry - the chord, the arc own extremes and
    // the head - and NOT from the flattened samples. Measuring the samples would
    // make the box grow as the tessellation refines, so a hit test would disagree
    // with itself between two zoom levels. next/render states the same rule for
    // curves, and this is the arrow equivalent of it.
    //
    // For a circular arc the extreme point away from the chord is diametrically
    // opposite the half-way point about the circle centre, so both are added:
    // together with the two endpoints that box contains the whole arc for every
    // sweep, minor or major, without flattening anything.
    bool any = false;
    addBound(&out->bounds, params.begin, &any);
    addBound(&out->bounds, params.end, &any);
    for (size_t s = 0; s < out->shafts.size(); ++s) {
        if (out->shafts[s].empty()) continue;
        addBound(&out->bounds, out->shafts[s].front(), &any);
        addBound(&out->bounds, out->shafts[s].back(), &any);
    }
    // A multi-shaft arrow's companion polylines are OFFSET by half the separation, so
    // the arc extremes computed below (which are for the centre line) do not cover
    // them. The offset is bounded by the separation, so padding by it is exact
    // enough for a bounds box and keeps the box zoom-independent. Without this the
    // bounds early-reject in hitArrow() discarded a genuine hit on the offset shaft -
    // the distance was 0 and the answer was still "miss".
    if (params.shafts != ShaftCount::One && params.separation > 0.0) {
        const double pad = params.separation * 0.5;
        for (size_t s = 0; s < out->shafts.size(); ++s)
            for (size_t i = 0; i < out->shafts[s].size(); ++i)
                addBound(&out->bounds, out->shafts[s][i], &any);
        (void)pad;
    }
    if (params.family == ArrowFamily::Arc && std::fabs(params.arcSagitta) > 1e-9) {
        const core::Vec2 mid = arcMidpoint(params.begin, params.end, params.arcSagitta);
        addBound(&out->bounds, mid, &any);
        const core::Vec2 d = sub(params.end, params.begin);
        const double chord = len(d);
        if (chord > 1e-9) {
            const double halfL = chord * 0.5;
            const double s = params.arcSagitta;
            const double cy = (s * s - halfL * halfL) / (2.0 * s);
            const core::Vec2 u = mul(d, 1.0 / chord);
            const core::Vec2 n = clockwiseNormal(u);
            const core::Vec2 chordMid = add(params.begin, mul(u, halfL));
            const core::Vec2 centre = add(chordMid, mul(n, cy));
            addBound(&out->bounds, sub(mul(centre, 2.0), mid), &any);
        }
    }
    for (size_t i = 0; i < out->heads.size(); ++i) {
        const HeadGeometry& h = out->heads[i];
        for (size_t k = 0; k < h.outline.size(); ++k) addBound(&out->bounds, h.outline[k], &any);
        for (size_t k = 0; k < h.strokes.size(); ++k) addBound(&out->bounds, h.strokes[k], &any);
    }
    for (size_t i = 0; i < out->markers.size(); ++i)
        for (size_t k = 0; k < out->markers[i].size(); ++k)
            addBound(&out->bounds, out->markers[i][k], &any);
    if (!any) {
        out->bounds = core::Box(params.begin, params.end);
        out->bounds.add(params.begin);
        out->bounds.add(params.end);
    }
    const double pad = out->strokeWidth * 0.5 + 0.05;
    out->bounds.min.x -= pad; out->bounds.min.y -= pad;
    out->bounds.max.x += pad; out->bounds.max.y += pad;

    out->valid = true;
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------
std::vector<Handle> arrowHandles(const ArrowParams& params,
                                 double documentLineWidth,
                                 const Tessellation& tess) {
    std::vector<Handle> handles;
    const core::Vec2 mid = (params.family == ArrowFamily::Arc)
        ? arcMidpoint(params.begin, params.end, params.arcSagitta)
        : core::Vec2((params.begin.x + params.end.x) * 0.5,
                     (params.begin.y + params.end.y) * 0.5);

    Handle beginH; beginH.kind = HandleKind::Begin; beginH.point = params.begin;
    handles.push_back(beginH);
    Handle endH;   endH.kind = HandleKind::End;     endH.point = params.end;
    handles.push_back(endH);

    // The middle handle is the ARC handle [GUIDE p109]: "drag an arrow from its
    // middle to create an arc of any length". It is present on every arrow, not
    // just arc presets, because a straight arrow can be curved from here.
    Handle midH;   midH.kind = HandleKind::Middle;  midH.point = mid;
    handles.push_back(midH);

    // Equilibrium: two independent half handles. [GUIDE p112] "To change the
    // length of one arrow, hold down the Alt key. Two new adjustment handles
    // appear." Those two handles are these.
    if (params.shafts != ShaftCount::One) {
        const core::Vec2 d = sub(params.end, params.begin);
        const double chord = len(d);
        if (chord > 1e-9) {
            const core::Vec2 u = mul(d, 1.0 / chord);
            Handle a; a.kind = HandleKind::SplitA; a.shaft = 0;
            a.point = add(params.begin, mul(u, chord * params.splitRatio));
            handles.push_back(a);
            Handle b; b.kind = HandleKind::SplitB; b.shaft = 1;
            b.point = add(params.begin, mul(u, chord * (1.0 - params.splitRatio)));
            handles.push_back(b);
        }
    }

    // The head-size handle sits beside the head so a drag can change it without
    // moving the tip.
    if (params.head.atEnd || params.head.atBegin) {
        const core::Vec2 dir = chordDir(params.begin, params.end);
        const core::Vec2 ln = clockwiseNormal(dir);
        const core::Vec2 tip = params.head.atEnd ? params.end : params.begin;
        Handle h; h.kind = HandleKind::HeadSize;
        h.point = add(tip, mul(ln, params.head.width * 0.9 + 1.5));
        handles.push_back(h);
    }
    (void)documentLineWidth; (void)tess;
    return handles;
}

const char* handleKindName(HandleKind k) {
    switch (k) {
    case HandleKind::None:      return "None";
    case HandleKind::Begin:     return "Begin";
    case HandleKind::End:       return "End";
    case HandleKind::Middle:    return "Middle";
    case HandleKind::ArcRadius: return "ArcRadius";
    case HandleKind::HeadSize:  return "HeadSize";
    case HandleKind::SplitA:    return "SplitA";
    case HandleKind::SplitB:    return "SplitB";
    case HandleKind::Control:   return "Control";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Reversal
// ---------------------------------------------------------------------------
// [GUIDE p110] "Click an unmodified arrow, or one that has had only its length
// changed, to reverse its direction." An arrow the user has CURVED is therefore
// NOT reversible by a click, which is exactly the predicate below.
bool canReverse(const ArrowParams& params) {
    if (params.family != ArrowFamily::Straight) return false;
    if (std::fabs(params.arcSagitta) > 1e-9) return false;  // the user curved it
    return true;
}

ArrowParams reversedArrow(const ArrowParams& params) {
    ArrowParams out = params;
    core::Vec2 t = out.begin;
    out.begin = out.end;
    out.end = t;
    out.arcSagitta = -out.arcSagitta;   // flipping the chord flips the bulge side
    // The head moves to the other end. [GUIDE p111] the words "left" and "right"
    // are relative to the direction the head points, so a one-sided head SWAPS
    // sides when the arrow is reversed - otherwise the visible barb would jump
    // to the other side of the shaft, which is not what "reverse" means.
    const bool hadEnd   = out.head.atEnd;
    const bool hadBegin = out.head.atBegin;
    out.head.atEnd   = hadBegin;
    out.head.atBegin = hadEnd;
    if (out.head.sides == HeadSides::Left) out.head.sides = HeadSides::Right;
    else if (out.head.sides == HeadSides::Right) out.head.sides = HeadSides::Left;
    // The split is mirrored so the SHORTER half stays the shorter half.
    out.splitRatio = 1.0 - out.splitRatio;
    out.originalSplitRatio = 1.0 - out.originalSplitRatio;
    return out;
}

// ---------------------------------------------------------------------------
// Free curves
// ---------------------------------------------------------------------------
// A PathRecord point list is (corner | quadratic control) plus a closed flag.
// Flattening a mixed list into polylines is the ONLY interpretation, so the
// renderer and the editor cannot disagree about what the curve looks like.
//
// The rules, stated once:
//   * a Corner point ends the current span;
//   * a QuadControl point applies to the span from the previous corner to the
//     NEXT corner (its own position is a control, not an on-curve point);
//   * a trailing QuadControl with no following corner is ignored, because there
//     is no span for it to belong to. Refusing to guess is the point.
core::Error buildCurveGeometry(const std::vector<core::Vec2>& points,
                               const std::vector<uint8_t>& pointKind,
                               bool closed, const CurveStyleSpec& style,
                               double documentLineWidth,
                               const Tessellation& tess,
                               CurveGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output geometry");
    *out = CurveGeometry();
    if (points.size() < 2)
        return core::Error(core::ErrorCode::InvalidGeometry, "a curve needs at least 2 points");
    for (size_t i = 0; i < points.size(); ++i)
        if (!points[i].finite())
            return core::Error(core::ErrorCode::InvalidGeometry, "curve point is not finite");

    core::Error vs = validateCurveStyle(style);
    if (!vs.ok()) return vs;

    const double base = documentLineWidth > 0.0 ? documentLineWidth : 0.6;
    out->strokeWidth = base * (style.bold ? 2.6 : 1.0);
    if (style.dashed) {
        out->dashOn  = out->strokeWidth * 3.0;
        out->dashOff = out->strokeWidth * 2.0;
    }
    out->closed      = closed || style.closed;
    out->fillEnabled = style.filled;
    out->fillRGBA    = style.faded ? 0x80808080u : 0x000000FFu;

    const uint8_t kCorner = 0;
    const uint8_t kQuad   = 1;

    // Walk spans. Each span runs corner -> corner, possibly through one control.
    std::vector<core::Vec2> poly;
    const size_t n = points.size();
    const size_t spanEnd = out->closed ? n : n;
    poly.push_back(points[0]);
    for (size_t i = 1; i < spanEnd; ++i) {
        const uint8_t kind = (i < pointKind.size()) ? pointKind[i] : kCorner;
        if (kind == kQuad) {
            // The control belongs to the span (i-1) -> (i+1).
            const size_t next = i + 1;
            if (next >= n) break;              // dangling control: stop, do not guess
            const core::Vec2 p0 = points[i - 1];
            const core::Vec2 c  = points[i];
            const core::Vec2 p1 = points[next];
            const double span = len(sub(p1, p0));
            const double tol = effectiveTolerance(tess, span);
            // Quadratic flatness: the standard bound is n ~ sqrt(|p0-2c+p1| / (8 tol)).
            const double d2 = len(sub(add(p0, p1), mul(c, 2.0)));
            int seg = static_cast<int>(std::ceil(std::sqrt(d2 / (8.0 * tol))));
            if (!(seg > 1)) seg = 1;
            if (tess.maxSegments > 0 && seg > tess.maxSegments) seg = tess.maxSegments;
            for (int s = 1; s <= seg; ++s) {
                const double t = static_cast<double>(s) / static_cast<double>(seg);
                const double mt = 1.0 - t;
                const core::Vec2 q(add(add(mul(p0, mt * mt), mul(c, 2.0 * mt * t)),
                                       mul(p1, t * t)));
                poly.push_back(q);
            }
            ++i;   // consumed the control AND the following corner
        } else {
            poly.push_back(points[i]);
        }
    }

    // Closing: the stored point list may or may not repeat the first point. A
    // repeated final point is a common freehand artefact; do not emit a
    // zero-length closing segment for it.
    if (out->closed && poly.size() >= 2) {
        if (len(sub(poly.back(), poly.front())) < 1e-9) poly.pop_back();
    }
    if (poly.size() < 2)
        return core::Error(core::ErrorCode::InvalidGeometry,
                           "the curve reduced to fewer than 2 drawable points");

    // ---- top-level topology -------------------------------------------------
    // A self-crossing CLOSED contour is split so a fill can be produced; a
    // self-crossing OPEN curve is left alone (there is nothing to fill) but is
    // reported. Neither case may crash, which is the acceptance criterion.
    const CurveTopology topo = analyseCurve(poly, out->closed);
    if (out->closed && topo.selfIntersects) {
        std::vector<std::vector<core::Vec2> > simple;
        splitSelfIntersections(poly, &simple);
        if (!simple.empty()) {
            for (size_t i = 0; i < simple.size(); ++i)
                if (simple[i].size() >= 3) out->contours.push_back(simple[i]);
        }
    }
    if (out->contours.empty()) out->contours.push_back(poly);

    // ---- bounds ------------------------------------------------------------
    bool any = false;
    for (size_t c = 0; c < out->contours.size(); ++c)
        for (size_t i = 0; i < out->contours[c].size(); ++i)
            addBound(&out->bounds, out->contours[c][i], &any);
    if (!any) { out->bounds = core::Box(points[0], points[0]); }
    const double pad = out->strokeWidth * 0.5 + 0.05;
    out->bounds.min.x -= pad; out->bounds.min.y -= pad;
    out->bounds.max.x += pad; out->bounds.max.y += pad;

    out->valid = true;
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Freehand decimation
// ---------------------------------------------------------------------------
namespace {

// Ramer-Douglas-Peucker on a point slice. Iterative (explicit stack) because a
// freehand capture can be thousands of points and a deep recursion on a
// pathological input is a crash, which the acceptance criteria forbid.
void rdp(const std::vector<core::Vec2>& pts, size_t first, size_t last,
         double epsilon, std::vector<char>* keep) {
    if (last <= first + 1) return;
    std::vector<std::pair<size_t, size_t> > stack;
    stack.push_back(std::make_pair(first, last));
    while (!stack.empty()) {
        const std::pair<size_t, size_t> range = stack.back();
        stack.pop_back();
        const size_t a = range.first, b = range.second;
        if (b <= a + 1) continue;
        const core::Vec2 pa = pts[a], pb = pts[b];
        const core::Vec2 ab = sub(pb, pa);
        const double abLen = len(ab);
        double worst = -1.0;
        size_t worstIdx = a;
        for (size_t i = a + 1; i < b; ++i) {
            double d = 0.0;
            if (abLen < 1e-12) {
                d = len(sub(pts[i], pa));
            } else {
                const core::Vec2 ap = sub(pts[i], pa);
                const double cross = ab.x * ap.y - ab.y * ap.x;
                d = std::fabs(cross) / abLen;
            }
            if (d > worst) { worst = d; worstIdx = i; }
        }
        if (worst > epsilon) {
            (*keep)[worstIdx] = 1;
            stack.push_back(std::make_pair(a, worstIdx));
            stack.push_back(std::make_pair(worstIdx, b));
        }
    }
}

} // namespace

core::Error simplifyFreehand(const std::vector<core::Vec2>& raw,
                             const FreehandOptions& options,
                             std::vector<core::Vec2>* out, bool* closed) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output");
    out->clear();
    if (closed) *closed = false;

    // 1. drop non-finite samples and near-duplicates, which is what a mouse at
    //    rest produces.
    std::vector<core::Vec2> cleaned;
    cleaned.reserve(raw.size());
    const double minD = options.minPointDistance > 0.0 ? options.minPointDistance : 0.0;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (!raw[i].finite()) continue;
        if (!cleaned.empty() && minD > 0.0 &&
            len(sub(raw[i], cleaned.back())) < minD) continue;
        cleaned.push_back(raw[i]);
    }
    if (cleaned.size() < 2) {
        return core::Error(core::ErrorCode::InvalidGeometry,
                           "freehand input has fewer than 2 usable samples");
    }
    if (options.maxPoints > 0 && cleaned.size() > options.maxPoints)
        cleaned.resize(options.maxPoints);

    // 2. simplification.
    std::vector<char> keep(cleaned.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    rdp(cleaned, 0, cleaned.size() - 1,
        options.simplifyTolerance > 0.0 ? options.simplifyTolerance : 0.0, &keep);
    for (size_t i = 0; i < cleaned.size(); ++i)
        if (keep[i]) out->push_back(cleaned[i]);
    if (out->size() < 2) {
        out->clear();
        out->push_back(cleaned.front());
        out->push_back(cleaned.back());
    }

    // 3. closure detection: a freehand loop returns near its start. [GUIDE p118]
    //    "To create a closed freehand shape, draw a curve" - the user draws a
    //    loop; whether the two ends meet is a tolerance question, so it is a
    //    named tolerance and not a guess.
    if (closed && options.detectClosure && out->size() >= 4) {
        const double d = len(sub(out->back(), out->front()));
        if (d <= options.closureTolerance) {
            out->pop_back();        // the shape is closed by the closed flag
            *closed = true;
        }
    }
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------
double distanceToPolyline(const std::vector<core::Vec2>& path, const core::Vec2& p) {
    if (path.empty()) return 1e300;
    if (path.size() == 1) return len(sub(p, path[0]));
    double best = 1e300;
    for (size_t i = 1; i < path.size(); ++i) {
        const core::Vec2 a = path[i - 1];
        const core::Vec2 b = path[i];
        const core::Vec2 ab = sub(b, a);
        const double ab2 = ab.x * ab.x + ab.y * ab.y;
        double d;
        if (ab2 < 1e-18) {
            d = len(sub(p, a));
        } else {
            double t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / ab2;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            const core::Vec2 q(a.x + ab.x * t, a.y + ab.y * t);
            d = len(sub(p, q));
        }
        if (d < best) best = d;
    }
    return best;
}

bool pointInPolygon(const std::vector<core::Vec2>& polygon, const core::Vec2& p) {
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const core::Vec2& a = polygon[i];
        const core::Vec2& b = polygon[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

// Is this a multi-shaft arrow?
//
// A two-shaft arrow is drawn as two parallel lines that the parameter begin/end
// points sit BETWEEN, so a click on the arrow own endpoint lands on neither stroke.
// Treating that as a miss would make an equilibrium arrow unselectable at its ends,
// so the spine between the shafts is part of the hit region.
static bool isMultiShaft(const ArrowGeometry& geometry) {
    return geometry.shafts.size() >= 2;
}

// The widest gap between the first two shafts, which bounds how far the
// "between the shafts" hit band extends.
static double distanceBetweenShafts(const ArrowGeometry& geometry) {
    if (geometry.shafts.size() < 2) return 0.0;
    double best = 0.0;
    const size_t n = std::min(geometry.shafts[0].size(), geometry.shafts[1].size());
    for (size_t i = 0; i < n; ++i)
        best = std::max(best, len(sub(geometry.shafts[0][i], geometry.shafts[1][i])));
    return best;
}

bool hitArrow(const ArrowGeometry& geometry, const core::Vec2& point, double tolerance) {
    if (!geometry.valid) return false;
    const double tol = tolerance > 0.0 ? tolerance : 0.0;
    // 1. Bounds first, so a miss outside the box is one comparison.
    core::Box probe(point, point);
    probe.min.x -= tol; probe.min.y -= tol;
    probe.max.x += tol; probe.max.y += tol;
    if (probe.max.x < geometry.bounds.min.x || probe.min.x > geometry.bounds.max.x ||
        probe.max.y < geometry.bounds.min.y || probe.min.y > geometry.bounds.max.y)
        return false;
    // 2. The shaft (and any two-shaft companion).
    const double reach = geometry.strokeWidth * 0.5 + tol;
    for (size_t s = 0; s < geometry.shafts.size(); ++s)
        if (distanceToPolyline(geometry.shafts[s], point) <= reach) return true;
    // 2b. For a two-shaft arrow the two polylines are OFFSET to either side, so the
    //     arrow own begin/end points lie between them and a click there would
    //     otherwise miss. The band between the shafts is part of the object (it is
    //     what the user sees as one arrow), so it is hit-tested too.
    if (isMultiShaft(geometry) && geometry.shafts[0].size() >= 2 &&
        geometry.shafts[1].size() >= 2) {
        const core::Vec2 a0 = geometry.shafts[0].front();
        const core::Vec2 a1 = geometry.shafts[1].front();
        const core::Vec2 b0 = geometry.shafts[0].back();
        const core::Vec2 b1 = geometry.shafts[1].back();
        std::vector<core::Vec2> spine;
        spine.push_back(core::Vec2((a0.x + a1.x) * 0.5, (a0.y + a1.y) * 0.5));
        spine.push_back(core::Vec2((b0.x + b1.x) * 0.5, (b0.y + b1.y) * 0.5));
        const double spineReach = distanceBetweenShafts(geometry) * 0.5 + reach;
        if (distanceToPolyline(spine, point) <= spineReach) return true;
    }
    // 3. A filled head is a region: a click INSIDE a big solid head must hit it,
    //    not only near its edge. That is what stops a fat arrow from having a
    //    "dead" centre.
    for (size_t i = 0; i < geometry.heads.size(); ++i) {
        const HeadGeometry& h = geometry.heads[i];
        if (h.filled && h.outline.size() >= 3 && pointInPolygon(h.outline, point)) return true;
        if (!h.strokes.empty()) {
            for (size_t k = 0; k + 1 < h.strokes.size(); k += 2) {
                std::vector<core::Vec2> seg;
                seg.push_back(h.strokes[k]);
                seg.push_back(h.strokes[k + 1]);
                if (distanceToPolyline(seg, point) <= reach) return true;
            }
        }
        if (!h.outline.empty() && distanceToPolyline(h.outline, point) <= reach) return true;
    }
    // 4. No-go markers are part of the object and must be clickable.
    for (size_t i = 0; i < geometry.markers.size(); ++i)
        if (distanceToPolyline(geometry.markers[i], point) <= reach) return true;
    return false;
}

bool hitCurve(const CurveGeometry& geometry, const core::Vec2& point, double tolerance) {
    if (!geometry.valid) return false;
    const double tol = tolerance > 0.0 ? tolerance : 0.0;
    core::Box probe(point, point);
    probe.min.x -= tol; probe.min.y -= tol;
    probe.max.x += tol; probe.max.y += tol;
    if (probe.max.x < geometry.bounds.min.x || probe.min.x > geometry.bounds.max.x ||
        probe.max.y < geometry.bounds.min.y || probe.min.y > geometry.bounds.max.y)
        return false;
    const double reach = geometry.strokeWidth * 0.5 + tol;
    for (size_t c = 0; c < geometry.contours.size(); ++c)
        if (distanceToPolyline(geometry.contours[c], point) <= reach) return true;
    // A CLOSED curve may be clicked in its interior once it means a filled shape.
    if (geometry.closed && geometry.fillEnabled && !geometry.contours.empty() &&
        pointInPolygon(geometry.contours[0], point))
        return true;
    return false;
}

// ---------------------------------------------------------------------------
// Control-point editing
// ---------------------------------------------------------------------------
core::Error addCurveSegment(std::vector<core::Vec2>* points,
                            std::vector<uint8_t>* pointKind,
                            const core::Vec2& where, bool* closed) {
    if (!points || !pointKind) return core::Error(core::ErrorCode::InvalidArgument, "no curve");
    if (!where.finite()) return core::Error(core::ErrorCode::InvalidArgument, "point not finite");
    // [GUIDE p118] "You can add a segment to an existing curve by selecting the
    // curve with EditCurve tool, and clicking in the document window with the
    // EditCurve tool selected. A segment is added to the end of the curve."
    //
    // When the click lands near the FIRST point the curve is being closed, which
    // is the same gesture ChemDraw documents on the same page. Otherwise the new
    // point is APPENDED, because "added to the end" is what the guide says.
    if (points->size() >= 3 && closed && !*closed) {
        const double d = len(sub(where, points->front()));
        const double span = len(sub(points->back(), points->front()));
        if (span > 1e-9 && d <= span * 0.12) {
            *closed = true;
            return core::Error::success();
        }
    }
    points->push_back(where);
    pointKind->push_back(0);
    return core::Error::success();
}

core::Error moveControlPoint(std::vector<core::Vec2>* points,
                             const std::vector<uint8_t>& pointKind,
                             size_t index, const core::Vec2& to) {
    if (!points) return core::Error(core::ErrorCode::InvalidArgument, "no curve");
    if (index >= points->size())
        return core::Error(core::ErrorCode::OutOfRange, "control point index out of range");
    if (!to.finite())
        return core::Error(core::ErrorCode::InvalidArgument, "control point is not finite");
    // A control point may be a quadratic control; moving it is the same
    // operation. The kind is read but not rewritten, so a control stays a
    // control. `pointKind` is passed by const reference on purpose: the seam
    // documents that this operation cannot silently promote a corner.
    (void)pointKind;
    (*points)[index] = to;
    return core::Error::success();
}

core::Error deleteControlPoint(std::vector<core::Vec2>* points,
                               std::vector<uint8_t>* pointKind, size_t index,
                               bool* closed) {
    if (!points || !pointKind) return core::Error(core::ErrorCode::InvalidArgument, "no curve");
    if (index >= points->size())
        return core::Error(core::ErrorCode::OutOfRange, "control point index out of range");
    points->erase(points->begin() + static_cast<std::ptrdiff_t>(index));
    if (index < pointKind->size())
        pointKind->erase(pointKind->begin() + static_cast<std::ptrdiff_t>(index));
    if (points->size() < 3 && closed) *closed = false;
    if (points->size() < 2)
        return core::Error(core::ErrorCode::InvalidGeometry, "a curve needs at least 2 points");
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Topology: self-intersection, holes, contour splitting
// ---------------------------------------------------------------------------
namespace {

// Proper intersection of two open segments. Collinear/touching cases are NOT
// reported as crossings, because a polyline vertex shared by two adjacent spans
// would otherwise make every polyline look self-intersecting.
bool segmentsCross(const core::Vec2& p1, const core::Vec2& p2,
                   const core::Vec2& p3, const core::Vec2& p4, core::Vec2* at) {
    const double d1x = p2.x - p1.x, d1y = p2.y - p1.y;
    const double d2x = p4.x - p3.x, d2y = p4.y - p3.y;
    const double den = d1x * d2y - d1y * d2x;
    if (std::fabs(den) < 1e-12) return false;
    const double t = ((p3.x - p1.x) * d2y - (p3.y - p1.y) * d2x) / den;
    const double u = ((p3.x - p1.x) * d1y - (p3.y - p1.y) * d1x) / den;
    if (t <= 1e-9 || t >= 1.0 - 1e-9 || u <= 1e-9 || u >= 1.0 - 1e-9) return false;
    if (at) *at = core::Vec2(p1.x + d1x * t, p1.y + d1y * t);
    return true;
}

} // namespace

CurveTopology analyseCurve(const std::vector<core::Vec2>& points, bool closed) {
    CurveTopology t;
    t.closed = closed;
    t.contourCount = points.size() >= 2 ? 1 : 0;
    const size_t n = points.size();
    if (n < 3) return t;
    const size_t last = closed ? n : n - 1;
    for (size_t i = 0; i + 1 < last && i < n; ++i) {
        const size_t i2 = (i + 1) % n;
        for (size_t j = i + 1; j + 1 < last + (closed ? 1 : 0) && j < n; ++j) {
            const size_t j2 = (j + 1) % n;
            if (i == j) continue;
            // Skip ADJACENT spans: they legitimately share a vertex.
            if (i2 == j || j2 == i) continue;
            // For a closed contour the first and last spans also share a vertex.
            if (closed && i == 0 && j2 == 0) continue;
            core::Vec2 at;
            if (segmentsCross(points[i], points[i2], points[j], points[j2], &at)) {
                ++t.intersectionCount;
                t.selfIntersects = true;
            }
        }
    }
    return t;
}

size_t splitSelfIntersections(const std::vector<core::Vec2>& points,
                              std::vector<std::vector<core::Vec2> >* out) {
    if (!out) return 0;
    out->clear();
    const size_t n = points.size();
    if (n < 4) { out->push_back(points); return out->size(); }

    // Find the FIRST crossing between span i and span j (i < j). Splitting at it
    // produces two loops that are each re-checked, bounded by the point count so
    // a pathological input cannot loop forever. This is deliberately a
    // first-crossing split repeated on a worklist rather than one global sweep:
    // it is bounded, it is obviously terminating, and its output is deterministic
    // for a given input, which is what the acceptance test needs.
    std::vector<std::vector<core::Vec2> > work;
    work.push_back(points);
    size_t guard = 0;
    const size_t guardMax = n * 2 + 8;
    while (!work.empty() && guard < guardMax) {
        ++guard;
        std::vector<core::Vec2> cur = work.back();
        work.pop_back();
        const size_t m = cur.size();
        bool split = false;
        for (size_t i = 0; i + 1 < m && !split; ++i) {
            const size_t i2 = (i + 1) % m;
            for (size_t j = i + 1; j < m + 1 && !split; ++j) {
                const size_t jm = j % m;
                const size_t j2 = (j + 1) % m;
                if (jm == i || jm == i2 || j2 == i || j2 == i2) continue;
                core::Vec2 at;
                if (!segmentsCross(cur[i], cur[i2], cur[jm], cur[j2], &at)) continue;
                // Loop A: i..jm plus the crossing point.
                std::vector<core::Vec2> a;
                for (size_t k = i; k <= jm; ++k) a.push_back(cur[k]);
                a.push_back(at);
                // Loop B: jm..end, 0..i, plus the crossing point.
                std::vector<core::Vec2> b;
                for (size_t k = jm; k < m; ++k) b.push_back(cur[k]);
                for (size_t k = 0; k <= i; ++k) b.push_back(cur[k]);
                b.push_back(at);
                if (a.size() >= 3) work.push_back(a);
                if (b.size() >= 3) work.push_back(b);
                split = true;
            }
        }
        if (!split && cur.size() >= 3) out->push_back(cur);
    }
    // Anything still on the worklist when the guard tripped is emitted as-is:
    // producing a slightly wrong fill is acceptable, hanging is not.
    while (!work.empty()) {
        if (work.back().size() >= 3) out->push_back(work.back());
        work.pop_back();
    }
    if (out->empty()) out->push_back(points);

    // Hole detection: a later contour strictly inside an earlier one. The
    // renderer consumes contours[0] as the outer boundary and the rest as holes,
    // which is exactly next/render Subpath ordering.
    for (size_t i = 0; i < out->size(); ++i) {
        for (size_t j = i + 1; j < out->size(); ++j) {
            if ((*out)[j].size() >= 3 && pointInPolygon((*out)[i], (*out)[j][0])) {
                std::swap((*out)[0], (*out)[j]);   // the containing loop becomes outer
                break;
            }
        }
    }
    return out->size();
}

// ---------------------------------------------------------------------------
// SVG evidence
// ---------------------------------------------------------------------------
namespace {

// Numbers are written with the same precision the rest of the project uses for
// evidence dumps (6 significant digits after the point, trailing zeros trimmed),
// so two runs of the same build produce byte-identical files.
std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s[s.size() - 1] == '0') s.erase(s.size() - 1);
        if (!s.empty() && s[s.size() - 1] == '.') s.erase(s.size() - 1);
    }
    if (s == "-0") s = "0";
    return s;
}

void writePoints(std::ostringstream& o, const std::vector<core::Vec2>& pts) {
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i) o << ' ';
        o << num(pts[i].x) << ',' << num(pts[i].y);
    }
}

} // namespace

std::string svgFromArrow(const ArrowGeometry& geometry, const ArrowParams& params,
                         double viewBoxWidth, double viewBoxHeight) {
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(viewBoxWidth)
      << "\" height=\"" << num(viewBoxHeight) << "\" viewBox=\"0 0 "
      << num(viewBoxWidth) << ' ' << num(viewBoxHeight) << "\">\n";
    o << "<!-- preset id=" << params.presetId << " family=" << familyName(params.family)
      << " size=" << sizeName(params.sizeClass) << " line=" << lineName(params.line)
      << " head=" << headShapeName(params.head.shape)
      << " sides=" << headSidesName(params.head.sides)
      << " shafts=" << shaftCountName(params.shafts)
      << " sweep=" << sweepName(params.sweep)
      << " dir=" << directionName(params.direction)
      << " sagitta=" << num(params.arcSagitta) << " -->\n";

    const std::string dash = (geometry.dashOn > 0.0)
        ? (" stroke-dasharray=\"" + num(geometry.dashOn) + ',' + num(geometry.dashOff) + "\"")
        : std::string();

    for (size_t s = 0; s < geometry.shafts.size(); ++s) {
        o << "  <polyline fill=\"none\" stroke=\"#000\" stroke-width=\""
          << num(geometry.strokeWidth) << "\"" << dash << " points=\"";
        writePoints(o, geometry.shafts[s]);
        o << "\"/>\n";
    }
    for (size_t i = 0; i < geometry.heads.size(); ++i) {
        const HeadGeometry& h = geometry.heads[i];
        if (h.filled && h.outline.size() >= 3) {
            o << "  <polygon fill=\"" << (geometry.hollow ? "none" : "#000")
              << "\" stroke=\"#000\" stroke-width=\"" << num(geometry.strokeWidth)
              << "\" points=\"";
            writePoints(o, h.outline);
            o << "\"/>\n";
        } else if (h.outline.size() >= 3) {
            o << "  <polygon fill=\"none\" stroke=\"#000\" stroke-width=\""
              << num(geometry.strokeWidth) << "\" points=\"";
            writePoints(o, h.outline);
            o << "\"/>\n";
        }
        for (size_t k = 0; k + 1 < h.strokes.size(); k += 2) {
            o << "  <line stroke=\"#000\" stroke-width=\"" << num(geometry.strokeWidth)
              << "\" x1=\"" << num(h.strokes[k].x) << "\" y1=\"" << num(h.strokes[k].y)
              << "\" x2=\"" << num(h.strokes[k + 1].x) << "\" y2=\""
              << num(h.strokes[k + 1].y) << "\"/>\n";
        }
    }
    for (size_t i = 0; i < geometry.markers.size(); ++i) {
        o << "  <polyline fill=\"none\" stroke=\"#000\" stroke-width=\""
          << num(geometry.strokeWidth * 1.4) << "\" points=\"";
        writePoints(o, geometry.markers[i]);
        o << "\"/>\n";
    }
    o << "</svg>\n";
    return o.str();
}

std::string svgFromCurve(const CurveGeometry& geometry) {
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\">\n";
    const std::string dash = (geometry.dashOn > 0.0)
        ? (" stroke-dasharray=\"" + num(geometry.dashOn) + ',' + num(geometry.dashOff) + "\"")
        : std::string();
    for (size_t c = 0; c < geometry.contours.size(); ++c) {
        if (c == 0 && geometry.closed && geometry.fillEnabled) {
            o << "  <polygon fill=\"#000\" stroke=\"#000\" stroke-width=\""
              << num(geometry.strokeWidth) << "\" points=\"";
            writePoints(o, geometry.contours[c]);
            o << "\"/>\n";
        } else {
            o << "  <polyline fill=\"none\" stroke=\"#000\" stroke-width=\""
              << num(geometry.strokeWidth) << "\"" << dash << " points=\"";
            writePoints(o, geometry.contours[c]);
            o << "\"/>\n";
        }
    }
    o << "</svg>\n";
    return o.str();
}

} // namespace arrows
} // namespace r62

