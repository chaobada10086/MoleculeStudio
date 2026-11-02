#include "next/symbols_orbitals/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace r62 {
namespace symbols {

namespace {

const double kPi = 3.14159265358979323846;
const double kDefaultTolerance = 0.25;

double effectiveTolerance(const Tessellation& tess) {
    if (std::isfinite(tess.tolerance) && tess.tolerance > 0.0) return tess.tolerance;
    return kDefaultTolerance;
}

double safeWidth(double w) {
    return (std::isfinite(w) && w > 0.0) ? w : 0.6;
}

void appendCircle(std::vector<core::Vec2>* out, const core::Vec2& c, double r, int segments) {
    out->clear();
    if (segments < 3) segments = 3;
    // Emit a CLOSED contour: repeat the first vertex so the polygon stroke
    // joins the last segment back to the start. Without the closing point the
    // charge ring (and any other circular outline) is drawn open, leaving a gap
    // on the right-hand side where the path started.
    out->reserve(static_cast<size_t>(segments) + 1u);
    for (int i = 0; i < segments; ++i) {
        const double a = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(segments);
        out->push_back(core::Vec2(c.x + r * std::cos(a), c.y + r * std::sin(a)));
    }
    if (!out->empty()) out->push_back((*out)[0]);
}

void appendEllipse(std::vector<core::Vec2>* out, const core::Vec2& c,
                   double semiMajor, double semiMinor, double angleRad, int segments) {
    out->clear();
    if (segments < 4) segments = 4;
    out->reserve(static_cast<size_t>(segments) + 1u);
    const double ca = std::cos(angleRad);
    const double sa = std::sin(angleRad);
    for (int i = 0; i < segments; ++i) {
        const double t = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(segments);
        const double lx = semiMajor * std::cos(t);
        const double ly = semiMinor * std::sin(t);
        out->push_back(core::Vec2(c.x + lx * ca - ly * sa, c.y + lx * sa + ly * ca));
    }
    if (!out->empty()) out->push_back((*out)[0]);
}

void appendLobe(std::vector<core::Vec2>* out, double length, double halfWidth,
                bool narrowAtOrigin, int segments) {
    out->clear();
    if (segments < 6) segments = 6;
    const int n = segments;
    out->reserve(static_cast<size_t>(2 * (n + 1)));
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n);
        const double w = halfWidth * std::sin(kPi * t);
        const double x = (narrowAtOrigin ? (length * t) : (length * (1.0 - t)));
        out->push_back(core::Vec2(x, -w));
    }
    for (int i = n; i >= 0; --i) {
        const double t = static_cast<double>(i) / static_cast<double>(n);
        const double w = halfWidth * std::sin(kPi * t);
        const double x = (narrowAtOrigin ? (length * t) : (length * (1.0 - t)));
        out->push_back(core::Vec2(x, w));
    }
}

void transformContour(std::vector<core::Vec2>* pts, const core::Vec2& anchor,
                      double rotationDeg, double scale) {
    for (size_t i = 0; i < pts->size(); ++i)
        (*pts)[i] = localToDocument((*pts)[i], anchor, rotationDeg, scale);
}

void addSignStrokes(std::vector<std::vector<core::Vec2> >* strokes,
                    const core::Vec2& c, double half, GlyphSign sign) {
    if (sign == GlyphSign::None) return;
    std::vector<core::Vec2> h;
    h.push_back(core::Vec2(c.x - half, c.y));
    h.push_back(core::Vec2(c.x + half, c.y));
    strokes->push_back(h);
    if (sign == GlyphSign::Plus) {
        std::vector<core::Vec2> v;
        v.push_back(core::Vec2(c.x, c.y - half));
        v.push_back(core::Vec2(c.x, c.y + half));
        strokes->push_back(v);
    }
}

void hatchContour(const std::vector<core::Vec2>& poly, double spacing,
                  std::vector<std::vector<core::Vec2> >* out) {
    if (poly.size() < 3 || spacing <= 0.0) return;
    core::Box b;
    for (size_t i = 0; i < poly.size(); ++i) b.add(poly[i]);
    if (b.empty()) return;
    const double dy = b.height();
    const double span = b.width() + dy;
    if (span <= 0.0) return;
    const int lines = static_cast<int>(span / spacing) + 2;
    if (lines <= 0 || lines > 4096) return;
    const double x0 = b.min.x - dy;
    for (int i = 0; i < lines; ++i) {
        const double c = x0 + static_cast<double>(i) * spacing;
        std::vector<core::Vec2> cross;
        for (size_t j = 0; j < poly.size(); ++j) {
            const size_t k = (j + 1) % poly.size();
            const core::Vec2& a = poly[j];
            const core::Vec2& q = poly[k];
            const double fa = a.y - (a.x - c);
            const double fq = q.y - (q.x - c);
            if ((fa <= 0.0) != (fq <= 0.0)) {
                const double denom = (fq - fa);
                if (denom != 0.0) {
                    const double t = -fa / denom;
                    cross.push_back(core::Vec2(a.x + (q.x - a.x) * t, a.y + (q.y - a.y) * t));
                }
            }
        }
        for (size_t j = 0; j + 1 < cross.size(); j += 2) {
            std::vector<core::Vec2> one;
            one.push_back(cross[j]);
            one.push_back(cross[j + 1]);
            out->push_back(one);
        }
    }
}

} // namespace

core::Vec2 rotateAbout(const core::Vec2& p, const core::Vec2& pivot, double degrees) {
    const double r = degrees * kPi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    const double dx = p.x - pivot.x;
    const double dy = p.y - pivot.y;
    return core::Vec2(pivot.x + dx * c + dy * s, pivot.y - dx * s + dy * c);
}

core::Vec2 scaleAbout(const core::Vec2& p, const core::Vec2& pivot, double factor) {
    return core::Vec2(pivot.x + (p.x - pivot.x) * factor,
                      pivot.y + (p.y - pivot.y) * factor);
}

core::Vec2 localToDocument(const core::Vec2& local, const core::Vec2& anchor,
                           double rotationDeg, double scale) {
    const core::Vec2 scaled = core::Vec2(local.x * scale, local.y * scale);
    return rotateAbout(scaled, core::Vec2(0, 0), rotationDeg) + anchor;
}

int circleSegments(double radius, const Tessellation& tess) {
    const double tol = effectiveTolerance(tess);
    if (!std::isfinite(radius) || radius <= 0.0) return 8;
    const double ratio = (tol >= radius) ? 1.0 : (1.0 - tol / radius);
    double theta = 2.0 * std::acos(ratio > 0.0 ? (ratio < 1.0 ? ratio : 1.0) : 0.0);
    if (!std::isfinite(theta) || theta <= 0.0) theta = kPi / 8.0;
    int n = static_cast<int>(std::ceil(2.0 * kPi / theta));
    // A charge ring is a SCREEN-FACING circle. The tolerance-derived count is
    // only ~12 segments for a 10pt ring (a visible dodecagon), so clamp the
    // minimum high enough that the ring reads as round at normal zoom. The cap
    // below still guards a runaway at very large radii.
    if (n < 64) n = 64;
    if (n > tess.maxSegments) n = tess.maxSegments;
    return n;
}

size_t ShapeGeometry::segmentCount() const {
    size_t n = 0;
    for (size_t i = 0; i < outlines.size(); ++i) n += outlines[i].size();
    for (size_t i = 0; i < strokes.size(); ++i)  n += strokes[i].size();
    for (size_t i = 0; i < dots.size(); ++i)     n += dots[i].size();
    for (size_t i = 0; i < hatches.size(); ++i)  n += hatches[i].size();
    return n;
}

const char* handleKindName(HandleKind k) {
    switch (k) {
    case HandleKind::None:         return "None";
    case HandleKind::Rotate:       return "Rotate";
    case HandleKind::Resize:       return "Resize";
    case HandleKind::LengthHandle: return "Length";
    case HandleKind::WidthHandle:  return "Width";
    case HandleKind::Move:         return "Move";
    }
    return "?";
}
// ===========================================================================
// Symbol metrics and glyph geometry
// ===========================================================================
SymbolMetrics symbolMetrics(const SymbolParams& p) {
    SymbolMetrics m;
    // Every metric is a multiple of the resolved glyph em (`p.size`), so a symbol
    // scales with the document instead of being pinned to one font size. The
    // scale multiplier the user applies is NOT folded in here: it is part of
    // localToDocument(), so the metrics describe the 1.0-scale shape.
    const double em = (std::isfinite(p.size) && p.size > 0.0) ? p.size : 5.5;
    // Charge rings are annotations beside a 10pt label, not a frame around it.
    // 0.66 em gives a ~7.3pt diameter instead of the old ~8.8pt, so the ring no
    // longer dominates the atom label; the sign is reduced with it.
    m.ringRadius   = em * 0.66;
    m.ringStroke   = em * 0.09;
    m.signHalf     = em * 0.28;
    m.dotRadius    = em * 0.13;
    m.dotGap       = em * 0.42;
    m.barHalf      = em * 0.42;
    m.attachmentR  = em * 0.46;
    m.beadRadius   = em * 0.62;
    m.siteRadius   = em * 0.10;
    return m;
}

// Build one glyph "component" contour in the LOCAL frame for a given kind, and
// return the local anchor of the shape contents. Kept as a small switch over the
// BOUNDED SymbolKind vocabulary rather than over the 16 preset ids: the table
// decides which kind a command uses, this decides how the kind looks.
namespace {

void buildGlyphLocal(SymbolKind kind, GlyphSign sign, const SymbolMetrics& m,
                     double em, const Tessellation& tess,
                     std::vector<std::vector<core::Vec2> >* outlines,
                     std::vector<int>* outlineLobe,
                     std::vector<std::vector<core::Vec2> >* strokes,
                     std::vector<std::vector<core::Vec2> >* dots) {
    const int seg = circleSegments(m.ringRadius, tess);
    switch (kind) {
    case SymbolKind::Ring: {
        std::vector<core::Vec2> ring;
        appendCircle(&ring, core::Vec2(0, 0), m.ringRadius, seg);
        outlines->push_back(ring);
        outlineLobe->push_back(-1);
        addSignStrokes(strokes, core::Vec2(0, 0), m.signHalf, sign);
        break;
    }
    case SymbolKind::Sign:
        addSignStrokes(strokes, core::Vec2(0, 0), m.signHalf, sign);
        break;
    case SymbolKind::DotPair: {
        // Two dots side by side, gap centred on the anchor.
        const double x = m.dotGap * 0.5;
        std::vector<core::Vec2> a;
        appendCircle(&a, core::Vec2(-x, 0), m.dotRadius, circleSegments(m.dotRadius, tess));
        std::vector<core::Vec2> b;
        appendCircle(&b, core::Vec2(x, 0), m.dotRadius, circleSegments(m.dotRadius, tess));
        dots->push_back(a);
        dots->push_back(b);
        break;
    }
    case SymbolKind::Dot: {
        // A radical dot is a standalone GRAPHIC that the user places and drags
        // like a text box. At the lone-pair radius it is only ~1.4pt across and
        // reads as a speck, so give the radical its own, visibly filled size.
        std::vector<core::Vec2> a;
        // Match the reference proportion: the radical dot is about one fifth of
        // the charge-ring diameter, not a bold bullet.
        const double radius = m.dotRadius * 0.9;
        appendCircle(&a, core::Vec2(0, 0), radius, circleSegments(radius, tess));
        dots->push_back(a);
        break;
    }
    case SymbolKind::Bar: {
        std::vector<core::Vec2> bar;
        bar.push_back(core::Vec2(-m.barHalf, 0));
        bar.push_back(core::Vec2(m.barHalf, 0));
        strokes->push_back(bar);
        break;
    }
    case SymbolKind::HDot:
    case SymbolKind::HDash: {
        // An "H" drawn as three strokes with a leading dot (out of plane) or a
        // leading dash (into plane) [GUIDE p242]. The H itself is the glyph; the
        // leading mark is the stereochemical cue.
        const double h = em * 0.55;
        const double hw = em * 0.36;
        // Two verticals of the H.
        std::vector<core::Vec2> left;
        left.push_back(core::Vec2(-hw * 0.5, -h * 0.5));
        left.push_back(core::Vec2(-hw * 0.5, h * 0.5));
        strokes->push_back(left);
        std::vector<core::Vec2> right;
        right.push_back(core::Vec2(hw * 0.5, -h * 0.5));
        right.push_back(core::Vec2(hw * 0.5, h * 0.5));
        strokes->push_back(right);
        // The crossbar.
        std::vector<core::Vec2> bar;
        bar.push_back(core::Vec2(-hw * 0.5, 0));
        bar.push_back(core::Vec2(hw * 0.5, 0));
        strokes->push_back(bar);
        // The leading mark, to the LEFT of the H (away from the atom).
        const double mx = -(hw * 0.5 + em * 0.30);
        if (kind == SymbolKind::HDot) {
            std::vector<core::Vec2> d;
            appendCircle(&d, core::Vec2(mx, 0), m.dotRadius, circleSegments(m.dotRadius, tess));
            dots->push_back(d);
        } else {
            std::vector<core::Vec2> d;
            d.push_back(core::Vec2(mx - m.barHalf * 0.5, -h * 0.35));
            d.push_back(core::Vec2(mx + m.barHalf * 0.5, h * 0.35));
            strokes->push_back(d);
        }
        break;
    }
    case SymbolKind::Attachment: {
        // A diamond stub. The star and the wavy forms are the same footprint with
        // a different stroke pattern in the render bridge; here the closed diamond
        // outline is the shared body.
        std::vector<core::Vec2> dia;
        dia.push_back(core::Vec2(0, -m.attachmentR));
        dia.push_back(core::Vec2(m.attachmentR, 0));
        dia.push_back(core::Vec2(0, m.attachmentR));
        dia.push_back(core::Vec2(-m.attachmentR, 0));
        outlines->push_back(dia);
        outlineLobe->push_back(-1);
        break;
    }
    case SymbolKind::Bead: {
        std::vector<core::Vec2> bead;
        appendCircle(&bead, core::Vec2(0, 0), m.beadRadius, circleSegments(m.beadRadius, tess));
        outlines->push_back(bead);
        outlineLobe->push_back(-1);
        break;
    }
    case SymbolKind::None:
        break;
    }
}

} // namespace

core::Error buildSymbolGeometry(const SymbolParams& p, double documentLineWidth,
                                const Tessellation& tess, ShapeGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    *out = ShapeGeometry();
    const core::Error v = validateSymbolParams(p);
    if (!v.ok()) return v;

    const SymbolMetrics m = symbolMetrics(p);
    const double em = p.size;
    const double lw = safeWidth(documentLineWidth);

    std::vector<std::vector<core::Vec2> > outlines;
    std::vector<int> outlineLobe;
    std::vector<std::vector<core::Vec2> > strokes;
    std::vector<std::vector<core::Vec2> > dots;
    buildGlyphLocal(p.kind, p.sign, m, em, tess, &outlines, &outlineLobe, &strokes, &dots);

    // Rotate / scale / translate ONCE, using the shared pipeline. Nothing below
    // re-derives the transform, which is what makes "rotation can only rotate"
    // true by construction.
    for (size_t i = 0; i < outlines.size(); ++i)
        transformContour(&outlines[i], p.position, p.rotation, p.scale);
    for (size_t i = 0; i < strokes.size(); ++i)
        transformContour(&strokes[i], p.position, p.rotation, p.scale);
    for (size_t i = 0; i < dots.size(); ++i)
        transformContour(&dots[i], p.position, p.rotation, p.scale);

    out->outlines      = outlines;
    out->outlineLobe   = outlineLobe;
    // A symbol glyph is an OUTLINE: nothing is painted. Both arrays are sized so
    // the per-outline indices stay aligned with outlineLobe for every consumer.
    out->outlineFilled.assign(outlines.size(), false);
    out->outlinePaintFill.assign(outlines.size(), false);
    out->outlinePositive.assign(outlines.size(), true);
    out->strokes       = strokes;
    out->dots          = dots;
    out->strokeWidth   = lw;
    out->valid         = true;

    // Conservative visible bounds: every emitted point plus the stroke radius.
    core::Box b;
    for (size_t i = 0; i < out->outlines.size(); ++i)
        for (size_t j = 0; j < out->outlines[i].size(); ++j) b.add(out->outlines[i][j]);
    for (size_t i = 0; i < out->strokes.size(); ++i)
        for (size_t j = 0; j < out->strokes[i].size(); ++j) b.add(out->strokes[i][j]);
    for (size_t i = 0; i < out->dots.size(); ++i)
        for (size_t j = 0; j < out->dots[i].size(); ++j) b.add(out->dots[i][j]);
    if (b.empty()) return core::Error(core::ErrorCode::InvalidGeometry, "symbol produced no geometry");
    const double pad = lw * 0.5 + m.ringStroke;
    b.min.x -= pad; b.min.y -= pad; b.max.x += pad; b.max.y += pad;
    out->bounds = b;
    return core::Error::success();
}

// ===========================================================================
// Hit testing
// ===========================================================================
double distanceToPolyline(const std::vector<core::Vec2>& path, const core::Vec2& p) {
    if (path.empty()) return std::numeric_limits<double>::infinity();
    if (path.size() == 1) return length(path[0] - p);
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const core::Vec2& a = path[i];
        const core::Vec2& b = path[i + 1];
        const core::Vec2 ab = b - a;
        const double len2 = lengthSq(ab);
        double t = 0.0;
        if (len2 > 0.0) {
            t = dot(p - a, ab) / len2;
            if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        }
        const double d = length(p - (a + ab * t));
        if (d < best) best = d;
    }
    return best;
}

bool pointInPolygon(const std::vector<core::Vec2>& polygon, const core::Vec2& p) {
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const double xi = polygon[i].x, yi = polygon[i].y;
        const double xj = polygon[j].x, yj = polygon[j].y;
        if (((yi > p.y) != (yj > p.y)) &&
            (p.x < (xj - xi) * (p.y - yi) / ((yj - yi) == 0.0 ? 1e-300 : (yj - yi)) + xi))
            inside = !inside;
    }
    return inside;
}

bool hitShape(const ShapeGeometry& geometry, const core::Vec2& point, double tolerance) {
    if (!geometry.valid) return false;
    if (tolerance < 0.0 || !std::isfinite(tolerance)) tolerance = 0.0;
    // A point inside a closed contour is a hit regardless of tolerance (an
    // orbital is a filled object); a stroke or a dot is a hit within tolerance.
    for (size_t i = 0; i < geometry.outlines.size(); ++i) {
        if (pointInPolygon(geometry.outlines[i], point)) return true;
        if (distanceToPolyline(geometry.outlines[i], point) <= tolerance) return true;
    }
    for (size_t i = 0; i < geometry.strokes.size(); ++i)
        if (distanceToPolyline(geometry.strokes[i], point) <= tolerance) return true;
    for (size_t i = 0; i < geometry.dots.size(); ++i) {
        const std::vector<core::Vec2>& d = geometry.dots[i];
        if (pointInPolygon(d, point)) return true;
        if (distanceToPolyline(d, point) <= tolerance) return true;
    }
    return false;
}
// ===========================================================================
// Orbital geometry
// ===========================================================================
namespace {

// Build the lobes of an orbital in the LOCAL frame (anchor at origin, no
// rotation, no scale). Returns one closed contour per lobe.
//
// Anchor semantics straight from the guide [p240/p241]:
//   * S / Sigma  : centered on the anchor -> the circle is centered at (0,0).
//   * SingleLobe : narrow end at the anchor -> lobe grows in +x.
//   * P          : node at the anchor -> two lobes on +/-x.
//   * HybridSp3  : node at the anchor -> a LARGE lobe in +x and a SMALL in -x.
//   * Dxy        : node at the anchor -> 4 lobes along the two diagonals.
//   * Dz2        : node at the anchor -> 2 lobes on +/-x plus a torus ring.
void buildOrbitalLocalLobes(const OrbitalParams& p, const Tessellation& tess,
                            std::vector<std::vector<core::Vec2> >* lobes) {
    lobes->clear();
    const double L = p.length;
    const double W = p.width;
    const int lobeSegs = 32;
    switch (p.shape) {
    case OrbitalShape::S: {
        std::vector<core::Vec2> c;
        appendCircle(&c, core::Vec2(0, 0), L * 0.5, circleSegments(L * 0.5, tess));
        lobes->push_back(c);
        break;
    }
    case OrbitalShape::Sigma: {
        // Same circle as the s orbital; the node marker is a decoration stroke
        // added by the geometry builder, not a second lobe.
        std::vector<core::Vec2> c;
        appendCircle(&c, core::Vec2(0, 0), L * 0.5, circleSegments(L * 0.5, tess));
        lobes->push_back(c);
        break;
    }
    case OrbitalShape::SingleLobe: {
        std::vector<core::Vec2> a;
        appendLobe(&a, L, W * 0.5, true, lobeSegs);
        lobes->push_back(a);
        break;
    }
    case OrbitalShape::P: {
        std::vector<core::Vec2> a;
        appendLobe(&a, L * 0.5, W * 0.5, false, lobeSegs);   // +x, narrow at node
        lobes->push_back(a);
        std::vector<core::Vec2> b;
        appendLobe(&b, L * 0.5, W * 0.5, true, lobeSegs);
        for (size_t i = 0; i < b.size(); ++i) b[i] = core::Vec2(-b[i].x, b[i].y);
        lobes->push_back(b);
        break;
    }
    case OrbitalShape::HybridSp3: {
        // Large lobe forward, small lobe backward. The 3:1 ratio is a [PROJECT]
        // choice; the guide only says the sp3 hybrid is asymmetric.
        std::vector<core::Vec2> a;
        appendLobe(&a, L * 0.62, W * 0.55, false, lobeSegs);
        lobes->push_back(a);
        std::vector<core::Vec2> b;
        appendLobe(&b, L * 0.22, W * 0.30, true, lobeSegs);
        for (size_t i = 0; i < b.size(); ++i) b[i] = core::Vec2(-b[i].x, b[i].y);
        lobes->push_back(b);
        break;
    }
    case OrbitalShape::Dxy: {
        // Four lobes along the two diagonals: +45, +135, -45, -135 degrees.
        const double angles[4] = { 45.0, 135.0, 225.0, 315.0 };
        for (int k = 0; k < 4; ++k) {
            std::vector<core::Vec2> lobe;
            appendLobe(&lobe, L * 0.5, W * 0.32, true, lobeSegs);
            const double r = angles[k] * kPi / 180.0;
            const double cs = std::cos(r);
            const double sn = std::sin(r);
            for (size_t i = 0; i < lobe.size(); ++i) {
                const double x = lobe[i].x, y = lobe[i].y;
                lobe[i] = core::Vec2(x * cs - y * sn, x * sn + y * cs);
            }
            lobes->push_back(lobe);
        }
        break;
    }
    case OrbitalShape::Dz2: {
        std::vector<core::Vec2> a;
        appendLobe(&a, L * 0.5, W * 0.42, false, lobeSegs);
        lobes->push_back(a);
        std::vector<core::Vec2> b;
        appendLobe(&b, L * 0.5, W * 0.42, true, lobeSegs);
        for (size_t i = 0; i < b.size(); ++i) b[i] = core::Vec2(-b[i].x, b[i].y);
        lobes->push_back(b);
        // The torus. Rendered as a ring band: an outer and an inner circle in one
        // subpath pair so the renderer can express the hole with even-odd fill.
        std::vector<core::Vec2> torusOuter;
        appendEllipse(&torusOuter, core::Vec2(0, 0), L * 0.30, W * 0.34, 0.0,
                      circleSegments(L * 0.30, tess));
        lobes->push_back(torusOuter);
        break;
    }
    }
}

// The node marker for the sigma orbital: a short stroke pair at the centre.
void appendSigmaNodeMarker(const OrbitalParams& p,
                           std::vector<std::vector<core::Vec2> >* strokes) {
    if (p.shape != OrbitalShape::Sigma) return;
    const double r = p.length * 0.5;
    std::vector<core::Vec2> a;
    a.push_back(core::Vec2(-r * 0.35, 0));
    a.push_back(core::Vec2(r * 0.35, 0));
    strokes->push_back(a);
    std::vector<core::Vec2> b;
    b.push_back(core::Vec2(0, -r * 0.35));
    b.push_back(core::Vec2(0, r * 0.35));
    strokes->push_back(b);
}

} // namespace

std::vector<std::vector<core::Vec2> > orbitalLocalLobes(const OrbitalParams& p,
                                                        const Tessellation& tess) {
    std::vector<std::vector<core::Vec2> > lobes;
    buildOrbitalLocalLobes(p, tess, &lobes);
    return lobes;
}

core::Error buildOrbitalGeometry(const OrbitalParams& p, double documentLineWidth,
                                 const Tessellation& tess, ShapeGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    *out = ShapeGeometry();
    const core::Error v = validateOrbitalParams(p);
    if (!v.ok()) return v;

    const double lw = safeWidth(documentLineWidth);
    const double scale = (std::isfinite(p.scale) && p.scale > 0.0) ? p.scale : 1.0;

    std::vector<std::vector<core::Vec2> > lobes;
    buildOrbitalLocalLobes(p, tess, &lobes);

    std::vector<std::vector<core::Vec2> > strokes;
    appendSigmaNodeMarker(p, &strokes);

    // Transform every lobe and stroke through the ONE shared pipeline.
    for (size_t i = 0; i < lobes.size(); ++i)
        transformContour(&lobes[i], p.position, p.rotation, scale);
    for (size_t i = 0; i < strokes.size(); ++i)
        transformContour(&strokes[i], p.position, p.rotation, scale);

    out->outlines = lobes;
    out->outlineLobe.clear();
    out->outlineFilled.clear();
    out->outlinePaintFill.clear();
    out->outlinePositive.clear();
    for (size_t i = 0; i < lobes.size(); ++i) {
        const int lobeIdx = (i < 6) ? static_cast<int>(i) : -1;
        const bool filled = (lobeIdx >= 0) ? p.lobeFilled[lobeIdx] : false;
        const bool pos    = (lobeIdx >= 0) ? p.lobePositive[lobeIdx] : true;
        out->outlineLobe.push_back(lobeIdx);
        out->outlineFilled.push_back(filled);
        // One predicate for every backend: Open never paints, otherwise the lobe is
        // painted and only its COLOUR follows the phase flag.
        out->outlinePaintFill.push_back(filled || p.fill != OrbitalFill::Open);
        out->outlinePositive.push_back(pos);
        // Shading = "filled with a hatch" rather than "filled with a flat colour".
        // The hatch is generated in DOCUMENT space after the transform, so the
        // hatch spacing is unaffected by the glyph rotation (a rotated object
        // keeps the same shading density).
        if (filled && p.fill == OrbitalFill::Shaded) {
            const double spacing = std::max(0.6, lw * 3.2);
            hatchContour(lobes[i], spacing, &out->hatches);
        }
    }
    out->strokes     = strokes;
    out->strokeWidth = lw;
    out->valid       = true;

    core::Box b;
    for (size_t i = 0; i < out->outlines.size(); ++i)
        for (size_t j = 0; j < out->outlines[i].size(); ++j) b.add(out->outlines[i][j]);
    for (size_t i = 0; i < out->strokes.size(); ++i)
        for (size_t j = 0; j < out->strokes[i].size(); ++j) b.add(out->strokes[i][j]);
    if (b.empty()) return core::Error(core::ErrorCode::InvalidGeometry, "orbital produced no geometry");
    const double pad = lw * 0.5;
    b.min.x -= pad; b.min.y -= pad; b.max.x += pad; b.max.y += pad;
    out->bounds = b;
    return core::Error::success();
}

// ===========================================================================
// Handles
// ===========================================================================
std::vector<Handle> symbolHandles(const SymbolParams& p, double documentLineWidth,
                                  const Tessellation& tess) {
    (void)documentLineWidth; (void)tess;
    std::vector<Handle> hs;
    if (!validateSymbolParams(p).ok()) return hs;
    const SymbolMetrics m = symbolMetrics(p);
    const double reach = m.ringRadius * 2.0 + p.size * 0.4;
    // Rotate about the anchor / charge end [GUIDE p243] when the symbol is one of
    // the rotatable ones; otherwise the only handles are move and resize.
    if (p.rotateFromEnd) {
        Handle h;
        h.kind = HandleKind::Rotate;
        h.point = localToDocument(core::Vec2(reach, 0), p.position, p.rotation, p.scale);
        hs.push_back(h);
    }
    if (p.resizable) {
        Handle h;
        h.kind = HandleKind::Resize;
        h.point = localToDocument(core::Vec2(0, -reach), p.position, p.rotation, p.scale);
        hs.push_back(h);
    }
    {
        Handle h;
        h.kind = HandleKind::Move;
        h.point = p.position;
        hs.push_back(h);
    }
    return hs;
}

std::vector<Handle> orbitalHandles(const OrbitalParams& p, double documentLineWidth,
                                   const Tessellation& tess) {
    (void)documentLineWidth; (void)tess;
    std::vector<Handle> hs;
    if (!validateOrbitalParams(p).ok()) return hs;
    const double scale = (std::isfinite(p.scale) && p.scale > 0.0) ? p.scale : 1.0;
    const double half = (p.length > 0.0 ? p.length : 1.0) * 0.5;
    {
        Handle h;
        h.kind = HandleKind::Rotate;
        h.point = localToDocument(core::Vec2(0, -half * 1.25), p.position, p.rotation, scale);
        hs.push_back(h);
    }
    {
        Handle h;
        h.kind = HandleKind::LengthHandle;
        h.point = localToDocument(core::Vec2(p.length * 0.5, 0), p.position, p.rotation, scale);
        hs.push_back(h);
    }
    {
        Handle h;
        h.kind = HandleKind::WidthHandle;
        h.point = localToDocument(core::Vec2(0, p.width * 0.5), p.position, p.rotation, scale);
        hs.push_back(h);
    }
    {
        Handle h;
        h.kind = HandleKind::Move;
        h.point = p.position;
        hs.push_back(h);
    }
    return hs;
}

double rotationFromHandleDrag(const core::Vec2& anchor, const core::Vec2& pointer,
                              double baseRotation) {
    const core::Vec2 v = pointer - anchor;
    if (lengthSq(v) <= 0.0) return baseRotation;
    return core::angleOf(v);
}

double scaleFromHandleDrag(const core::Vec2& anchor, const core::Vec2& pointer,
                           double baseRadius, double currentScale) {
    if (!(baseRadius > 0.0) || !std::isfinite(baseRadius)) return currentScale;
    const double d = length(pointer - anchor);
    if (!std::isfinite(d)) return currentScale;
    return d / baseRadius;
}
// ===========================================================================
// SVG evidence
// ===========================================================================
// Kept in the module (not the test) so the demo and the test write the SAME
// bytes for the same geometry. This is geometric evidence, not a rendering
// substitute: it proves the outlines/strokes/dots exist and where they are.
namespace {

std::string fmt(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return std::string(buf);
}

void emitPoly(std::ostringstream& os, const std::vector<core::Vec2>& pts,
              double ox, double oy, const char* fill, const char* stroke) {
    if (pts.empty()) return;
    os << "<polygon points=\"";
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i) os << " ";
        os << fmt(pts[i].x + ox) << "," << fmt(pts[i].y + oy);
    }
    os << "\" fill=\"" << fill << "\" stroke=\"" << stroke << "\" stroke-width=\"0.4\"/>\n";
}

void emitLine(std::ostringstream& os, const core::Vec2& a, const core::Vec2& b,
              double ox, double oy, const char* stroke) {
    os << "<line x1=\"" << fmt(a.x + ox) << "\" y1=\"" << fmt(a.y + oy)
       << "\" x2=\"" << fmt(b.x + ox) << "\" y2=\"" << fmt(b.y + oy)
       << "\" stroke=\"" << stroke << "\" stroke-width=\"0.4\"/>\n";
}

} // namespace

std::string svgFromShape(const ShapeGeometry& geometry, const std::string& title) {
    std::ostringstream os;
    const core::Box b = geometry.bounds;
    const double w = b.empty() ? 10.0 : (b.width() + 4.0);
    const double h = b.empty() ? 10.0 : (b.height() + 4.0);
    const double ox = b.empty() ? 2.0 : (2.0 - b.min.x);
    const double oy = b.empty() ? 2.0 : (2.0 - b.min.y);
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
       << fmt(w) << " " << fmt(h) << "\" width=\"" << fmt(w * 8)
       << "\" height=\"" << fmt(h * 8) << "\">\n";
    os << "<title>" << title << "</title>\n";
    os << "<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    os << "<g transform=\"scale(1)\">\n";

    for (size_t i = 0; i < geometry.outlines.size(); ++i) {
        const size_t li = (i < geometry.outlineLobe.size()) ? static_cast<size_t>(geometry.outlineLobe[i]) : 0;
        const bool filled = (i < geometry.outlineFilled.size()) ? geometry.outlineFilled[i] : false;
        const bool positive = (i < geometry.outlinePositive.size()) ? geometry.outlinePositive[i] : true;
        (void)li;
        // The two phases get different colours so a phase-inverted variant is
        // visibly different in the evidence, not just different in a field.
        //
        // The fill predicate MUST match render_bridge.cpp's orbitalStyle(), which
        // fills whenever the lobe is filled OR the form is not Open. Using the
        // lone 'filled' flag here made a Solid orbital whose phase is inverted
        // (_Solid2) draw as an unfilled outline in the SVG evidence while the GL
        // renderer painted it -- evidence and product disagreeing.
        const bool paintFill = (i < geometry.outlinePaintFill.size())
                                   ? geometry.outlinePaintFill[i] : filled;
        const char* fillCss = paintFill ? (positive ? "#9ec5e8" : "#e8b09e") : "none";
        emitPoly(os, geometry.outlines[i], ox, oy, fillCss, "#202020");
    }
    for (size_t i = 0; i < geometry.hatches.size(); ++i) {
        if (geometry.hatches[i].size() >= 2)
            emitLine(os, geometry.hatches[i][0], geometry.hatches[i][1], ox, oy, "#5a5a5a");
    }
    for (size_t i = 0; i < geometry.strokes.size(); ++i) {
        for (size_t j = 0; j + 1 < geometry.strokes[i].size(); ++j)
            emitLine(os, geometry.strokes[i][j], geometry.strokes[i][j + 1], ox, oy, "#101010");
    }
    for (size_t i = 0; i < geometry.dots.size(); ++i)
        emitPoly(os, geometry.dots[i], ox, oy, "#101010", "#101010");

    os << "</g>\n</svg>\n";
    return os.str();
}

} // namespace symbols
} // namespace r62
