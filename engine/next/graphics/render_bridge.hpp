#ifndef R62_NEXT_GRAPHICS_RENDER_BRIDGE_HPP
#define R62_NEXT_GRAPHICS_RENDER_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/graphics/render_bridge.hpp
//
// T11 - THE BRIDGE FROM SHAPE/BRACKET/TABLE GEOMETRY TO T03 render::Primitive.
//
// This is the ONLY header in next/graphics/ that includes next/render, exactly
// as next/arrows_curves/render_bridge.hpp is for T10. The geometry kernel stays
// render-independent, so the geometry and hit-test claims are testable with no
// GPU, and a future host adapter can reuse them without adopting T03.
//
// -----------------------------------------------------------------------------
// WHAT IS DELEGATED TO T03 AND WHAT IS NOT
// -----------------------------------------------------------------------------
// This module does NOT re-implement: curve flattening, dashing, wavy strokes,
// stroking to triangles, ear-clipping fills, or bounds inflation. Every one of
// those lives in next/render/geometry.hpp and is called here:
//
//   * a dashed appearance becomes style.dash; T03 applies it at flatten time;
//   * a wavy line becomes style.wavyAmplitude; T03 applies it;
//   * a fill becomes PrimKind::Polygon with subpaths[0] and the fill paint;
//   * every primitive's conservative bounds is render::geom::computeBounds().
//
// What IS this module's job is the SHAPE GEOMETRY: which points a circle, a
// rounded rectangle, an arc or a curly brace has. That is a business decision.
// 
// -----------------------------------------------------------------------------
// VERSION SIGNATURE
// -----------------------------------------------------------------------------
// Primitive::version is shapeSignature()/bracketSignature()/tableSignature():
// a hash of exactly the inputs the geometry depends on, plus the document line
// width, and NOT the view transform. Zoom therefore costs nothing and is not an
// object change, which is T03's cache contract.
// =============================================================================

#include "next/core/document.hpp"
#include "next/graphics/geometry.hpp"
#include "next/graphics/tables.hpp"
#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"

#include <vector>

namespace r62 {
namespace graphics {

// The render module this header was compiled against.
inline bool requestRenderBridgeCompatibility(int major, int minor) {
    return render::requestRenderCompatibility(major, minor);
}


// ---------------------------------------------------------------------------
// Style bridging
// ---------------------------------------------------------------------------
// A ShapeParams already carries a core::LineStyle and a fill colour, so the
// bridge is a total function. The default stroke width (0) becomes the caller's
// document line width, never a hard-coded constant, so a document default change
// reaches the screen.
render::Style renderStyleFor(const ShapeParams& p, double documentLineWidth);
render::Style renderStyleFor(const BracketParams& p, double documentLineWidth);
render::Style renderStyleFor(const TableGrid& g, double documentLineWidth);

// ---------------------------------------------------------------------------
// Emission
// ---------------------------------------------------------------------------
// One Primitive per drawn element: a fill, a stroke, a shadow, a bracket side, a
// table grid line, a label. The caller owns `layer` and `baseZ`; pieces inside one
// object use baseZ, baseZ+1, ... so a label paints over its bracket.
core::Error appendShapePrimitives(const ShapeParams& params, const core::ObjectRef& source,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  double documentLineWidth,
                                  std::vector<render::Primitive>* out);
core::Error appendBracketPrimitives(const BracketParams& params, const core::ObjectRef& source,
                                    uint64_t version, uint32_t layer, int32_t baseZ,
                                    double documentLineWidth, double labelTextSize,
                                    std::vector<render::Primitive>* out);
core::Error appendTablePrimitives(const core::Document& document, core::ObjectId id,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  double documentLineWidth,
                                  std::vector<render::Primitive>* out);

// Convenience wrappers that read the object and emit. They return NotFound for a
// dead id, so a stale cache entry cannot draw a ghost.
core::Error appendShapePrimitivesFor(const core::Document& document, core::ObjectId id,
                                     uint32_t layer, int32_t baseZ,
                                     std::vector<render::Primitive>* out);
core::Error appendBracketPrimitivesFor(const core::Document& document, core::ObjectId id,
                                       uint32_t layer, int32_t baseZ, double labelTextSize,
                                       std::vector<render::Primitive>* out);
core::Error appendTablePrimitivesFor(const core::Document& document, core::ObjectId id,
                                     uint32_t layer, int32_t baseZ,
                                     std::vector<render::Primitive>* out);

// Content signature of a table (grid + borders + cell kinds and text).
uint64_t tableSignature(const core::Document& document, core::ObjectId id);

} // namespace graphics
} // namespace r62

#endif // R62_NEXT_GRAPHICS_RENDER_BRIDGE_HPP
