#ifndef R62_NEXT_BIODRAW_RENDER_BRIDGE_HPP
#define R62_NEXT_BIODRAW_RENDER_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/render_bridge.hpp
//
// T14 - THE ONLY PATH FROM BioDraw GEOMETRY INTO T03's SCENE.
//
// -----------------------------------------------------------------------------
// WHY THERE IS EXACTLY ONE BRIDGE
// -----------------------------------------------------------------------------
// T03's INTERFACES.md freezes scene_api.hpp as the single drawing interface and
// forbids a business module from issuing its own GL calls or copying the curve /
// fill code. So this header is the whole drawing surface of T14: a caller converts
// a BioShapeParams into render::Primitive records and hands them to a Scene.
//
// There is no private flattener here. A quadratic contour is published as a Curve
// primitive with T03's PointKind values, and T03 flattens it -- so a change to the
// flattener improves every BioDraw figure at once.
//
// -----------------------------------------------------------------------------
// SUB-ELEMENT COLOUR IS CARRIED THROUGH
// -----------------------------------------------------------------------------
// Each element's own colour (from resolveSubElementColor) becomes the PAINT of its
// primitive. That is what makes the sub-element colouring a real document property
// rather than a render-time tint: the primitive is coloured from the model, so the
// same figure re-rendered anywhere looks the same.
// =============================================================================

#include "next/biodraw/geometry.hpp"
#include "next/render/scene_api.hpp"

namespace r62 {
namespace biodraw {

// Handshake: the render API version this bridge was compiled against.
bool requestBioRenderCompatibility(int major, int minor);

// Translate a figure's own LineStyle + fill into a render style. Exposed so a
// caller can style a preview identically without duplicating the mapping.
render::Style bioRenderStyle(const BioShapeParams& params, const core::LineStyle& style,
                             uint32_t fillRGBA, double documentLineWidth);

// Append every sub-element of `geometry` to `scene`. `source` is stamped on each
// primitive so the incremental cache and the hit test can identify the object.
// `version` is the content signature; two calls with the same version produce
// primitives the cache can reuse.
core::Error appendBioPrimitives(const BioGeometry& geometry, const core::ObjectRef& source,
                                uint64_t version, render::Scene* scene);

// Convenience: read the figure from the document and append it. Returns
// core::Error::success() with no primitives added when the object is not a
// BioShape, so a caller can loop over every BioShape without pre-checking.
core::Error appendBioPrimitivesFor(const core::Document& document, core::ObjectId id,
                                   render::Scene* scene);

// Build an SVG document for a set of figures. This is EVIDENCE, not a renderer:
// it exists so the geometry can be inspected and archived without a GPU, and it
// uses the same contours the renderer consumes. Text is emitted as SVG <text>
// only for labels the caller supplies.
std::string bioGeometryToSvg(const BioGeometry& geometry, const std::string& title,
                             double padding);

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_RENDER_BRIDGE_HPP
