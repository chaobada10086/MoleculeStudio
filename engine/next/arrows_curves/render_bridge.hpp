#ifndef R62_NEXT_ARROWS_CURVES_RENDER_BRIDGE_HPP
#define R62_NEXT_ARROWS_CURVES_RENDER_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/arrows_curves/render_bridge.hpp
//
// T10 - THE BRIDGE FROM ARROW/CURVE GEOMETRY TO T03 render::Primitive.
//
// This is the ONLY header in next/arrows_curves/ that includes next/render. The
// geometry kernel stays render-independent, so it is testable with no GPU and a
// future host adapter could reuse it without adopting T03. That is the same
// boundary T09 draws between rich_text and render_bridge.
//
// -----------------------------------------------------------------------------
// HOW AN ARROW OR CURVE REACHES THE SCREEN
// -----------------------------------------------------------------------------
//   geometry.hpp  ->  ArrowGeometry / CurveGeometry   (pure, cached by the caller)
//   appendPrimitives() -> render::Primitive records   (one per drawn element)
//
// There is no per-preset code anywhere in this file. It reads ArrowParams and
// emits whatever the parameters describe, which is the mechanical meaning of
// "56 presets are data, not 56 draw functions".
//
// -----------------------------------------------------------------------------
// VERSION SIGNATURE (the cache key)
// -----------------------------------------------------------------------------
// Primitive::version must change when the object content changes and stay the
// same when it does not. arrowSignatureOf() hashes exactly the inputs the
// geometry depends on - the parameter record plus the document line width - and
// NOT the view transform, because zoom is not an object change. That is what
// makes a zoom free (T03 INTERFACES.md 5, contract 1).
// =============================================================================

#include "next/arrows_curves/geometry.hpp"
#include "next/arrows_curves/model.hpp"
#include "next/arrows_curves/preset.hpp"
#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"

#include <cstdint>
#include <vector>

namespace r62 {
namespace arrows {

// The render module this header was compiled against.
inline bool requestRenderBridgeCompatibility(int major, int minor) {
    return render::requestRenderCompatibility(major, minor);
}

// ---------------------------------------------------------------------------
// Signatures
// ---------------------------------------------------------------------------
// Stable content signature of an arrow. Two calls with equal inputs MUST produce
// equal primitives; this value is the cache key that guarantees it.
uint64_t arrowSignatureOf(const ArrowParams& params, double documentLineWidth);
uint64_t curveSignatureOf(const std::vector<core::Vec2>& points,
                          const std::vector<uint8_t>& pointKind, bool closed,
                          const CurveStyleSpec& style, double documentLineWidth);

// ---------------------------------------------------------------------------
// Primitive emission
// ---------------------------------------------------------------------------
// One Primitive per drawn element: a shaft, a head outline, a head stroke, a no-go
// marker. A chevron arrow is therefore ~4 primitives, NOT one per triangle and NOT
// one per preset. `layer` is the content layer the caller owns; `baseZ` orders the
// pieces so a head paints over its shaft.
//
// Bounds are filled with render::geom::computeBounds, so T04/T05 hit tests can use
// them without re-deriving stroke width, head extent or dash pattern.
core::Error appendArrowPrimitives(const ArrowGeometry& geometry,
                                  const core::ObjectRef& source,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  std::vector<render::Primitive>* out);

core::Error appendCurvePrimitives(const CurveGeometry& geometry,
                                  const core::ObjectRef& source,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  std::vector<render::Primitive>* out);

// Convenience: build the geometry AND emit. Used by the demo and by any caller
// that has no reason to hold the geometry itself.
core::Error appendArrow(const ArrowParams& params, double documentLineWidth,
                        const Tessellation& tess, const core::ObjectRef& source,
                        uint32_t layer, std::vector<render::Primitive>* out);
core::Error appendCurve(const std::vector<core::Vec2>& points,
                        const std::vector<uint8_t>& pointKind, bool closed,
                        const CurveStyleSpec& style, double documentLineWidth,
                        const Tessellation& tess, const core::ObjectRef& source,
                        uint32_t layer, std::vector<render::Primitive>* out);

// ---------------------------------------------------------------------------
// Style translation
// ---------------------------------------------------------------------------
// ArrowParams -> render::Style. Exposed so the editor overlay and the content
// layer cannot disagree about what a "faded" or "hollow" arrow looks like.
render::Style arrowStyle(const ArrowGeometry& geometry);
render::Style curveStyle(const CurveGeometry& geometry);

// ---------------------------------------------------------------------------
// Overlay: handles for the currently edited arrow/curve
// ---------------------------------------------------------------------------
// The adjustment handles the guide describes [GUIDE p109, p117] as overlay
// primitives. They go to the overlay scene, never into content, so clearing the
// overlay cannot erase a user object.
void appendArrowHandles(const std::vector<Handle>& handles,
                        const core::ObjectRef& source,
                        const core::ViewTransform& view, uint32_t layer,
                        std::vector<render::Primitive>* out);

} // namespace arrows
} // namespace r62

#endif // R62_NEXT_ARROWS_CURVES_RENDER_BRIDGE_HPP
