#ifndef R62_NEXT_EXPERIMENTAL_RENDER_BRIDGE_HPP
#define R62_NEXT_EXPERIMENTAL_RENDER_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/experimental/render_bridge.hpp
//
// T16 - THE BRIDGE FROM A FIGURE TO T03 render::Primitive AND T09 TEXT.
//
// This is the only header in next/experimental/ that includes next/render and
// next/text. The geometry kernel and the data model stay render-free, so every
// geometry and validation claim is testable with no GPU and no font.
//
// -----------------------------------------------------------------------------
// WHAT IS DELEGATED
// -----------------------------------------------------------------------------
//   * fill/segment/polygon batching, dashing and bounds: T03.
//   * glyph outlines, layout and text primitives: T09. When a GlyphCache is
//     supplied the labels are emitted as T09 filled-glyph polygons; when it is
//     null the labels fall back to T03 Text primitives with a CONSERVATIVE ink
//     box, and that fallback is stated in RESULTS.md rather than hidden here.
//
// The version passed in is the caller value (figureObjectSignature) and is copied
// onto every primitive, so T03 incremental cache identity follows the figure.
// =============================================================================

#include "next/core/document.hpp"
#include "next/experimental/geometry.hpp"
#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"
#include "next/text/glyph_source.hpp"
#include "next/text/render_bridge.hpp"
#include "next/text/rich_text.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace experimental {

inline bool requestRenderBridgeCompatibility(int major, int minor) {
    return render::requestRenderCompatibility(major, minor);
}

render::Style renderStyleFor(const PlateFigure& figure, double documentLineWidth);

// One label the figure draws from its payload. Annotation TEXT OBJECTS are not
// listed here: they are real core::Text objects and the normal text path draws
// them. This list is the payload-embedded vocabulary (lane labels, guide captions,
// spot labels and derived Rf captions).
struct FigureLabel {
    std::string text;
    core::Vec2  position;   // document-space baseline anchor
    double      size;       // document units
    std::string kind;       // "title" | "lane" | "origin" | "front" | "spot" | "rf"
    FigureLabel() : size(0.0) {}
};
void collectFigureLabels(const PlateFigure& figure, const PlateFrame& frame,
                         double defaultTextSize, std::vector<FigureLabel>* out);

core::Error appendFigurePrimitives(const PlateFigure& figure, const PlateFrame& frame,
                                   const core::ObjectRef& source, uint64_t version,
                                   uint32_t layer, int32_t baseZ, double documentLineWidth,
                                   double defaultTextSize,
                                   text::GlyphCache* glyphs, const text::LayoutOptions& options,
                                   std::vector<render::Primitive>* out);

core::Error appendFigurePrimitivesFor(const core::Document& document, core::ObjectId figureId,
                                      uint32_t layer, int32_t baseZ,
                                      text::GlyphCache* glyphs,
                                      const text::LayoutOptions& options,
                                      std::vector<render::Primitive>* out);

} // namespace experimental
} // namespace r62

#endif // R62_NEXT_EXPERIMENTAL_RENDER_BRIDGE_HPP
