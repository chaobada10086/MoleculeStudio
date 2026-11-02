#ifndef R62_NEXT_RENDER_SCENE_BUILDER_HPP
#define R62_NEXT_RENDER_SCENE_BUILDER_HPP
// =============================================================================
// re_round62_viewer/next/render/scene_builder.hpp
//
// T03 - turns a core::Document (plus tool overlays) into a render::Scene.
//
// This is an ADAPTER, not a second model. It reads T01's frozen slabs and emits
// Primitive records. It never writes to the document and never invents chemistry.
//
// INCREMENTAL CONTRACT (the point of this file):
//   * rebuildContent() walks the document and records a per-object signature.
//   * updateContent() re-walks, compares signatures, and only re-emits the
//     primitives whose object actually changed. Untouched primitives are kept
//     by value, so the backend's identity-based mesh cache stays warm.
//   A caller that pans or zooms calls NEITHER: the view change is handled by
//   Backend::setView and does not touch this builder at all.
// =============================================================================

#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"

#include <map>
#include <vector>

namespace r62 {
namespace render {

// What the builder produced, so a caller can prove selective work happened.
struct BuildStats {
    size_t objectsScanned;
    size_t primitivesEmitted;
    size_t primitivesReused;
    size_t primitivesRemoved;
    std::vector<core::ObjectRef> changed;   // objects whose primitive was rebuilt
    BuildStats() : objectsScanned(0), primitivesEmitted(0), primitivesReused(0),
                   primitivesRemoved(0) {}
};

// Geometry parameters the builder needs. These are NOT chemistry decisions:
// they are the drawing conventions every chemistry module would agree on, and a
// business module is free to override them per primitive after the fact.
struct BuildOptions {
    double atomLabelSize;      // document units for an element label
    double bondDoubleOffset;   // document units, each line's offset from the axis
    double bondTripleOffset;
    double arrowHeadLength;    // document units
    double selectionHandleSize;
    double hotspotRadius;
    uint32_t atomLabelColor;
    uint32_t bondColor;
    uint32_t selectionColor;
    uint32_t hotspotColor;
    uint32_t previewColor;
    BuildOptions()
        : atomLabelSize(10.0), bondDoubleOffset(1.6), bondTripleOffset(2.2),
          arrowHeadLength(4.0), selectionHandleSize(2.6), hotspotRadius(14.0),
          atomLabelColor(0x000000FFu), bondColor(0x000000FFu),
          selectionColor(0x1A73E8FFu), hotspotColor(0x1F9B3AFFu),
          previewColor(0x8A8A8AFFu) {}
};

// Builds a Scene from a document. One instance per (document, view) pair; it
// holds the previous signature map so updateContent() can diff.
class SceneBuilder {
public:
    SceneBuilder();
    void setOptions(const BuildOptions& options) { options_ = options; }
    const BuildOptions& options() const { return options_; }

    // Full rebuild. `hf` is the layer content lives in.
    void rebuildContent(const core::Document& doc, Scene* scene,
                        const core::ViewTransform& view, LayerId contentLayer,
                        BuildStats* stats);

    // Diffing rebuild. Emits only changed/new primitives; reuses the rest by
    // copying the PREVIOUS primitive value (so `version` and `bounds` are
    // identical and the backend cache hits).
    void updateContent(const core::Document& doc, Scene* scene,
                       const core::ViewTransform& view, LayerId contentLayer,
                       BuildStats* stats);

    // ---- overlay builders -------------------------------------------------
    // Selection marks and drag handles for a selection set. Layer: kSelection.
    void buildSelection(const core::Document& doc,
                        const std::vector<core::ObjectRef>& selection,
                        const core::ViewTransform& view, OverlayScene* overlay,
                        const core::Vec2& dragDelta);
    // Hover hotspot. Layer: kHighlight.
    void buildHotspot(const core::Document& doc, const core::ObjectRef& hotspot,
                      const core::ViewTransform& view, OverlayScene* overlay);
    // Tool preview (T01 PreviewScene). Layer: kPreview.
    void buildPreview(const core::PreviewScene& preview, const core::ViewTransform& view,
                      OverlayScene* overlay);
    // Marquee / guide rectangle. Layer: kGuide.
    void buildMarquee(const core::Vec2& a, const core::Vec2& b,
                      const core::ViewTransform& view, OverlayScene* overlay);

    // The flattened glyph measurer is injected; ownership stays with the caller.
    void setTextMeasurer(geom::TextMeasurer* measurer) { measurer_ = measurer; }

    // Objects the caller wants suppressed this frame (a drag preview replaces
    // them). The builder honours it by omitting those primitives.
    void setHidden(const std::vector<core::ObjectRef>& hidden);

private:
    BuildOptions options_;
    geom::TextMeasurer* measurer_;
    // Set by rebuildContent/updateContent so an overlay builder can resolve an
    // ObjectRef without the caller having to pass the document twice.
    const core::Document* doc_;
    std::map<core::ObjectRef, uint64_t> signatures_;
    std::map<core::ObjectRef, Primitive> cached_;
    std::map<core::ObjectRef, bool> hidden_;

    // Signature of everything a primitive depends on. Two calls returning the
    // same value MUST produce the same primitive; that is the whole guarantee.
    static uint64_t signatureOfAtom(const core::Document& doc, const core::AtomRecord& a,
                                    const core::ViewTransform& view, const BuildOptions& o);
    static uint64_t signatureOfBond(const core::Document& doc, const core::BondRecord& b,
                                    const core::ViewTransform& view, const BuildOptions& o);
    static uint64_t signatureOfText(const core::Document& doc, const core::TextRecord& t,
                                    const core::ViewTransform& view, const BuildOptions& o);
    static uint64_t signatureOfPath(const core::PathRecord& p);

    Primitive makeAtom(const core::Document& doc, const core::AtomRecord& a,
                       const core::ViewTransform& view, LayerId layer) const;
    Primitive makeBond(const core::Document& doc, const core::BondRecord& b,
                       const core::ViewTransform& view, LayerId layer) const;
    Primitive makeText(const core::Document& doc, const core::TextRecord& t,
                       const core::ViewTransform& view, LayerId layer) const;
    Primitive makePath(const core::PathRecord& p, const core::ViewTransform& view,
                       LayerId layer) const;
};

} // namespace render
} // namespace r62

#endif // R62_NEXT_RENDER_SCENE_BUILDER_HPP
