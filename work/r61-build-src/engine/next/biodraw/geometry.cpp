// =============================================================================
// re_round62_viewer/next/biodraw/geometry.cpp
//
// T14 - the 24 glyph builders. THIS IS THE ONLY PLACE A BioDraw SHAPE IS MADE.
//
// Structure of the file:
//   1. small vector/contour helpers
//   2. one static builder per glyph, all sharing the signature
//        void build(GlyphContext&)
//   3. buildGlyphGeometry() dispatching on glyph kind -- the ONLY dispatch
//   4. bounds and hit test over the built contours
//   5. non-destructive transforms and sub-element extraction
//
// The builders write into a GlyphContext that already carries the document-space
// frame, the resolved stroke width and the normalised components. A builder never
// reads a preset id and never reads the document.
// =============================================================================

#include "next/biodraw/geometry.hpp"
#include "next/biodraw/plasmid.hpp"

#include <cmath>
#include <cstddef>

namespace r62 {
namespace biodraw {

namespace {

const double kPi = 3.14159265358979323846;
double deg2rad(double d) { return d * kPi / 180.0; }
double clampDouble(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// A frame in document space, resolved once per figure, that every builder uses.
struct GlyphContext {
    const BioShapeParams* params;
    core::Vec2 centre;
    core::Vec2 axisU;      // document-space half-extent along +x (after rotation)
    core::Vec2 axisV;      // document-space half-extent along +y
    double     halfW;      // |axisU|
    double     halfH;      // |axisV|
    double     stroke;
    std::vector<HostPath> paths;
    std::vector<BioSubElement> elements;
    core::Box  ink;
    bool       haveInk;

    GlyphContext()
        : params(0), halfW(0.0), halfH(0.0), stroke(0.6), haveInk(false) {}

    core::Vec2 pointAt(double u, double v) const {
        return core::Vec2(centre.x + u * axisU.x + v * axisV.x,
                          centre.y + u * axisU.y + v * axisV.y);
    }

    void notePoint(const core::Vec2& p) {
        if (!haveInk) { ink = core::Box(p, p); haveInk = true; }
        else ink.add(p);
    }

    // Append a contour to the element named by `key`, creating it on demand and
    // KEEPING DRAW ORDER. Elements are found by linear scan because a figure has
    // tens of them, not thousands; the alternative (a map) would destroy the
    // draw order that the renderer needs.
    BioSubElement& element(uint32_t key, ComponentKind component,
                           uint32_t componentIndex, uint32_t unitIndex) {
        for (size_t i = 0; i < elements.size(); ++i)
            if (elements[i].key == key) return elements[i];
        BioSubElement created;
        created.key = key;
        created.component = component;
        created.componentIndex = componentIndex;
        created.unitIndex = unitIndex;
        elements.push_back(created);
        return elements.back();
    }

    BioContour& newContour(uint32_t key, ComponentKind component,
                           uint32_t componentIndex, uint32_t unitIndex,
                           bool closed, bool filled) {
        BioSubElement& el = element(key, component, componentIndex, unitIndex);
        BioContour contour;
        contour.closed = closed;
        contour.filled = filled;
        contour.key = key;
        el.contours.push_back(contour);
        return el.contours.back();
    }

    void push(BioContour& c, const core::Vec2& p, uint8_t kind) {
        c.points.push_back(p);
        c.kind.push_back(kind);
        notePoint(p);
    }
};

// ---------------------------------------------------------------------------
// Contour construction helpers
// ---------------------------------------------------------------------------
// A closed polygonal contour from a list of (u,v) frame coordinates.
void addFramePolygon(GlyphContext& ctx, uint32_t key, ComponentKind component,
                     uint32_t ci, uint32_t ui, const double (*uv)[2], size_t count,
                     bool filled) {
    if (count < 2) return;
    BioContour& c = ctx.newContour(key, component, ci, ui, true, filled);
    for (size_t i = 0; i < count; ++i) ctx.push(c, ctx.pointAt(uv[i][0], uv[i][1]), kPointCorner);
}

// A closed/ open polyline from a parametric function evaluated on the frame.
template <typename Fn>
void addSampledPath(GlyphContext& ctx, uint32_t key, ComponentKind component,
                    uint32_t ci, uint32_t ui, Fn fn, int samples, bool closed,
                    bool filled) {
    if (samples < 2) return;
    BioContour& c = ctx.newContour(key, component, ci, ui, closed, filled);
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(samples - (closed ? 0 : 1));
        double u = 0.0, v = 0.0;
        fn(t, &u, &v);
        ctx.push(c, ctx.pointAt(u, v), kPointCorner);
    }
}

// An ellipse in FRAME coordinates (u,v), which becomes a true ellipse after a
// non-uniform stretch because the frame carries the anisotropy.
void addFrameEllipse(GlyphContext& ctx, uint32_t key, ComponentKind component,
                     uint32_t ci, uint32_t ui, double cu, double cv, double ru,
                     double rv, int samples, bool filled) {
    if (samples < 8) samples = 8;
    BioContour& c = ctx.newContour(key, component, ci, ui, true, filled);
    for (int i = 0; i < samples; ++i) {
        const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(samples);
        ctx.push(c, ctx.pointAt(cu + ru * std::cos(a), cv + rv * std::sin(a)), kPointCorner);
    }
}

// A straight segment, as its own open contour, so stroke handling is uniform.
void addSegment(GlyphContext& ctx, uint32_t key, ComponentKind component,
                uint32_t ci, uint32_t ui, double u0, double v0, double u1, double v1) {
    BioContour& c = ctx.newContour(key, component, ci, ui, false, false);
    ctx.push(c, ctx.pointAt(u0, v0), kPointCorner);
    ctx.push(c, ctx.pointAt(u1, v1), kPointCorner);
}

// A quadratic curve, published with the T11 'previous point is a control point'
// convention so the shared renderer flattens it.
void addQuad(GlyphContext& ctx, uint32_t key, ComponentKind component,
             uint32_t ci, uint32_t ui, double u0, double v0, double cu, double cv,
             double u1, double v1) {
    BioContour& c = ctx.newContour(key, component, ci, ui, false, false);
    ctx.push(c, ctx.pointAt(u0, v0), kPointCorner);
    ctx.push(c, ctx.pointAt(cu, cv), kPointQuad);
    ctx.push(c, ctx.pointAt(u1, v1), kPointCorner);
}


// N points around an ellipse, as a closed polygon (a vesicle, ring or barrel).
void addRingPolygon(GlyphContext& ctx, uint32_t key, ComponentKind component,
                    uint32_t ci, uint32_t ui, double ru, double rv, int nodes,
                    bool filled) {
    if (nodes < 3) nodes = 3;
    BioContour& c = ctx.newContour(key, component, ci, ui, true, filled);
    for (int i = 0; i < nodes; ++i) {
        const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(nodes);
        ctx.push(c, ctx.pointAt(ru * std::cos(a), rv * std::sin(a)), kPointCorner);
    }
}

// A rounded-corner rectangle (capsule body). cornerFraction 0 gives a rectangle,
// 1 gives a stadium.
void addRoundRect(GlyphContext& ctx, uint32_t key, ComponentKind component,
                  uint32_t ci, uint32_t ui, double fraction, bool filled) {
    const double r = clampDouble(fraction, 0.0, 1.0);
    if (r <= 1e-6) {
        const double uv[4][2] = { { -1.0, -1.0 }, { 1.0, -1.0 }, { 1.0, 1.0 }, { -1.0, 1.0 } };
        addFramePolygon(ctx, key, component, ci, ui, uv, 4, filled);
        return;
    }
    // Straight runs between quarter-circle corners, sampled so a quad control is
    // not needed and the contour is exact for the shortening.
    const int per = 6;
    BioContour& c = ctx.newContour(key, component, ci, ui, true, filled);
    const double cx[4] = { 1.0 - r, 1.0 - r, -1.0 + r, -1.0 + r };
    const double cy[4] = { -1.0 + r, 1.0 - r, 1.0 - r, -1.0 + r };
    const double a0[4] = { -kPi / 2.0, 0.0, kPi / 2.0, kPi };
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= per; ++i) {
            const double a = a0[corner] + (kPi / 2.0) * static_cast<double>(i) / static_cast<double>(per);
            ctx.push(c, ctx.pointAt(cx[corner] + r * std::cos(a), cy[corner] + r * std::sin(a)),
                     kPointCorner);
        }
    }
}

} // namespace
namespace {

// ---------------------------------------------------------------------------
// Shared sub-drawings and small helpers
// ---------------------------------------------------------------------------
// A segment given in ABSOLUTE document coordinates. Everything that needs a
// segment between two computed points uses this, so no builder has to reach back
// into an element list to patch a contour it just created.
void addAbsSegment(GlyphContext& ctx, uint32_t key, ComponentKind component,
                   uint32_t ci, uint32_t ui, const core::Vec2& a, const core::Vec2& b) {
    BioContour& c = ctx.newContour(key, component, ci, ui, false, false);
    ctx.push(c, a, kPointCorner);
    ctx.push(c, b, kPointCorner);
}

// A filled or hollow disc for one placed unit, using the unit's own frame. All
// repeated-unit drawing goes through here, so 'count changes -> units change' is
// identical in every glyph.
void addUnitDisc(GlyphContext& ctx, const PlacedUnit& u, bool filled, int samples) {
    if (samples < 6) samples = 6;
    BioContour& c = ctx.newContour(u.key, u.kind, u.componentIndex, u.unitIndex,
                                   true, filled);
    for (int k = 0; k < samples; ++k) {
        const double a = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(samples);
        ctx.push(c, core::Vec2(u.centre.x + u.axisU.x * std::cos(a) + u.axisV.x * std::sin(a),
                               u.centre.y + u.axisU.y * std::cos(a) + u.axisV.y * std::sin(a)),
                     kPointCorner);
    }
}

void drawComponentUnits(GlyphContext& ctx, size_t index, bool filled, int samples) {
    if (index >= ctx.paths.size()) return;
    const std::vector<PlacedUnit> units = expandUnits(*ctx.params, index, ctx.paths[index]);
    for (size_t i = 0; i < units.size(); ++i) addUnitDisc(ctx, units[i], filled, samples);
}

// Units drawn as a SHORT BAR across the path (a base pair rung, a crista, a
// cisterna). The bar follows the unit frame's +y, so it stays perpendicular to a
// curved host path rather than to the screen.
void drawComponentBars(GlyphContext& ctx, size_t index, double halfExtent) {
    if (index >= ctx.paths.size()) return;
    const std::vector<PlacedUnit> units = expandUnits(*ctx.params, index, ctx.paths[index]);
    for (size_t i = 0; i < units.size(); ++i) {
        const PlacedUnit& u = units[i];
        const double lenV = core::length(u.axisV) > 1e-9 ? core::length(u.axisV) : 1.0;
        const core::Vec2 v(u.axisV.x / lenV, u.axisV.y / lenV);
        addAbsSegment(ctx, u.key, u.kind, u.componentIndex, u.unitIndex,
                      core::Vec2(u.centre.x - v.x * halfExtent, u.centre.y - v.y * halfExtent),
                      core::Vec2(u.centre.x + v.x * halfExtent, u.centre.y + v.y * halfExtent));
    }
}

// An organic body whose outline bulges with `lobes` lobes. The base radius is 1
// in frame units, so the frame's anisotropy stretches it. lobes == 0 gives a
// plain ellipse.
void outlineBlob(GlyphContext& ctx, uint32_t key, double lobes, double depth,
                 int samples, bool filled) {
    if (samples < 16) samples = 16;
    const int n = lobes > 0.0 ? static_cast<int>(lobes + 0.5) : 0;
    BioContour& c = ctx.newContour(key, ComponentKind::None, 0, 0, true, filled);
    for (int i = 0; i < samples; ++i) {
        const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(samples);
        const double r = 1.0 + (n > 0 ? depth * std::sin(static_cast<double>(n) * a) : 0.0);
        ctx.push(c, ctx.pointAt(r * std::cos(a), r * std::sin(a)), kPointCorner);
    }
}

// A run of points along a host path, offset by `normalOffset` and `extra`. Used
// for membrane rails, ribbons and helix backbones.
void addPathRun(GlyphContext& ctx, uint32_t key, ComponentKind component,
                uint32_t ci, uint32_t ui, const HostPath& path, double normalOffset,
                int samples, bool closed) {
    if (samples < 2) samples = 2;
    BioContour& c = ctx.newContour(key, component, ci, ui, closed, false);
    for (int i = 0; i < samples; ++i) {
        const double t = closed ? static_cast<double>(i) / static_cast<double>(samples)
                                : static_cast<double>(i) / static_cast<double>(samples - 1);
        const core::Vec2 p = path.pointAt(t);
        const core::Vec2 tan = path.tangentAt(t);
        const core::Vec2 normal(-tan.y, tan.x);
        ctx.push(c, core::Vec2(p.x + normal.x * normalOffset, p.y + normal.y * normalOffset),
                 kPointCorner);
    }
}

// ---------------------------------------------------------------------------
// Glyph builders. Every one has the same signature.
// ---------------------------------------------------------------------------

// --- Blob: enzymes, G proteins, paramecium, amoeba -------------------------
void buildBlob(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    // Variant -> lobe count. PROJECT choice: the vendor file supplies an icon
    // name, not a topology, so this decomposition is ours and remains editable.
    static const int kLobesByVariant[16] = { 0, 0, 2, 3, 0, 0, 2, 2, 0, 3, 2, 3, 4, 2, 0, 5 };
    const int vi = (v >= 0 && v < 16) ? v : 0;
    double lobes = static_cast<double>(kLobesByVariant[vi]);
    if (g.lobes > 0.0) lobes = g.lobes;   // an explicit user value WINS
    outlineBlob(ctx, subElementKeyForBand(kBandBody), lobes, g.lobeDepth, 64, true);

    // A cleft / lumen INSIDE the body. It is its own element, so it can be
    // coloured separately -- which is what highlighting an active site needs.
    if (vi == 0 || vi == 1 || vi == 4 || vi == 6) {
        const double ru = (vi == 1) ? 0.42 : 0.28;
        const double rv = (vi == 1) ? 0.22 : 0.20;
        addFrameEllipse(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                        0, 0, g.nucleusOffset, 0.0, ru, rv, 32, true);
    }
    if (vi == 14 || vi == 15) {
        addFrameEllipse(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                        0, 0, 0.0, 0.0, 0.22, 0.22, 24, true);
    }
    // Bound beads (substrate, cofactor, GTP, cilia) are a COMPONENT: their number
    // is user data, not a hard-coded glyph detail.
    drawComponentUnits(ctx, 0, true, 12);
}

// --- Bilayer: membrane line / membrane arc ---------------------------------
void buildBilayer(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const HostPath& path = ctx.paths[0];
    const double headR = g.headRadius;
    const double gap = g.leafletGap;
    const double tailL = g.tailLength;

    // Rails, sampled along the path at +-gap normal offset. An ARC membrane
    // therefore curves and the lipids stay perpendicular to it.
    addPathRun(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
               path, -gap, 48, false);
    addPathRun(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
               path,  gap, 48, false);

    const std::vector<PlacedUnit> heads = expandUnits(*ctx.params, 0, path);
    for (size_t i = 0; i < heads.size(); ++i) {
        const PlacedUnit& u = heads[i];
        const double len = core::length(u.axisU) > 1e-9 ? core::length(u.axisU) : 1.0;
        const core::Vec2 t(u.axisU.x / len, u.axisU.y / len);   // unit tangent
        const core::Vec2 n(-t.y, t.x);                          // unit normal
        for (int side = 0; side < 2; ++side) {
            const double sgn = (side == 0) ? -1.0 : 1.0;
            const core::Vec2 hc(u.centre.x + n.x * sgn * gap, u.centre.y + n.y * sgn * gap);
            BioContour& ring = ctx.newContour(u.key, ComponentKind::LipidHead,
                                              u.componentIndex, u.unitIndex, true, true);
            for (int k = 0; k < 10; ++k) {
                const double a = 2.0 * kPi * static_cast<double>(k) / 10.0;
                ctx.push(ring, core::Vec2(hc.x + headR * std::cos(a), hc.y + headR * std::sin(a)),
                         kPointCorner);
            }
            // Tail pair, from the head back toward the midline. Both tails share
            // the lipid's key so colouring a lipid colours the whole lipid.
            const core::Vec2 base(hc.x - n.x * sgn * headR, hc.y - n.y * sgn * headR);
            const core::Vec2 tip(base.x - n.x * sgn * tailL, base.y - n.y * sgn * tailL);
            for (int tail = 0; tail < 2; ++tail) {
                const double lateral = (tail == 0 ? -0.35 : 0.35) * headR;
                addAbsSegment(ctx, u.key, ComponentKind::LipidTail, u.componentIndex,
                              u.unitIndex,
                              core::Vec2(base.x + t.x * lateral, base.y + t.y * lateral),
                              core::Vec2(tip.x + t.x * lateral * 1.8, tip.y + t.y * lateral * 1.8));
            }
        }
    }
}

} // namespace
namespace {

// --- Vesicle: membrane ellipse, micelle -------------------------------------
void buildVesicle(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    // v == 0: a closed bilayer ellipse (two rings). v == 1: a micelle, ONE ring
    // of heads with the tails filling the core (no inner leaflet).
    if (v == 1) {
        addRingPolygon(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None,
                       0, 0, 1.0, 1.0, 64, false);
    } else {
        const double gap = g.leafletGap / 24.0;   // frame fraction of the half-extent
        addFrameEllipse(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None,
                        0, 0, 0.0, 0.0, 1.0 - gap, 1.0 - gap, 64, false);
        addFrameEllipse(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None,
                        0, 0, 0.0, 0.0, 1.0 + gap, 1.0 + gap, 64, false);
    }
    if (ctx.paths.empty()) return;
    const std::vector<PlacedUnit> units = expandUnits(*ctx.params, 0, ctx.paths[0]);
    for (size_t i = 0; i < units.size(); ++i) {
        const PlacedUnit& u = units[i];
        const core::Vec2 radial(u.centre.x - ctx.centre.x, u.centre.y - ctx.centre.y);
        const double rlen = core::length(radial) > 1e-9 ? core::length(radial) : 1.0;
        const core::Vec2 r(radial.x / rlen, radial.y / rlen);
        BioContour& c = ctx.newContour(u.key, ComponentKind::LipidHead, u.componentIndex,
                                       u.unitIndex, true, true);
        for (int k = 0; k < 10; ++k) {
            const double a = 2.0 * kPi * static_cast<double>(k) / 10.0;
            ctx.push(c, core::Vec2(u.centre.x + g.headRadius * std::cos(a),
                                   u.centre.y + g.headRadius * std::sin(a)), kPointCorner);
        }
        if (v == 1) {
            // Micelle tail points inward toward the core.
            addAbsSegment(ctx, u.key, ComponentKind::LipidTail, u.componentIndex, u.unitIndex,
                          core::Vec2(u.centre.x - r.x * g.headRadius,
                                     u.centre.y - r.y * g.headRadius),
                          core::Vec2(u.centre.x - r.x * (g.headRadius + g.tailLength),
                                     u.centre.y - r.y * (g.headRadius + g.tailLength)));
        }
    }
}

// --- Capsule: mitochondrion, bacterium --------------------------------------
void buildCapsule(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    const double frac = (v == 3) ? 1.0 : 0.5;   // 3 == coccus, a stadium
    addRoundRect(ctx, subElementKeyForBand(kBandBody), ComponentKind::None, 0, 0, frac, true);
    addRoundRect(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None, 0, 0,
                 frac, false);
    // Cristae (component 0) and ribosome beads / flagella (component 1).
    drawComponentBars(ctx, 0, 0.35);
    drawComponentUnits(ctx, 1, true, 10);
    if (v == 3) {
        addRingPolygon(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None,
                       0, 0, 1.06, 1.06, 48, false);
    }
    // Nucleoid squiggle, so a bacterium is not an empty pill.
    if (ctx.paths.size() > 2) {
        addPathRun(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, 0, 0,
                   ctx.paths[2], 0.0, 40, false);
    }
    (void)g;
}

// --- Rod --------------------------------------------------------------------
void buildRod(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    addRoundRect(ctx, subElementKeyForBand(kBandBody), ComponentKind::None, 0, 0,
                 g.cornerFraction, true);
    drawComponentBars(ctx, 0, 0.6);
}

// --- Spiralling: helix protein, ER ribbon -----------------------------------
void buildSpiralling(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    const double turns = g.turns > 0.0 ? g.turns : 3.0;
    double amplitude = g.amplitude > 0.0 ? g.amplitude : 0.55;
    if (v == 5) amplitude = g.amplitude > 0.0 ? g.amplitude : 0.30;   // helix protein
    const int samples = 160;
    BioContour& c = ctx.newContour(subElementKeyForBand(kBandBackbone), ComponentKind::None,
                                   0, 0, false, false);
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
        const double u = -1.0 + 2.0 * t;
        const double vv = amplitude * std::sin(2.0 * kPi * turns * t);
        ctx.push(c, ctx.pointAt(u, vv), kPointCorner);
    }
    // A second offset ribbon gives the ER its lumen; the helix protein stays a
    // single coil, which is exactly what distinguishes the two presets in data.
    if (v != 5) {
        const double gap = g.leafletGap / 24.0;
        BioContour& c2 = ctx.newContour(subElementKeyForBand(kBandOutline), ComponentKind::None,
                                        0, 0, false, false);
        for (int i = 0; i < samples; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
            const double u = -1.0 + 2.0 * t;
            const double vv = amplitude * std::sin(2.0 * kPi * turns * t) + gap;
            ctx.push(c2, ctx.pointAt(u, vv), kPointCorner);
        }
    }
    drawComponentUnits(ctx, 0, true, 8);   // ribosome beads on rough ER
}

// --- Tube: Golgi stack ------------------------------------------------------
// Each cisterna is an open arc across the figure; the STACK COUNT is the
// component count, so 'same Golgi with one more layer' is a parameter edit.
void buildTube(GlyphContext& ctx) {
    if (ctx.paths.empty()) return;
    const std::vector<PlacedUnit> cisternae = expandUnits(*ctx.params, 0, ctx.paths[0]);
    const int samples = 32;
    for (size_t i = 0; i < cisternae.size(); ++i) {
        const PlacedUnit& u = cisternae[i];
        // The unit's own frame v coordinate, so the stack is positioned by the
        // component expansion and not by an ad-hoc per-glyph formula.
        const double frameV = (u.centre.y - ctx.centre.y) / (ctx.halfH > 1e-9 ? ctx.halfH : 1.0);
        BioContour& c = ctx.newContour(u.key, ComponentKind::Cisterna, u.componentIndex,
                                       u.unitIndex, false, false);
        for (int k = 0; k < samples; ++k) {
            const double t = static_cast<double>(k) / static_cast<double>(samples - 1);
            const double uu = -1.0 + 2.0 * t;
            // A gentle upward bow, so the stack reads as flattened sacs.
            ctx.push(c, ctx.pointAt(uu, frameV - 0.14 * std::sin(kPi * t)), kPointCorner);
        }
    }
    drawComponentUnits(ctx, 1, true, 12);   // budding vesicles
}

} // namespace
namespace {

// --- Pore: nuclear pore -----------------------------------------------------
// A ring of subunits around a membrane disc. `PoreSubunit` is a component, so the
// subunit count is editable and each subunit is separately colourable.
void buildPore(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    // The membrane disc the pore sits in.
    addRingPolygon(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None,
                   0, 0, 1.0, 0.55, 64, false);
    // The central channel.
    addFrameEllipse(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                    0, 0, 0.0, 0.0, 0.30, 0.28, 32, true);
    // Subunit ring.
    drawComponentUnits(ctx, 0, false, 10);
    if (v == 1) {
        // Basket fibrils on the nuclear side.
        const int fibrils = 8;
        for (int i = 0; i < fibrils; ++i) {
            const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(fibrils);
            const double u0 = 0.42 * std::cos(a), v0 = 0.26 * std::sin(a) - 0.30;
            addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, 0, 0,
                       u0, v0, u0 * 0.35, v0 - 0.55);
        }
        // Central transporter bead.
        addFrameEllipse(ctx, subElementKeyForBand(kBandLabel), ComponentKind::None,
                        0, 0, 0.0, 0.0, 0.14, 0.14, 20, true);
    }
    (void)g;
}

// --- Barrel: centriole ------------------------------------------------------
// A cylinder seen at a slight angle: an elliptical end face plus the barrel wall,
// with the triplet count coming from the component list.
void buildBarrel(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    (void)g;
    addRingPolygon(ctx, subElementKeyForBand(kBandBody), ComponentKind::None,
                   0, 0, 1.0, 0.42, 48, false);
    // Barrel wall: two side rails.
    addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
               -1.0, 0.42, -1.0, -0.42);
    addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
               1.0, 0.42, 1.0, -0.42);
    // The far end face, so the barrel reads as a tube and not a flat ring.
    addRingPolygon(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                   0, 0, 1.0, 0.42, 48, false);
    // Triplets around the near face.
    drawComponentUnits(ctx, 0, true, 8);
}

// --- Receptor: 7-TM serpentine, single-pass, dimer, ionotropic -------------
void buildReceptor(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    // Variant -> transmembrane segment count. The serpentine is DRAWN from the
    // count, so 'Receptor 2 has 5 TM helices' is data, not a different routine.
    static const int kTmByVariant[11] = { 7, 5, 7, 7, 1, 5, 1, 1, 7, 7, 1 };
    const int vi = (v >= 0 && v < 11) ? v : 0;
    int tm = kTmByVariant[vi];
    if (g.tmSegments > 0.0) tm = static_cast<int>(g.tmSegments + 0.5);
    if (tm < 1) tm = 1;
    if (tm > 12) tm = 12;

    // Domains above and below the membrane.
    addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
               -1.0, 0.0, 1.0, 0.0);
    const double step = 2.0 / static_cast<double>(tm);
    for (int i = 0; i < tm; ++i) {
        const double u0 = -1.0 + step * (static_cast<double>(i) + 0.5);
        const double v0 = (i % 2 == 0) ? -0.55 : 0.55;
        addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, i, 0,
                   u0, v0, u0, -v0);
        // Inter-segment loop on the side the segment ended.
        if (i + 1 < tm) {
            const double u1 = u0 + step;
            addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, i, 0,
                       u0, -v0, u1, -v0);
        }
    }
    // N / C terminal tails.
    addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
               -1.0, -0.55, -1.35, -0.75);
    addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
               1.0, (tm % 2 == 0 ? -0.55 : 0.55), 1.35, (tm % 2 == 0 ? -0.85 : 0.85));

    // Ligand, when the preset or the user asks for one. It is its own element so
    // 'no ligand' is a colour-hidden state and a genuine model value, not a
    // missing contour.
    const bool ligand = (g.ligandPresent > 0.5) ||
                        (vi == 2) || (vi == 8);
    if (ligand) {
        addFrameEllipse(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                        0, 0, -0.35, 0.85, 0.16, 0.16, 20, true);
    }
    // Bound beads (G protein, scaffold, second messenger) are a component.
    drawComponentUnits(ctx, 0, true, 10);
}

// --- Channel: ion channel ---------------------------------------------------
void buildChannel(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    static const int kTmByVariant[13] = { 6, 4, 4, 4, 2, 6, 4, 6, 6, 6, 6, 6, 6 };
    const int vi = (v >= 0 && v < 13) ? v : 0;
    int tm = kTmByVariant[vi];
    if (g.tmSegments > 0.0) tm = static_cast<int>(g.tmSegments + 0.5);
    if (tm < 2) tm = 2;

    // Membrane leaflets.
    addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
               -1.0, -0.30, 1.0, -0.30);
    addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
               -1.0, 0.30, 1.0, 0.30);
    // Transmembrane helices, packed either side of the pore.
    const double step = 0.34;
    for (int i = 0; i < tm; ++i) {
        const double u0 = -0.5 * step * static_cast<double>(tm - 1) + step * static_cast<double>(i);
        addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, i, 0,
                   u0, -0.42, u0, 0.42);
    }
    // The pore itself: a gap in the middle of the membrane.
    addFrameEllipse(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                    0, 0, 0.0, 0.0, 0.10, 0.34, 24, true);
    // Selectivity filter on the upper face.
    addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
               -0.22, -0.55, 0.22, -0.55);

    // Ions in the filter, a plug/ball, an auxiliary subunit, lipids, a toxin:
    // all of these are COMPONENTS, so their number is data.
    drawComponentUnits(ctx, 0, true, 10);
    if (vi == 11) {
        // Ball-and-chain plug on the cytosolic mouth.
        addFrameEllipse(ctx, subElementKeyForBand(kBandLabel), ComponentKind::None,
                        0, 0, 0.0, 0.92, 0.13, 0.13, 18, true);
        addSegment(ctx, subElementKeyForBand(kBandLabel), ComponentKind::None, 0, 0,
                   0.0, 0.62, 0.0, 0.78);
    }
    if (vi == 8) {
        // Auxiliary beta subunit to the right of the pore.
        addFrameEllipse(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None,
                        0, 0, 0.72, 0.0, 0.16, 0.42, 24, false);
    }
    (void)g;
}

} // namespace
namespace {

// --- Antibody: immunoglobin variants ----------------------------------------
// Two heavy and two light chains with disulfide bridges. `disulfides` and
// `chainLength` are parameters, and the antigen beads are components.
void buildAntibody(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    int disulfides = g.disulfides > 0.0 ? static_cast<int>(g.disulfides + 0.5) : 2;
    if (disulfides < 0) disulfides = 0;
    if (disulfides > 8) disulfides = 8;

    // v == 0: full Y.  v == 1: Fab only.  v == 2: Fc only.  v == 4: scFv.
    const bool full = (v == 0 || v == 3);
    const bool fab  = (v == 1);
    const bool fc   = (v == 2);
    const bool scfv = (v == 4);

    // Heavy chains: the two arms of the Y.
    if (full || fab) {
        addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
                   0.0, 0.0, -0.62, -0.72);
        addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
                   0.0, 0.0, 0.62, -0.72);
    }
    if (full || fab || scfv) {
        // Light chains beside each heavy arm.
        addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
                   -0.22, -0.18, -0.86, -0.64);
        addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
                   0.22, -0.18, 0.86, -0.64);
    }
    // Fc stem.
    if (full || fc) {
        addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
                   0.0, 0.0, 0.0, 0.86);
    }
    // scFv linker.
    if (scfv) {
        addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, 0, 0,
                   -0.62, -0.72, 0.62, -0.72);
    }
    // Disulfide bridges between the arms.
    for (int i = 0; i < disulfides; ++i) {
        const double u = -0.30 + 0.10 * i;
        const double vv = -0.58 + 0.06 * i;
        addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, 0,
                   static_cast<uint32_t>(i), u, vv, vv * 0.0 + u + 0.30, vv);
    }
    // Antigen beads at the Fab tips (component 0).
    drawComponentUnits(ctx, 0, true, 12);
    (void)g;
}

// --- DoubleHelix: the DNA SCHEMATIC -----------------------------------------
// THIS IS AN ILLUSTRATION OF A DOUBLE HELIX, NOT A CHEMICAL STRUCTURE. It has no
// bases, no sequence and no chemistry: it is two sinusoidal backbones joined by
// `basePairs` rungs. Base-pair count, radius and rise are all editable.
void buildDoubleHelix(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const double pairs = g.basePairs > 0.0 ? g.basePairs : 12.0;
    const double turns = g.turns > 0.0 ? g.turns : 2.0;
    const double amp = g.amplitude > 0.0 ? g.amplitude : 1.0;
    const int samples = 200;

    // Two backbones, opposite phase.
    for (int strand = 0; strand < 2; ++strand) {
        const double phase = (strand == 0) ? 0.0 : kPi;
        BioContour& c = ctx.newContour(subElementKeyForBand(kBandBackbone),
                                       ComponentKind::None, static_cast<uint32_t>(strand), 0,
                                       false, false);
        for (int i = 0; i < samples; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
            const double u = -1.0 + 2.0 * t;
            const double vv = amp * std::sin(2.0 * kPi * turns * t + phase);
            ctx.push(c, ctx.pointAt(u, vv), kPointCorner);
        }
    }
    // Rungs. They are a COMPONENT, so the base-pair count is user data and each
    // rung has its own key and can be coloured separately.
    const int rungs = static_cast<int>(pairs + 0.5);
    for (int i = 0; i < rungs && i < kMaxComponentCount; ++i) {
        const double t = (rungs == 1) ? 0.5 : static_cast<double>(i) / static_cast<double>(rungs - 1);
        const double u = -1.0 + 2.0 * t;
        const double v0 = amp * std::sin(2.0 * kPi * turns * t);
        const double v1 = amp * std::sin(2.0 * kPi * turns * t + kPi);
        const uint32_t key = subElementKeyForUnit(0u, static_cast<uint32_t>(i));
        BioContour& c = ctx.newContour(key, ComponentKind::BasePair, 0,
                                       static_cast<uint32_t>(i), false, false);
        ctx.push(c, ctx.pointAt(u, v0), kPointCorner);
        ctx.push(c, ctx.pointAt(u, v1), kPointCorner);
    }
    (void)g;
}

// --- Cloverleaf: tRNA secondary structure -----------------------------------
void buildCloverleaf(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    // The backbone: an acceptor stem up the middle, three loops, one anticodon
    // loop at the bottom.
    const double stem[5][2] = { { 0.0, 1.0 }, { -0.18, 0.30 }, { 0.0, 0.30 },
                                { 0.18, 0.30 }, { 0.0, 1.0 } };
    (void)stem;
    addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
               -0.10, 1.00, -0.10, 0.35);
    addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
               0.10, 1.00, 0.10, 0.35);
    // The three loops across the top.
    const double loopCentres[3][2] = { { -0.62, -0.10 }, { 0.0, -0.45 }, { 0.62, -0.10 } };
    for (int i = 0; i < 3; ++i) {
        addFrameEllipse(ctx, subElementKeyForBand(kBandOutline), ComponentKind::Nucleotide,
                        0, static_cast<uint32_t>(i), loopCentres[i][0], loopCentres[i][1],
                        0.26, 0.22, 24, false);
    }
    // Anticodon stem and loop at the bottom.
    addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, 0, 0,
               -0.12, 0.35, -0.12, -0.62);
    addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, 0, 0,
               0.12, 0.35, 0.12, -0.62);
    addFrameEllipse(ctx, subElementKeyForBand(kBandOutline), ComponentKind::Nucleotide,
                    0, 3, 0.0, -0.80, 0.22, 0.20, 24, false);
    // Nucleotide beads around the loops: a component, so their count is editable.
    drawComponentUnits(ctx, 0, true, 8);
    (void)g;
}

// --- Strand: mRNA -----------------------------------------------------------
void buildStrand(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const double amp = g.amplitude > 0.0 ? g.amplitude : 0.35;
    const HostPath& path = ctx.paths[0];
    const int samples = 120;
    BioContour& c = ctx.newContour(subElementKeyForBand(kBandBackbone), ComponentKind::None,
                                   0, 0, false, false);
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
        const core::Vec2 p = path.pointAt(t);
        const core::Vec2 tan = path.tangentAt(t);
        const core::Vec2 normal(-tan.y, tan.x);
        const double wobble = amp * ctx.halfH * std::sin(6.0 * kPi * t);
        ctx.push(c, core::Vec2(p.x + normal.x * wobble, p.y + normal.y * wobble), kPointCorner);
    }
    drawComponentUnits(ctx, 0, true, 10);   // codon beads
    (void)g;
}

// --- Ribosome: subunit pair -------------------------------------------------
void buildRibosome(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    if (v == 4) {
        // Large subunit only.
        addFrameEllipse(ctx, subElementKeyForBand(kBandBody), ComponentKind::None,
                        0, 0, 0.0, 0.0, 0.85, 0.75, 48, true);
    } else {
        // Large subunit (lower) and small subunit (upper), 50S/30S proportions.
        addFrameEllipse(ctx, subElementKeyForBand(kBandBody), ComponentKind::None,
                        0, 0, 0.0, 0.35, 0.85, 0.60, 48, true);
        addFrameEllipse(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                        0, 0, 0.0, -0.42, 0.62, 0.40, 48, true);
    }
    // A threaded mRNA strand.
    if (v == 1 || v == 2) {
        addSegment(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None, 0, 0,
                   -1.0, -0.05, 1.0, -0.05);
    }
    // A nascent peptide chain.
    if (v == 3) {
        BioContour& c = ctx.newContour(subElementKeyForBand(kBandOutline), ComponentKind::None,
                                       0, 0, false, false);
        for (int i = 0; i < 40; ++i) {
            const double t = static_cast<double>(i) / 39.0;
            ctx.push(c, ctx.pointAt(0.55 + 0.35 * std::sin(4.0 * kPi * t),
                                    0.35 - 1.10 * t), kPointCorner);
        }
    }
    // Ribosome beads / tRNA beads / polysome copies: a component.
    drawComponentUnits(ctx, 0, true, 10);
    (void)g;
}

} // namespace
namespace {

// --- Virus: capsid + spikes, enveloped, filamentous -------------------------
void buildVirus(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    int faces = g.capsidFaces > 0.0 ? static_cast<int>(g.capsidFaces + 0.5) : 0;
    if (faces <= 0) faces = (v == 0) ? 6 : (v == 1 || v == 3) ? 0 : 0;

    if (v == 2) {
        // Filamentous virus: a long wavy rod, no polygonal capsid.
        BioContour& c = ctx.newContour(subElementKeyForBand(kBandBody), ComponentKind::None,
                                       0, 0, false, false);
        const int samples = 64;
        for (int i = 0; i < samples; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
            ctx.push(c, ctx.pointAt(-1.0 + 2.0 * t, 0.45 * std::sin(3.0 * kPi * t)),
                     kPointCorner);
        }
        drawComponentUnits(ctx, 0, true, 8);
        return;
    }

    if (v == 3) {
        // HIV: an envelope, a conical core and gp120 spikes.
        addRingPolygon(ctx, subElementKeyForBand(kBandBody), ComponentKind::None,
                       0, 0, 1.0, 1.0, 56, false);
        addRingPolygon(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None,
                       0, 0, 0.90, 0.90, 56, false);
        // Conical core: a triangle-ish wedge inside.
        const double core[4][2] = { { -0.10, 0.55 }, { 0.22, -0.10 }, { -0.22, -0.10 },
                                    { -0.10, 0.55 } };
        addFramePolygon(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                        0, 0, core, 3, true);
        drawComponentUnits(ctx, 0, true, 8);   // spikes
        return;
    }

    if (v == 1) {
        // Enveloped virus: capsid inside a bilayer envelope.
        addRingPolygon(ctx, subElementKeyForBand(kBandBody), ComponentKind::None,
                       0, 0, 1.0, 1.0, 56, false);
        addRingPolygon(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None,
                       0, 0, 0.86, 0.86, 56, false);
    }
    // Capsid polygon (a hexagon for the plain virus).
    if (faces >= 3) {
        addRingPolygon(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                       0, 0, 0.72, 0.72, faces, true);
    } else {
        addRingPolygon(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                       0, 0, 0.72, 0.72, 48, true);
    }
    drawComponentUnits(ctx, 0, true, 8);   // spikes
    (void)g;
}

// --- Bacteriophage ----------------------------------------------------------
void buildBacteriophage(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    // Icosahedral head.
    addRingPolygon(ctx, subElementKeyForBand(kBandBody), ComponentKind::None,
                   0, 0, 0.55, 0.52, 6, true);
    addSegment(ctx, subElementKeyForBand(kBandOutline), ComponentKind::None, 0, 0,
               -0.55, -0.30, 0.55, -0.30);
    // Tail sheath and base plate.
    addSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::None, 0, 0,
               0.0, 0.50, 0.0, 1.00);
    addRingPolygon(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                   0, 0, 0.18, 0.10, 20, false);
    // Tail fibres: a COMPONENT, so their number is editable.
    const int fibres = g.spikes > 0.0 ? static_cast<int>(g.spikes + 0.5) : 6;
    for (int i = 0; i < fibres && i < 64; ++i) {
        const double a = kPi * (static_cast<double>(i) / static_cast<double>(fibres - 1 ? fibres - 1 : 1));
        const double tipU = 0.0 + 0.65 * std::cos(a);
        const double tipV = 1.00 + 0.38 * std::sin(a);
        addSegment(ctx, subElementKeyForUnit(0u, static_cast<uint32_t>(i)), ComponentKind::Spike,
                   0, static_cast<uint32_t>(i), 0.0, 1.00, tipU, tipV);
    }
}

// --- Star -------------------------------------------------------------------
void buildStar(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    int points = g.nodes > 0.0 ? static_cast<int>(g.nodes + 0.5) : 5;
    if (points < 3) points = 3;
    if (points > 64) points = 64;
    const double inner = (g.innerRatio > 0.0 && g.innerRatio < 1.0) ? g.innerRatio : 0.45;
    BioContour& c = ctx.newContour(subElementKeyForBand(kBandBody), ComponentKind::None,
                                   0, 0, true, true);
    for (int i = 0; i < points * 2; ++i) {
        const double a = kPi * static_cast<double>(i) / static_cast<double>(points);
        const double r = (i % 2 == 0) ? 1.0 : inner;
        ctx.push(c, ctx.pointAt(r * std::cos(a), r * std::sin(a)), kPointCorner);
    }
}

// --- Polygon: circle, ball, triangle, rectangle, hexagon, octagon -----------
void buildPolygon(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    const int v = ctx.params->variant;
    // Variant -> default vertex count. A circle is a fine polygon: drawing it as
    // a sampled ellipse would make 'circle' and '64-gon' two different code paths
    // for the same shape, and the node count could not be edited.
    static const int kNodesByVariant[6] = { 64, 64, 3, 4, 6, 8 };
    const int vi = (v >= 0 && v < 6) ? v : 0;
    int nodes = kNodesByVariant[vi];
    if (g.nodes >= 3.0) nodes = static_cast<int>(g.nodes + 0.5);
    if (nodes < 3) nodes = 3;
    if (nodes > 256) nodes = 256;
    const double frac = (vi == 3) ? 0.0 : g.cornerFraction;
    if (vi == 3) {
        // Rectangle: the vertex count must be 4 for it to be a rectangle, so the
        // node parameter is NOT allowed to override it here.
        addRoundRect(ctx, subElementKeyForBand(kBandBody), ComponentKind::None, 0, 0,
                     0.0, true);
        return;
    }
    if (frac > 0.02 && vi != 0 && vi != 1) {
        addRoundRect(ctx, subElementKeyForBand(kBandBody), ComponentKind::None, 0, 0,
                     frac, true);
        return;
    }
    addRingPolygon(ctx, subElementKeyForBand(kBandBody), ComponentKind::None,
                   0, 0, 1.0, 1.0, nodes, true);
    if (vi == 1) {
        // Ball: a highlight lobe, its own element so it can be coloured.
        addFrameEllipse(ctx, subElementKeyForBand(kBandNucleus), ComponentKind::None,
                        0, 0, -0.32, -0.32, 0.30, 0.26, 24, true);
    }
}

// --- Cloud ------------------------------------------------------------------
void buildCloud(GlyphContext& ctx) {
    const BioGlyphParams& g = ctx.params->glyphParams;
    int scallops = g.nodes > 0.0 ? static_cast<int>(g.nodes + 0.5) : 9;
    if (scallops < 4) scallops = 4;
    if (scallops > 64) scallops = 64;
    const double depth = g.amplitude > 0.0 ? g.amplitude : 0.18;
    // Scallops are drawn as quadratic arcs between the base points, published with
    // the kPointQuad convention so the shared T03 flattener renders them as true
    // curves. A cloud built from straight segments would read as a gear.
    const double base[3][2] = { { 0.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 0.0 } };
    (void)base;
    for (int i = 0; i < scallops; ++i) {
        const double a0 = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(scallops);
        const double a1 = 2.0 * kPi * static_cast<double>(i + 1) / static_cast<double>(scallops);
        const double am = 0.5 * (a0 + a1);
        const double u0 = std::cos(a0), v0 = std::sin(a0);
        const double u1 = std::cos(a1), v1 = std::sin(a1);
        const double bulge = 1.0 + depth * 1.6;
        const double cu = bulge * std::cos(am), cv = bulge * std::sin(am);
        addQuad(ctx, subElementKeyForBand(kBandBody), ComponentKind::None, 0, 0,
                u0, v0, cu, cv, u1, v1);
    }
    // Close the ring so a filled cloud has no gap at the seam.
    BioSubElement& body = ctx.element(subElementKeyForBand(kBandBody), ComponentKind::None, 0, 0);
    if (!body.contours.empty()) body.contours.front().closed = true;
}

} // namespace
namespace {

// --- Plasmid: a circular base-range map -------------------------------------
// THE ONE GLYPH WHOSE CONTENT IS DATA. It draws: an outer backbone ring, one
// radial TRACK per feature, an arc per feature derived from its BASE RANGE, and
// a tick per feature start. Every one of those is derived from PlasmidData on
// this call -- nothing about a marker's position is cached, which is why moving a
// marker cannot leave a stale arc behind.
void buildPlasmid(GlyphContext& ctx) {
    const PlasmidData& data = ctx.params->plasmid;
    if (data.baseCount <= 0) {
        // A plasmid with no base count is not drawable. It gets an empty ring and
        // NOTHING else -- inventing a default circle would hide the missing data.
        addRingPolygon(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None,
                       0, 0, 1.0, 1.0, 64, false);
        return;
    }

    // Outer backbone.
    addRingPolygon(ctx, subElementKeyForBand(kBandBackbone), ComponentKind::None,
                   0, 0, 1.0, 1.0, 128, false);

    // Base ticks: a component, so the tick count is editable. They are drawn on
    // the backbone and each has its own key.
    drawComponentUnits(ctx, 0, false, 6);

    const double trackStep = 0.18;
    for (size_t i = 0; i < data.features.size(); ++i) {
        const PlasmidFeature& f = data.features[i];
        const FeatureAngle a = featureAngleSpan(data, f);
        const double rOuter = 0.92 - trackStep * f.track;
        const double rInner = rOuter - trackStep * 0.85;
        if (rInner < 0.15) continue;   // out of radial room; skip, do not overlap
        // Track band key: the radial track is the colour identity for the REGION,
        // and the unit key carries the FEATURE INDEX so two markers on the same
        // track are still separately colourable.
        const uint32_t key = subElementKeyForUnit(static_cast<uint32_t>(i) + 1u, 0u);

        const int samples = 96;
        // An arrow-shaped marker (kind == 1) is drawn as a filled wedge; a region
        // (kind == 0) as an open band.
        BioContour& c = ctx.newContour(key, f.kind == 1 ? ComponentKind::Marker
                                                       : ComponentKind::Region,
                                       static_cast<uint32_t>(i), 0, true, f.kind == 1);
        // One edge out, the other back, so the band is closed even when it wraps.
        for (int k = 0; k < samples; ++k) {
            const double t = static_cast<double>(k) / static_cast<double>(samples - 1);
            const double angle = deg2rad(a.startDeg + a.spanDeg * t);
            ctx.push(c, ctx.pointAt(rOuter * std::cos(angle), -rOuter * std::sin(angle)),
                     kPointCorner);
        }
        for (int k = samples - 1; k >= 0; --k) {
            const double t = static_cast<double>(k) / static_cast<double>(samples - 1);
            const double angle = deg2rad(a.startDeg + a.spanDeg * t);
            ctx.push(c, ctx.pointAt(rInner * std::cos(angle), -rInner * std::sin(angle)),
                     kPointCorner);
        }

        // Start tick, so a one-base marker is still visible.
        const double tickAngle = deg2rad(a.startDeg);
        addAbsSegment(ctx, subElementKeyForBand(kBandRungs), ComponentKind::Marker,
                      static_cast<uint32_t>(i), 0,
                      core::Vec2(ctx.centre.x + ctx.halfW * rOuter * std::cos(tickAngle),
                                 ctx.centre.y - ctx.halfH * rOuter * std::sin(tickAngle)),
                      core::Vec2(ctx.centre.x + ctx.halfW * (rOuter + 0.08) * std::cos(tickAngle),
                                 ctx.centre.y - ctx.halfH * (rOuter + 0.08) * std::sin(tickAngle)));

        // The label anchor is a zero-size element carrying the name, so a
        // consumer can place text without recomputing the angle.
        const double labelAngle = deg2rad(featureLabelAngleDeg(data, f));
        const uint32_t labelKey = subElementKeyForBand(kBandLabel);
        BioContour& lc = ctx.newContour(labelKey, ComponentKind::Marker,
                                        static_cast<uint32_t>(i), 0, false, false);
        ctx.push(lc, ctx.pointAt((rOuter + 0.14) * std::cos(labelAngle),
                                 -(rOuter + 0.14) * std::sin(labelAngle)), kPointCorner);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Dispatch -- THE ONLY place a glyph is selected
// ---------------------------------------------------------------------------
double resolveStrokeWidth(const BioShapeParams& params, double documentLineWidth) {
    if (params.style.width > 0.0) return params.style.width;
    if (documentLineWidth > 0.0) return documentLineWidth;
    return 0.6;   // project default, same value T11 falls back to
}

std::vector<HostPath> glyphHostPaths(const BioShapeParams& params) {
    // Paths are built in FRAME coordinates and then mapped to document space, so
    // a rotation or a non-uniform stretch moves them with the figure by
    // construction. The number and meaning of paths is a property of the glyph.
    std::vector<HostPath> paths;
    BioShapeParams probe = params;
    core::Vec2 axisU, axisV;
    frameAxes(probe, &axisU, &axisV);
    const double halfW = core::length(axisU);
    const double halfH = core::length(axisV);
    // Helper: a frame point to document space.
    struct Local {
        static core::Vec2 map(const BioShapeParams& p, double u, double v) {
            return framePoint(p, u, v);
        }
    };
    const GlyphKind g = params.glyph;

    switch (g) {
        case GlyphKind::Bilayer: {
            HostPath p;
            if (params.variant == 1) {
                // Membrane ARC: a shallow arc across the figure.
                p.kind = PathKind::Arc;
                p.centre = Local::map(params, 0.0, 0.35);
                p.radius = halfW * 1.35;
                p.radiusV = halfW * 1.35;
                p.startDeg = 200.0;
                p.sweepDeg = 140.0;
            } else {
                p.kind = PathKind::Straight;
                p.start = Local::map(params, -1.0, 0.0);
                p.end   = Local::map(params, 1.0, 0.0);
            }
            paths.push_back(p);
            break;
        }
        case GlyphKind::Vesicle: {
            HostPath p;
            p.kind = PathKind::Ellipse;
            p.centre = params.centre;
            p.radius = halfW;
            p.radiusV = halfH;
            p.startDeg = params.rotationDeg;
            paths.push_back(p);
            break;
        }
        case GlyphKind::Pore: {
            HostPath p;
            p.kind = PathKind::Ring;
            p.centre = params.centre;
            p.radius = halfW * 0.62;
            p.startDeg = params.rotationDeg;
            paths.push_back(p);
            break;
        }
        case GlyphKind::Barrel: {
            HostPath p;
            p.kind = PathKind::Ellipse;
            p.centre = Local::map(params, 0.0, -0.42);
            p.radius = halfW * 0.94;
            p.radiusV = halfW * 0.94 * 0.42;
            paths.push_back(p);
            break;
        }
        case GlyphKind::Capsule: {
            // Cristae (component 0) across the body; beads (1); nucleoid (2).
            HostPath cristae;
            cristae.kind = PathKind::Straight;
            cristae.start = Local::map(params, -0.85, 0.0);
            cristae.end   = Local::map(params, 0.85, 0.0);
            paths.push_back(cristae);
            HostPath beads;
            beads.kind = PathKind::Straight;
            beads.start = Local::map(params, -0.6, 0.45);
            beads.end   = Local::map(params, 0.6, 0.45);
            paths.push_back(beads);
            HostPath nucleoid;
            nucleoid.kind = PathKind::Straight;
            nucleoid.start = Local::map(params, -0.55, 0.0);
            nucleoid.end   = Local::map(params, 0.55, 0.0);
            paths.push_back(nucleoid);
            break;
        }
        case GlyphKind::Tube: {
            HostPath cisternae;
            cisternae.kind = PathKind::Straight;
            cisternae.start = Local::map(params, 0.0, -0.85);
            cisternae.end   = Local::map(params, 0.0, 0.85);
            paths.push_back(cisternae);
            HostPath vesicles;
            vesicles.kind = PathKind::Arc;
            vesicles.centre = Local::map(params, 0.0, 1.05);
            vesicles.radius = halfW * 0.75;
            vesicles.radiusV = halfW * 0.75;
            vesicles.startDeg = 180.0;
            vesicles.sweepDeg = 180.0;
            paths.push_back(vesicles);
            break;
        }
        case GlyphKind::DoubleHelix: {
            // Base-pair rungs sit along the helix axis; the axis is the frame +x.
            HostPath rungs;
            rungs.kind = PathKind::Straight;
            rungs.start = Local::map(params, -1.0, 0.0);
            rungs.end   = Local::map(params, 1.0, 0.0);
            paths.push_back(rungs);
            break;
        }
        case GlyphKind::Cloverleaf: {
            HostPath beads;
            beads.kind = PathKind::Ring;
            beads.centre = Local::map(params, 0.0, -0.45);
            beads.radius = halfW * 0.5;
            paths.push_back(beads);
            break;
        }
        case GlyphKind::Strand: {
            HostPath beads;
            beads.kind = PathKind::Straight;
            beads.start = Local::map(params, -0.95, 0.0);
            beads.end   = Local::map(params, 0.95, 0.0);
            paths.push_back(beads);
            break;
        }
        case GlyphKind::Ribosome: {
            HostPath beads;
            beads.kind = PathKind::Straight;
            beads.start = Local::map(params, -0.7, 0.30);
            beads.end   = Local::map(params, 0.7, 0.30);
            paths.push_back(beads);
            break;
        }
        case GlyphKind::Blob: {
            HostPath beads;
            beads.kind = PathKind::Ellipse;
            beads.centre = Local::map(params, 0.0, -0.62);
            beads.radius = halfW * 0.35;
            beads.radiusV = halfW * 0.35;
            paths.push_back(beads);
            break;
        }
        case GlyphKind::Spiralling: {
            HostPath beads;
            beads.kind = PathKind::Straight;
            beads.start = Local::map(params, -0.9, -0.55);
            beads.end   = Local::map(params, 0.9, -0.55);
            paths.push_back(beads);
            break;
        }
        case GlyphKind::Receptor:
        case GlyphKind::Channel: {
            HostPath beads;
            beads.kind = PathKind::Straight;
            beads.start = Local::map(params, -0.8, -0.95);
            beads.end   = Local::map(params, 0.8, -0.95);
            paths.push_back(beads);
            break;
        }
        case GlyphKind::Antibody: {
            HostPath beads;
            beads.kind = PathKind::Straight;
            beads.start = Local::map(params, -0.85, -0.80);
            beads.end   = Local::map(params, 0.85, -0.80);
            paths.push_back(beads);
            break;
        }
        case GlyphKind::Virus: {
            HostPath spikes;
            spikes.kind = PathKind::Ellipse;
            spikes.centre = params.centre;
            spikes.radius = halfW;
            spikes.radiusV = halfH;
            paths.push_back(spikes);
            break;
        }
        case GlyphKind::Plasmid: {
            HostPath ticks;
            ticks.kind = PathKind::Ring;
            ticks.centre = params.centre;
            ticks.radius = halfW;
            paths.push_back(ticks);
            break;
        }
        default:
            break;
    }
    (void)probe;
    return paths;
}
BioGeometry buildGlyphGeometry(const BioShapeParams& params, double documentLineWidth) {
    BioGeometry out;
    // NORMALISE first, so a caller that passes count = 5000 gets exactly the
    // geometry the model would persist, not a different one.
    bool clamped = false;
    out.params = normalizeComponents(params, &clamped);
    out.strokeWidth = resolveStrokeWidth(out.params, documentLineWidth);

    GlyphContext ctx;
    ctx.params = &out.params;
    ctx.centre = out.params.centre;
    ctx.stroke = out.strokeWidth;
    frameAxes(out.params, &ctx.axisU, &ctx.axisV);
    ctx.halfW = core::length(ctx.axisU);
    ctx.halfH = core::length(ctx.axisV);
    ctx.paths = glyphHostPaths(out.params);

    // A degenerate frame cannot be drawn; return an EMPTY geometry rather than a
    // zero-size one, so a caller can tell 'nothing to draw' from 'drew a dot'.
    if (!(ctx.halfW > 1e-9) || !(ctx.halfH > 1e-9)) return out;

    switch (out.params.glyph) {
        case GlyphKind::Blob:          buildBlob(ctx); break;
        case GlyphKind::Bilayer:       buildBilayer(ctx); break;
        case GlyphKind::Vesicle:       buildVesicle(ctx); break;
        case GlyphKind::Capsule:       buildCapsule(ctx); break;
        case GlyphKind::Rod:           buildRod(ctx); break;
        case GlyphKind::Spiralling:    buildSpiralling(ctx); break;
        case GlyphKind::Tube:          buildTube(ctx); break;
        case GlyphKind::Pore:          buildPore(ctx); break;
        case GlyphKind::Barrel:        buildBarrel(ctx); break;
        case GlyphKind::BarrelPore:    buildChannel(ctx); break;   // shares the gate drawing
        case GlyphKind::YShape:        buildAntibody(ctx); break;  // degenerate Y == Fab
        case GlyphKind::Antibody:      buildAntibody(ctx); break;
        case GlyphKind::Receptor:      buildReceptor(ctx); break;
        case GlyphKind::Channel:       buildChannel(ctx); break;
        case GlyphKind::DoubleHelix:   buildDoubleHelix(ctx); break;
        case GlyphKind::Cloverleaf:    buildCloverleaf(ctx); break;
        case GlyphKind::Strand:        buildStrand(ctx); break;
        case GlyphKind::Ribosome:      buildRibosome(ctx); break;
        case GlyphKind::Star:          buildStar(ctx); break;
        case GlyphKind::Polygon:       buildPolygon(ctx); break;
        case GlyphKind::Cloud:         buildCloud(ctx); break;
        case GlyphKind::Virus:         buildVirus(ctx); break;
        case GlyphKind::Bacteriophage: buildBacteriophage(ctx); break;
        case GlyphKind::Plasmid:       buildPlasmid(ctx); break;
        case GlyphKind::Count:         break;
    }

    out.elements = ctx.elements;
    out.unitCount = totalUnitCount(out.params);
    if (ctx.haveInk) out.inkBounds = ctx.ink;
    else out.inkBounds = core::Box(out.params.centre, out.params.centre);
    // The hit box inflates the ink box by half the stroke width: a stroke is
    // centred on its path, so the visible extent is the path extent PLUS half a
    // width on every side. Doing this here means every consumer agrees.
    const double inflate = 0.5 * out.strokeWidth;
    out.hitBounds = core::Box(core::Vec2(out.inkBounds.min.x - inflate,
                                         out.inkBounds.min.y - inflate),
                              core::Vec2(out.inkBounds.max.x + inflate,
                                         out.inkBounds.max.y + inflate));
    return out;
}

// ---------------------------------------------------------------------------
// Bounds (cheap, conservative)
// ---------------------------------------------------------------------------
core::Box bioShapeBounds(const BioShapeParams& params, double documentLineWidth) {
    // The full geometry is built because it is O(units) and the model already
    // caps units; a cheaper approximation would have to duplicate every glyph's
    // extent rule and would then be able to disagree with the renderer.
    const BioGeometry g = buildGlyphGeometry(params, documentLineWidth);
    return g.hitBounds;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------
namespace {

double distanceToSegment(const core::Vec2& p, const core::Vec2& a, const core::Vec2& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared < 1e-18) {
        const double ex = p.x - a.x, ey = p.y - a.y;
        return std::sqrt(ex * ex + ey * ey);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double cx = a.x + t * dx, cy = a.y + t * dy;
    const double ex = p.x - cx, ey = p.y - cy;
    return std::sqrt(ex * ex + ey * ey);
}

double distanceToContour(const core::Vec2& p, const BioContour& c) {
    if (c.points.empty()) return 1e18;
    if (c.points.size() == 1) {
        const double dx = p.x - c.points[0].x, dy = p.y - c.points[0].y;
        return std::sqrt(dx * dx + dy * dy);
    }
    double best = 1e18;
    const size_t count = c.points.size();
    const size_t last = c.closed ? count : count - 1;
    for (size_t i = 0; i < last; ++i) {
        const core::Vec2& a = c.points[i];
        const core::Vec2& b = c.points[(i + 1) % count];
        const double d = distanceToSegment(p, a, b);
        if (d < best) best = d;
    }
    return best;
}

// Even-odd point-in-contour, used only for FILLED contours. A membrane is not
// filled, so clicking the empty space inside a membrane ring does not select it,
// which is what a user expects.
bool pointInContour(const core::Vec2& p, const BioContour& c) {
    if (c.points.size() < 3) return false;
    bool inside = false;
    const size_t count = c.points.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const core::Vec2& a = c.points[i];
        const core::Vec2& b = c.points[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

} // namespace

BioHitResult hitTestBioShape(const BioGeometry& geometry, const core::Vec2& point,
                             double tolerance) {
    BioHitResult result;
    if (tolerance < 0.0) tolerance = 0.0;
    const double radius = tolerance + 0.5 * geometry.strokeWidth;
    double best = 1e18;
    for (size_t i = 0; i < geometry.elements.size(); ++i) {
        const BioSubElement& el = geometry.elements[i];
        for (size_t k = 0; k < el.contours.size(); ++k) {
            const BioContour& c = el.contours[k];
            const double d = distanceToContour(point, c);
            if (d <= radius && d < best) {
                best = d;
                result.hit = true;
                result.key = el.key;
                result.distance = d;
                result.elementIndex = i;
            }
            // A filled contour is also hit strictly inside.
            if (c.filled && pointInContour(point, c) && 0.0 < best) {
                best = 0.0;
                result.hit = true;
                result.key = el.key;
                result.distance = 0.0;
                result.elementIndex = i;
            }
        }
    }
    return result;
}
// ---------------------------------------------------------------------------
// Non-destructive transform
// ---------------------------------------------------------------------------
// Scaling edits the PARAMETERS. The unit COUNT is deliberately untouched: a
// membrane with 17 lipids still has 17 lipids after a stretch, because thinning
// the count is the 'scale destroyed the structure' failure the task forbids.
core::Error scaleBioShape(BioShapeParams* params, const core::Vec2& pivot,
                          double factorU, double factorV, bool* changed) {
    if (changed) *changed = false;
    if (!params) return core::Error(core::ErrorCode::OutOfRange, "null params");
    if (!(std::fabs(factorU) > 1e-12) || !(std::fabs(factorV) > 1e-12))
        return core::Error(core::ErrorCode::InvalidArgument, "zero scale factor");

    // The centre moves relative to the pivot.
    params->centre.x = pivot.x + (params->centre.x - pivot.x) * factorU;
    params->centre.y = pivot.y + (params->centre.y - pivot.y) * factorV;
    // The frame axes stretch with their OWN axis, which is what makes a
    // non-uniform stretch an ellipse rather than a scaled circle.
    params->axisU.x *= factorU;
    params->axisU.y *= factorU;
    params->axisV.x *= factorV;
    params->axisV.y *= factorV;
    params->scale *= std::sqrt(std::fabs(factorU * factorV));

    // A repeated unit's own size scales by the GEOMETRIC MEAN, so a stretched
    // membrane has slightly oval heads instead of absurdly stretched ones while
    // the count stays exactly what the user asked for.
    const double unitFactor = std::sqrt(std::fabs(factorU * factorV));
    for (size_t i = 0; i < params->components.size(); ++i) {
        if (params->components[i].kind == ComponentKind::Marker ||
            params->components[i].kind == ComponentKind::Region) continue;  // plasmid: bases, not length
        params->components[i].unitSize *= unitFactor;
        params->components[i].spacing *= unitFactor;
    }
    params->glyphParams.headRadius *= unitFactor;
    params->glyphParams.tailLength *= unitFactor;
    params->glyphParams.subunitRadius *= unitFactor;
    params->glyphParams.helixRadius *= unitFactor;
    params->glyphParams.risePerTurn *= unitFactor;
    params->glyphParams.leafletGap *= unitFactor;
    if (changed) *changed = true;
    return core::Error::success();
}

core::Error rotateBioShape(BioShapeParams* params, const core::Vec2& pivot,
                           double degrees) {
    if (!params) return core::Error(core::ErrorCode::OutOfRange, "null params");
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(radians), s = std::sin(radians);
    const double dx = params->centre.x - pivot.x;
    const double dy = params->centre.y - pivot.y;
    params->centre.x = pivot.x + dx * c - dy * s;
    params->centre.y = pivot.y + dx * s + dy * c;
    // The axis vectors are rotated and rotationDeg is left ALONE. Doing both would
    // apply the rotation twice, because frameAxes() already rotates the stored axes
    // by rotationDeg -- a 90 degree rotation would then come out as 180 and the
    // bounding box would look unchanged, which is exactly how this was found.
    // Keeping the axes as the single carrier of orientation also means an
    // anisotropic figure keeps its anisotropy through any number of rotations.
    const double ux = params->axisU.x, uy = params->axisU.y;
    params->axisU.x = ux * c - uy * s;
    params->axisU.y = ux * s + uy * c;
    const double vx = params->axisV.x, vy = params->axisV.y;
    params->axisV.x = vx * c - vy * s;
    params->axisV.y = vx * s + vy * c;
    // The plasmid's base-0 reference angle is a separate, explicitly-stored
    // quantity and DOES need its own increment.
    if (params->isPlasmid()) params->plasmid.startAngleDeg += degrees;
    return core::Error::success();
}

core::Error distortBioShape(BioShapeParams* params, double shearU, double shearV) {
    if (!params) return core::Error(core::ErrorCode::OutOfRange, "null params");
    if (shearU < -4.0 || shearU > 4.0 || shearV < -4.0 || shearV > 4.0)
        return core::Error(core::ErrorCode::InvalidArgument, "shear factor out of range");
    // Shear is a SEPARATE operation from scaling: it adds a component of the other
    // axis to this one, which a scalar scale cannot express.
    params->axisU.x += shearU * params->axisV.x;
    params->axisU.y += shearU * params->axisV.y;
    params->axisV.x += shearV * params->axisU.x;
    params->axisV.y += shearV * params->axisU.y;
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Sub-element extraction (the model half of 'ungroup')
// ---------------------------------------------------------------------------
// Every sub-element becomes an independent figure of the simplest glyph that
// draws it. The PARAMETERS COME FROM THE GEOMETRY that was actually drawn, so an
// exploded part is exactly where the user saw it and cannot jump.
std::vector<ExplodedPart> explodeParts(const BioGeometry& geometry, double documentLineWidth) {
    std::vector<ExplodedPart> parts;
    for (size_t i = 0; i < geometry.elements.size(); ++i) {
        const BioSubElement& el = geometry.elements[i];
        if (el.contours.empty()) continue;
        // The part's box is the box of its own contours.
        core::Box box(el.contours[0].points[0], el.contours[0].points[0]);
        for (size_t k = 0; k < el.contours.size(); ++k)
            for (size_t j = 0; j < el.contours[k].points.size(); ++j)
                box.add(el.contours[k].points[j]);
        if (box.width() <= 0.0) box.max.x = box.min.x + 1e-6;
        if (box.height() <= 0.0) box.max.y = box.min.y + 1e-6;

        ExplodedPart part;
        part.key = el.key;
        part.params = geometry.params;
        part.params.components.clear();
        part.params.subElementColors.clear();
        part.params.plasmid = PlasmidData();
        part.params.centre = core::Vec2(0.5 * (box.min.x + box.max.x),
                                        0.5 * (box.min.y + box.max.y));
        part.params.axisU = core::Vec2(0.5 * box.width(), 0.0);
        part.params.axisV = core::Vec2(0.0, 0.5 * box.height());
        part.params.rotationDeg = 0.0;
        part.params.scale = 1.0;
        part.params.style.width = geometry.strokeWidth;
        // A single contour of a repeated unit is a POLYGON; anything with a
        // quadratic control point is generic enough that a Polygon would lose it,
        // so those keep the source glyph and simply draw at their own box.
        bool hasQuad = false;
        for (size_t k = 0; k < el.contours.size() && !hasQuad; ++k)
            for (size_t j = 0; j < el.contours[k].kind.size(); ++j)
                if (el.contours[k].kind[j] == kPointQuad) { hasQuad = true; break; }
        part.glyph = hasQuad ? geometry.params.glyph : GlyphKind::Polygon;
        part.params.glyph = part.glyph;
        if (!hasQuad) {
            // A polygon part draws its own box, so its node count is the vertex
            // count that reproduces the contour; 4 for a rectangle-ish part.
            part.params.glyphParams.nodes = 4.0;
        }
        (void)documentLineWidth;
        parts.push_back(part);
    }
    return parts;
}

} // namespace biodraw
} // namespace r62
