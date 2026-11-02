#include "next/symbols_orbitals/render_bridge.hpp"
#include "next/symbols_orbitals/push.hpp"
#include "next/arrows_curves/render_bridge.hpp"

#include <cmath>
#include <cstring>

namespace r62 {
namespace symbols {

namespace {

void hashDouble(uint64_t* h, double v) {
    // Hash the BITS, so two values that print the same but differ (a -0.0 vs 0.0,
    // or a last-bit difference from a different arithmetic order) do not collide
    // silently and serve a stale mesh.
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    *h ^= bits;
    *h *= 1099511628211ull;
}

// The colour of the "positive phase" and "negative phase" lobes. The guide says
// the colour menu changes the shading/solid colour [GUIDE p240] but does not fix
// values, so these are [PROJECT] defaults. They exist so a phase-inverted variant
// (_Shaded2/_Solid2) is genuinely VISIBLY different, not merely different in a
// stored field.
const uint32_t kPositivePhase = 0x4A7EBBFFu;
const uint32_t kNegativePhase = 0xD08A5AFFu;
const uint32_t kSymbolInk     = 0x101010FFu;

} // namespace

// ===========================================================================
// Signatures
// ===========================================================================
uint64_t symbolSignatureOf(const SymbolParams& p, double documentLineWidth) {
    // The parameter record ALREADY encodes every field the geometry depends on, so
    // hashing it is complete by construction: a field added to SymbolParams and to
    // encodeSymbolParams() automatically enters the signature. Forgetting to update
    // a hand-written field list is the classic stale-cache bug.
    uint64_t h = symbolParamsSignature(p);
    hashDouble(&h, documentLineWidth);
    return h;
}

uint64_t orbitalSignatureOf(const OrbitalParams& p, double documentLineWidth) {
    uint64_t h = orbitalParamsSignature(p);
    hashDouble(&h, documentLineWidth);
    return h;
}

uint64_t pushSignatureOf(const PushParams& p, double documentLineWidth) {
    // A push reaches the screen as a T10 arrow, so the signature must be the T10
    // one applied to the arrow the push resolves to: that way the T10 cache and the
    // T12 cache agree about identity, and a change to the head mapping can never
    // leave a stale mesh.
    const arrows::ArrowParams ap = pushToArrowParams(p, documentLineWidth);
    return arrows::arrowSignatureOf(ap, documentLineWidth);
}

// ===========================================================================
// Styles
// ===========================================================================
render::Style symbolStyle(const ShapeGeometry& geometry, const SymbolParams& p) {
    (void)p;
    render::Style s;
    s.stroke = render::Paint(kSymbolInk, true);
    s.fill   = render::Paint(kSymbolInk, true);
    s.strokeWidth = geometry.strokeWidth;
    s.cap  = render::CapStyle::Butt;
    s.join = render::JoinStyle::Miter;
    s.fillRule = render::FillRule::NonZero;
    return s;
}

render::Style orbitalStyle(const ShapeGeometry& geometry, const OrbitalParams& p,
                           int lobeIndex, bool filled, bool positive) {
    render::Style s;
    s.strokeWidth = geometry.strokeWidth;
    s.cap  = render::CapStyle::Round;
    s.join = render::JoinStyle::Round;
    // EvenOdd so the dz2 torus (an outer ring with an inner hole) fills as a band
    // rather than a disc. The same rule the rasterizer applies, so the two agree.
    s.fillRule = render::FillRule::EvenOdd;
    (void)lobeIndex;
    const uint32_t phase = positive ? kPositivePhase : kNegativePhase;
    s.stroke = render::Paint(0x202020FFu, true);
    // Open -> no fill. Shaded/Solid -> the phase colour, so the two phase variants
    // are distinguishable in a screenshot and not only in a field.
    s.fill = render::Paint(phase, filled || p.fill != OrbitalFill::Open);
    return s;
}

// ===========================================================================
// Primitive emission
// ===========================================================================
core::Error appendSymbolPrimitives(const ShapeGeometry& geometry,
                                   const SymbolParams& params,
                                   const core::ObjectRef& source,
                                   uint64_t version, uint32_t layer, int32_t baseZ,
                                   std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output list");
    if (!geometry.valid) return core::Error::success();   // nothing to draw, not an error
    const render::Style style = symbolStyle(geometry, params);
    const double defaultWidth = geometry.strokeWidth > 0.0 ? geometry.strokeWidth : 0.6;

    int z = baseZ;
    for (size_t i = 0; i < geometry.outlines.size(); ++i) {
        if (geometry.outlines[i].size() < 3) continue;
        render::Primitive pr;
        pr.kind = render::PrimKind::Polygon;
        pr.layer = render::LayerId(layer);
        pr.z = z++;
        pr.source = source;
        pr.points = geometry.outlines[i];
        // A charge ring / attachment diamond is an OUTLINE, not a filled disc: the
        // guide draws a symbol body as a line figure. Fill is disabled here and the
        // body is stroked; the enclosed sign is a separate stroke primitive.
        pr.style = style;
        pr.style.fill.enabled = false;
        pr.version = version;
        pr.bounds = render::geom::computeBounds(pr, defaultWidth);
        out->push_back(pr);
    }
    for (size_t i = 0; i < geometry.strokes.size(); ++i) {
        if (geometry.strokes[i].size() < 2) continue;
        render::Primitive pr;
        pr.kind = render::PrimKind::Polyline;
        pr.layer = render::LayerId(layer);
        pr.z = z++;
        pr.source = source;
        pr.points = geometry.strokes[i];
        pr.style = style;
        pr.version = version;
        pr.bounds = render::geom::computeBounds(pr, defaultWidth);
        out->push_back(pr);
    }
    for (size_t i = 0; i < geometry.dots.size(); ++i) {
        if (geometry.dots[i].size() < 3) continue;
        render::Primitive pr;
        pr.kind = render::PrimKind::Polygon;
        pr.layer = render::LayerId(layer);
        pr.z = z++;
        pr.source = source;
        pr.points = geometry.dots[i];
        pr.style = style;
        pr.style.fill.enabled = true;
        pr.version = version;
        pr.bounds = render::geom::computeBounds(pr, defaultWidth);
        out->push_back(pr);
    }
    return core::Error::success();
}

core::Error appendOrbitalPrimitives(const ShapeGeometry& geometry,
                                    const OrbitalParams& params,
                                    const core::ObjectRef& source,
                                    uint64_t version, uint32_t layer, int32_t baseZ,
                                    std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output list");
    if (!geometry.valid) return core::Error::success();
    const double defaultWidth = geometry.strokeWidth > 0.0 ? geometry.strokeWidth : 0.6;

    int z = baseZ;
    for (size_t i = 0; i < geometry.outlines.size(); ++i) {
        if (geometry.outlines[i].size() < 3) continue;
        const int lobeIdx = (i < geometry.outlineLobe.size()) ? geometry.outlineLobe[i] : -1;
        // The authoritative paint predicate produced by buildOrbitalGeometry, so this
    // backend and the SVG evidence cannot disagree about whether a lobe is painted.
    const bool filled = (i < geometry.outlinePaintFill.size())
                            ? geometry.outlinePaintFill[i]
                            : ((i < geometry.outlineFilled.size()) ? geometry.outlineFilled[i] : false);
        const bool positive = (i < geometry.outlinePositive.size()) ? geometry.outlinePositive[i] : true;
        render::Primitive pr;
        pr.kind = render::PrimKind::Polygon;
        pr.layer = render::LayerId(layer);
        pr.z = z++;
        pr.source = source;
        pr.points = geometry.outlines[i];
        pr.style = orbitalStyle(geometry, params, lobeIdx, filled, positive);
        pr.version = version;
        pr.bounds = render::geom::computeBounds(pr, defaultWidth);
        out->push_back(pr);
    }
    // Hatch segments carry the phase colour of the lobe that produced them. They
    // are emitted AFTER the outlines so the shading reads as ink over the shape.
    for (size_t i = 0; i < geometry.hatches.size(); ++i) {
        if (geometry.hatches[i].size() < 2) continue;
        render::Primitive pr;
        pr.kind = render::PrimKind::Segment;
        pr.layer = render::LayerId(layer);
        pr.z = z++;
        pr.source = source;
        pr.points = geometry.hatches[i];
        pr.style = orbitalStyle(geometry, params, -1, true,
                                 (i < geometry.outlinePositive.size()) ? geometry.outlinePositive[i] : true);
        pr.style.stroke = render::Paint(0x606060FFu, true);
        pr.version = version;
        pr.bounds = render::geom::computeBounds(pr, defaultWidth);
        out->push_back(pr);
    }
    for (size_t i = 0; i < geometry.strokes.size(); ++i) {
        if (geometry.strokes[i].size() < 2) continue;
        render::Primitive pr;
        pr.kind = render::PrimKind::Polyline;
        pr.layer = render::LayerId(layer);
        pr.z = z++;
        pr.source = source;
        pr.points = geometry.strokes[i];
        pr.style = orbitalStyle(geometry, params, -1, false, true);
        pr.style.fill.enabled = false;
        pr.version = version;
        pr.bounds = render::geom::computeBounds(pr, defaultWidth);
        out->push_back(pr);
    }
    return core::Error::success();
}

core::Error appendPushPrimitives(const PushParams& params, double documentLineWidth,
                                 const core::ObjectRef& source, uint32_t layer,
                                 std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output list");
    const core::Error v = validatePushParams(params);
    if (!v.ok()) return v;
    // DELEGATED TO T10. There is no curve flattener and no head builder in this
    // module; arrows::appendArrow owns both, so a push and an arrow can never
    // render differently for the same parameters.
    const arrows::ArrowParams ap = pushToArrowParams(params, documentLineWidth);
    arrows::Tessellation tess;
    return arrows::appendArrow(ap, documentLineWidth, tess, source, layer, out);
}

core::Error appendSymbol(const SymbolParams& p, double documentLineWidth,
                         const Tessellation& tess, const core::ObjectRef& source,
                         uint32_t layer, std::vector<render::Primitive>* out) {
    ShapeGeometry g;
    const core::Error e = buildSymbolGeometry(p, documentLineWidth, tess, &g);
    if (!e.ok()) return e;
    return appendSymbolPrimitives(g, p, source, symbolSignatureOf(p, documentLineWidth),
                                  layer, 0, out);
}

core::Error appendOrbital(const OrbitalParams& p, double documentLineWidth,
                          const Tessellation& tess, const core::ObjectRef& source,
                          uint32_t layer, std::vector<render::Primitive>* out) {
    ShapeGeometry g;
    const core::Error e = buildOrbitalGeometry(p, documentLineWidth, tess, &g);
    if (!e.ok()) return e;
    return appendOrbitalPrimitives(g, p, source, orbitalSignatureOf(p, documentLineWidth),
                                   layer, 0, out);
}

// ===========================================================================
// Overlay handles
// ===========================================================================
void appendSymbolHandles(const std::vector<Handle>& handles,
                         const core::ObjectRef& source,
                         const core::ViewTransform& view, uint32_t layer,
                         std::vector<render::Primitive>* out) {
    if (!out) return;
    // Handles are sized in DEVICE pixels and converted through the view, so they
    // stay a constant on-screen size at any zoom and any DPI - the same rule T10
    // and T03 use. No document-space constant is invented here.
    const double size = view.deviceRadiusToDocument(4.0);
    for (size_t i = 0; i < handles.size(); ++i) {
        render::Primitive pr;
        pr.kind = render::PrimKind::Marker;
        pr.layer = render::LayerId(layer);
        pr.z = static_cast<int32_t>(i);
        pr.source = source;
        pr.points.push_back(handles[i].point);
        pr.marker = (handles[i].kind == HandleKind::Rotate)
                        ? render::MarkerShape::HandleCircle
                        : render::MarkerShape::HandleSquare;
        pr.size = size * 2.0;
        pr.style.stroke = render::Paint(0x1E64C8FFu, true);
        pr.style.fill   = render::Paint(0xFFFFFFAAu, true);
        pr.style.strokeWidth = size * 0.5;
        pr.version = 0;
        pr.bounds = render::geom::computeBounds(pr, size);
        out->push_back(pr);
    }
}

} // namespace symbols
} // namespace r62
