// =============================================================================
// re_round62_viewer/next/bonds/geometry.cpp
//
// T07 - bond geometry generation. See geometry.hpp for the contract.
//
// Every constant used here is either cited in spec.hpp (GUIDE) or explicitly
// labelled PROJECT there. Nothing is invented silently.
// =============================================================================

#include "next/bonds/geometry.hpp"

#include <cmath>

namespace r62 {
namespace bonds {

namespace {

const double kPi = 3.14159265358979323846;

core::Vec2 add(const core::Vec2& a, const core::Vec2& b) { return core::Vec2(a.x + b.x, a.y + b.y); }
core::Vec2 mul(const core::Vec2& a, double s) { return core::Vec2(a.x * s, a.y * s); }

core::Vec2 along(const BondAxis& ax, double t) { return add(ax.begin, mul(ax.dir, t)); }
core::Vec2 offsetAt(const BondAxis& ax, double t, double s) {
    return add(add(ax.begin, mul(ax.dir, t)), mul(ax.normal, s));
}

BondShape stroke(const core::Vec2& a, const core::Vec2& b) {
    BondShape s;
    s.kind = BondShapeKind::Stroke;
    s.points.push_back(a);
    s.points.push_back(b);
    return s;
}

BondShape polygon(const std::vector<core::Vec2>& contour) {
    BondShape s;
    s.kind = BondShapeKind::FilledPolygon;
    s.outline = contour;
    s.fill = true;
    return s;
}

// GUIDE (guide_text.txt:1633): BondSpacing is a percentage of the bond LENGTH.
double innerGap(const BondAxis& ax, const BondGeometryOptions& o) {
    return ax.length * (o.multipleEndGapPercent / 100.0);
}

// How far to pull a decoration back from each end. A labelled atom needs extra
// clearance so a double bond does not run into the element glyph.
double endInset(const BondAxis& ax, const BondGeometryOptions& o, bool labelled) {
    double inset = innerGap(ax, o);
    if (labelled) inset += o.labelClearance;
    // Never consume more than a third of the bond: a very short decorated bond
    // must degrade to a smaller inset rather than invert.
    const double cap = ax.length / 3.0;
    return inset > cap ? cap : inset;
}

double spacingFor(const BondAxis& ax, const BondGeometryOptions& o) {
    return ax.length * (o.spacingPercent / 100.0);
}

// GUIDE (guide_text.txt:1118): "The end of a wedge is 1.5 times the bold
// width." The 1.5x is the full end width, so the half-width is boldWidth*1.5/2.
double wedgeWideHalf(const BondGeometryOptions& o) {
    return o.boldWidth * o.wedgeEndScale / 2.0;
}

} // namespace

// ---------------------------------------------------------------------------
// Axis resolution - the single source of endpoints
// ---------------------------------------------------------------------------
BondAxis makeAxis(const core::Vec2& begin, const core::Vec2& end, double minLength) {
    BondAxis ax;
    ax.begin = begin;
    ax.end = end;
    const double dx = end.x - begin.x;
    const double dy = end.y - begin.y;
    ax.length = std::sqrt(dx * dx + dy * dy);
    const double thr = minLength > 0.0 ? minLength : 0.0;
    if (!(ax.length > thr) || !begin.finite() || !end.finite()) {
        // A zero-length or non-finite bond is flagged, never silently given a
        // default direction. SOURCES_AND_LIMITS.md records "zero-length drag must
        // not manufacture a default direction" as a real past defect.
        ax.degenerate = true;
        ax.dir = core::Vec2(1.0, 0.0);
        ax.normal = core::Vec2(0.0, 1.0);
        if (!(ax.length > 0.0)) ax.length = 0.0;
        return ax;
    }
    ax.dir = core::Vec2(dx / ax.length, dy / ax.length);
    ax.normal = core::Vec2(-ax.dir.y, ax.dir.x);
    return ax;
}

BondAxis makeAxisReversed(const core::Vec2& begin, const core::Vec2& end, double minLength) {
    BondAxis ax = makeAxis(end, begin, minLength);
    ax.reversed = true;
    return ax;
}

double multipleBondSpacing(double length, const BondGeometryOptions& options) {
    if (!(length > 0.0)) return 0.0;
    return length * (options.spacingPercent / 100.0);
}

core::Vec2 clearLabel(const core::Vec2& atom, const core::Vec2& toward,
                      double labelRadius, double clearance) {
    if (!(labelRadius > 0.0) || !(clearance > 0.0)) return atom;
    const double dx = toward.x - atom.x;
    const double dy = toward.y - atom.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 1e-12)) return atom;
    const double push = labelRadius + clearance;
    if (push >= len) return core::Vec2(atom.x + dx * 0.5, atom.y + dy * 0.5);
    return core::Vec2(atom.x + dx / len * push, atom.y + dy / len * push);
}

// ---------------------------------------------------------------------------
// Multiplicity geometry
// ---------------------------------------------------------------------------
void emitMultiple(BondRenderData* out, const BondAxis& ax,
                  const BondGeometryOptions& o, int lines, DoubleSide side,
                  double insetBegin, double insetEnd) {
    const double sp = spacingFor(ax, o);
    const double t0 = insetBegin;
    const double t1 = ax.length - insetEnd;
    if (t1 < t0) return;

    if (lines <= 1) {
        // A plain single bond spans the full axis: ChemDraw does not inset a
        // single bond, and insetting it would leave a visible gap at every atom.
        out->shapes.push_back(stroke(ax.begin, ax.end));
        return;
    }

    if (lines == 2) {
        switch (side) {
            case DoubleSide::Symmetric:
                out->shapes.push_back(stroke(offsetAt(ax, t0, +sp), offsetAt(ax, t1, +sp)));
                out->shapes.push_back(stroke(offsetAt(ax, t0, -sp), offsetAt(ax, t1, -sp)));
                break;
            case DoubleSide::Left:
                out->shapes.push_back(stroke(along(ax, t0), along(ax, t1)));
                out->shapes.push_back(stroke(offsetAt(ax, t0, -sp), offsetAt(ax, t1, -sp)));
                break;
            case DoubleSide::Right:
                out->shapes.push_back(stroke(along(ax, t0), along(ax, t1)));
                out->shapes.push_back(stroke(offsetAt(ax, t0, +sp), offsetAt(ax, t1, +sp)));
                break;
            case DoubleSide::Centered:
                out->shapes.push_back(stroke(offsetAt(ax, t0, +sp), offsetAt(ax, t1, +sp)));
                out->shapes.push_back(stroke(offsetAt(ax, t0, -sp * 2.0),
                                             offsetAt(ax, t1, -sp * 2.0)));
                break;
        }
        return;
    }

    if (lines == 3) {
        // The triple axis line spans the full bond; only the outer pair is inset.
        out->shapes.push_back(stroke(ax.begin, ax.end));
        out->shapes.push_back(stroke(offsetAt(ax, t0, +sp), offsetAt(ax, t1, +sp)));
        out->shapes.push_back(stroke(offsetAt(ax, t0, -sp), offsetAt(ax, t1, -sp)));
        return;
    }

    // Quadruple: four parallel lines, symmetric about the axis, no axis line.
    out->shapes.push_back(stroke(offsetAt(ax, t0, +sp * 0.5), offsetAt(ax, t1, +sp * 0.5)));
    out->shapes.push_back(stroke(offsetAt(ax, t0, -sp * 0.5), offsetAt(ax, t1, -sp * 0.5)));
    out->shapes.push_back(stroke(offsetAt(ax, t0, +sp * 1.5), offsetAt(ax, t1, +sp * 1.5)));
    out->shapes.push_back(stroke(offsetAt(ax, t0, -sp * 1.5), offsetAt(ax, t1, -sp * 1.5)));
}

// ---------------------------------------------------------------------------
// Stereo displays: wedge / hash / hollow wedge
// ---------------------------------------------------------------------------
// GUIDE (guide_text.txt:2498): "Drag from the narrow end of the wedged bond to
// the wide end". The axis begin is the NARROW end and end is WIDE. Stereo
// direction therefore lives in the stored begin/end order and survives a save.
void emitWedge(BondRenderData* out, const BondAxis& ax, const BondGeometryOptions& o,
               bool hollow) {
    const double halfWide = wedgeWideHalf(o);
    std::vector<core::Vec2> tri;
    tri.push_back(ax.begin);
    tri.push_back(offsetAt(ax, ax.length, +halfWide));
    tri.push_back(offsetAt(ax, ax.length, -halfWide));
    if (!hollow) {
        out->shapes.push_back(polygon(tri));
        return;
    }
    BondShape outline;
    outline.kind = BondShapeKind::Stroke;
    outline.points = tri;
    outline.points.push_back(ax.begin);
    out->shapes.push_back(outline);
}

// GUIDE (guide_text.txt:1128/1631): HashSpacing is the spacing between the
// hashed lines, default 2.5 pt. Honoured as the along-axis step.
void emitHash(BondRenderData* out, const BondAxis& ax, const BondGeometryOptions& o,
              bool tapered) {
    const double step = o.hashSpacing > 0.0 ? o.hashSpacing : 2.5;
    const double halfWide = tapered ? wedgeWideHalf(o) : (o.lineWidth * 0.5);
    int lines = static_cast<int>(ax.length / step);
    if (lines < 3) lines = 3;
    if (lines > 64) lines = 64;
    BondShape fan;
    fan.kind = BondShapeKind::Polyline;
    for (int i = 1; i <= lines; ++i) {
        const double t = ax.length * (static_cast<double>(i) / (lines + 1.0));
        const double grow = tapered ? (t / ax.length) : 1.0;
        const double half = halfWide * grow;
        fan.points.push_back(offsetAt(ax, t, +half));
        fan.points.push_back(offsetAt(ax, t, -half));
    }
    out->shapes.push_back(fan);
}

// Crossed double bond ("double either"). GUIDE 2456-2457.
void emitCrossed(BondRenderData* out, const BondAxis& ax, const BondGeometryOptions& o,
                 double insetBegin, double insetEnd) {
    const double sp = spacingFor(ax, o);
    const double t0 = insetBegin;
    const double t1 = ax.length - insetEnd;
    if (t1 < t0) return;
    const double half = sp > 0.0 ? sp : ax.length * 0.1;
    out->shapes.push_back(stroke(offsetAt(ax, t0, +half), offsetAt(ax, t1, -half)));
    out->shapes.push_back(stroke(offsetAt(ax, t0, -half), offsetAt(ax, t1, +half)));
}

// Dative / coordinate bond. GUIDE 2484: arrowhead at the NEGATIVE (acceptor)
// end; 2493: dashed coordination bonds are functionally equivalent, so the
// semantics axis - not the display - is what makes a bond dative.
void emitDative(BondRenderData* out, const BondAxis& ax, const BondGeometryOptions& o) {
    const double headLen = ax.length * (o.dativeHeadPercent / 100.0);
    const double shaftEnd = ax.length - headLen;
    out->shapes.push_back(stroke(ax.begin, along(ax, shaftEnd > 0.0 ? shaftEnd : ax.length)));
    if (!(headLen > 0.0)) return;
    const double half = std::tan(o.dativeHeadAngle * kPi / 180.0) * headLen;
    BondShape head;
    head.kind = BondShapeKind::Polyline;
    head.points.push_back(offsetAt(ax, shaftEnd, +half));
    head.points.push_back(ax.end);
    head.points.push_back(offsetAt(ax, shaftEnd, -half));
    out->shapes.push_back(head);
}

void applyDashToAll(BondRenderData* out, double on, double off) {
    for (size_t i = 0; i < out->shapes.size(); ++i) {
        out->shapes[i].dashOn = on;
        out->shapes[i].dashOff = off;
    }
}
void applyWaveToAll(BondRenderData* out, const BondGeometryOptions& o) {
    for (size_t i = 0; i < out->shapes.size(); ++i) {
        out->shapes[i].wavyAmplitude = o.wavyAmplitude;
        out->shapes[i].wavyWavelength = o.wavyWavelength;
    }
}
void applyBoldToAll(BondRenderData* out, double width) {
    for (size_t i = 0; i < out->shapes.size(); ++i) out->shapes[i].strokeWidth = width;
}

// ---------------------------------------------------------------------------
// How many lines an order asks for
// ---------------------------------------------------------------------------
int orderLineCount(core::BondOrder order, bool aromaticSystem) {
    switch (order) {
        case core::BondOrder::Single:  return 1;
        case core::BondOrder::Double:  return 2;
        case core::BondOrder::Triple:  return 3;
        case core::BondOrder::Quadruple: return 4;
        case core::BondOrder::Aromatic:
            // An aromatic bond is a single line with a dashed inner line, i.e.
            // one-and-a-half. Rendered as two lines; the SEMANTICS axis keeps
            // the exact meaning so nothing is lost.
            return aromaticSystem ? 2 : 1;
        case core::BondOrder::SingleOrDouble:
        case core::BondOrder::SingleOrAromatic:
        case core::BondOrder::DoubleOrAromatic:
            // Query orders 6/7/8 mean "one of the listed multiplicities". Two
            // lines communicates the ambiguity without asserting a value.
            return 2;
        case core::BondOrder::Any:
            // "Any bond": one plain line. More would assert a multiplicity the
            // query explicitly does not have.
            return 1;
        default: return 1;
    }
}

// ---------------------------------------------------------------------------
// The public generator
// ---------------------------------------------------------------------------
BondRenderData buildBondGeometry(const BondAxis& axis, const BondAxes& axes,
                                 const BondGeometryOptions& options,
                                 const EndpointContext& ctxBegin,
                                 const EndpointContext& ctxEnd) {
    BondRenderData out;
    out.axisBegin = axis.begin;
    out.axisEnd = axis.end;

    if (axis.degenerate) {
        out.degenerate = true;
        out.note = "zero-length or non-finite bond axis";
        return out;
    }

    const double insetBegin = endInset(axis, options, ctxBegin.drawsLabel);
    const double insetEnd = endInset(axis, options, ctxEnd.drawsLabel);
    if (ctxBegin.drawsLabel || ctxEnd.drawsLabel) out.clippedByLabel = true;

    // --- display axis: stereo and style take priority over the axis line ----
    switch (axes.display) {
        case core::BondDisplay::Wedge:
            emitWedge(&out, axis, options, false);
            out.note = "solid wedge, narrow end at begin (guide 2498)";
            return out;
        case core::BondDisplay::HollowWedge:
            emitWedge(&out, axis, options, true);
            out.note = "hollow wedge, narrow end at begin";
            return out;
        case core::BondDisplay::WedgeHash:
            emitHash(&out, axis, options, true);
            out.note = "hashed wedge (WedgeHash), tapering from the narrow end";
            return out;
        case core::BondDisplay::Hash:
            emitHash(&out, axis, options, true);
            out.note = "hashed wedge (Hash), wide end at end (guide 2502-2503)";
            return out;
        case core::BondDisplay::Bold:
            out.shapes.push_back(stroke(axis.begin, axis.end));
            applyBoldToAll(&out, options.boldWidth);
            out.note = "bold bond at the GUIDE BoldWidth (2pt default)";
            return out;
        default:
            break;
    }

    // --- order axis: how many lines -----------------------------------------
    const bool aromaticSystem = (axes.semantics == core::BondSemantics::Aromatic);
    const int lines = orderLineCount(axes.order, aromaticSystem);

    // A crossed double bond is a DISPLAY of a double bond with Either stereo.
    if (axes.stereo == core::BondStereo::Either && lines == 2) {
        emitCrossed(&out, axis, options, insetBegin, insetEnd);
        out.note = "crossed double bond (double either, guide 2456-2457)";
        return out;
    }

    emitMultiple(&out, axis, options, lines, axes.side, insetBegin, insetEnd);

    // --- semantics axis ----------------------------------------------------
    if (axes.semantics == core::BondSemantics::Dative) {
        // Dative replaces the plain line with a directional line + open head.
        out.shapes.clear();
        emitDative(&out, axis, options);
        out.note = "dative/coordinate bond, arrowhead at the acceptor (end)";
        return out;
    }
    if (axes.semantics == core::BondSemantics::Hydrogen) {
        // GUIDE 2504-2510: dotted from an explicit hydrogen to an acceptor.
        applyDashToAll(&out, options.dotOn, options.dotOff);
        out.note = "hydrogen (dotted) bond; must start at an explicit H (guide 2509)";
    } else if (axes.display == core::BondDisplay::Dashed) {
        applyDashToAll(&out, options.dashOn, options.dashOff);
    } else if (axes.display == core::BondDisplay::Dotted) {
        applyDashToAll(&out, options.dotOn, options.dotOff);
    } else if (axes.display == core::BondDisplay::Wavy) {
        applyWaveToAll(&out, options);
        out.note = "wavy bond";
    }

    // Aromatic: dash the companion line so the pair reads as aromatic rather
    // than as an ordinary double bond. The axis line stays solid.
    if (axes.semantics == core::BondSemantics::Aromatic && out.shapes.size() >= 2) {
        out.shapes[1].dashOn = options.dashOn;
        out.shapes[1].dashOff = options.dashOff;
        out.note = "aromatic bond: solid axis plus dashed inner line";
    }

    return out;
}

BondRenderData buildBondGeometry(const core::Vec2& begin, const core::Vec2& end,
                                 const BondAxes& axes,
                                 const BondGeometryOptions& options) {
    const BondAxis ax = makeAxis(begin, end, options.minDecoratedLength);
    return buildBondGeometry(ax, axes, options, EndpointContext(), EndpointContext());
}

size_t visibleShapeCount(const BondRenderData& data) {
    size_t n = 0;
    for (size_t i = 0; i < data.shapes.size(); ++i) {
        const BondShape& s = data.shapes[i];
        if (s.kind == BondShapeKind::FilledPolygon) { n += 1; continue; }
        if (s.kind == BondShapeKind::Polyline) { n += s.points.size() / 2; continue; }
        n += 1;
    }
    return n;
}

size_t totalPointCount(const BondRenderData& data) {
    size_t n = 0;
    for (size_t i = 0; i < data.shapes.size(); ++i) {
        n += data.shapes[i].points.size();
        n += data.shapes[i].outline.size();
        n += data.shapes[i].fillHoles.size();
    }
    return n;
}

} // namespace bonds
} // namespace r62
