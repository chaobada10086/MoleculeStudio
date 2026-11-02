// =============================================================================
// re_round62_viewer/next/render/scene_builder_overlay.cpp
//
// T03 - selection / hotspot / preview / marquee overlay builders.
//
// Every primitive built here goes into an OverlayScene, which REJECTS anything
// below layers::kHighlight. That is the structural guarantee behind 'an overlay
// must not clear user objects': an overlay primitive physically cannot occupy a
// content layer.
// =============================================================================

#include "next/render/scene_builder.hpp"
#include "next/render/geometry.hpp"

#include <cmath>

namespace r62 {
namespace render {

static Primitive overlayBase(PrimKind kind, uint32_t layer, int32_t z) {
    Primitive p;
    p.kind = kind;
    p.layer = LayerId(layer);
    p.z = z;
    p.source = core::ObjectRef();
    return p;
}

void SceneBuilder::buildSelection(const core::Document& doc,
                                  const std::vector<core::ObjectRef>& selection,
                                  const core::ViewTransform& view, OverlayScene* overlay,
                                  const core::Vec2& dragDelta) {
    (void)view;
    if (!overlay) return;
    for (size_t i = 0; i < selection.size(); ++i) {
        const core::ObjectRef& ref = selection[i];
        // ---- atom: a ring around it, plus small square drag handles ----------
        if (ref.kind == core::ObjectKind::Atom) {
            const core::AtomRecord* a = doc.atoms().alive(ref.id);
            if (!a) continue;
            const core::Vec2 c(a->position.x + dragDelta.x, a->position.y + dragDelta.y);
            Primitive ring = overlayBase(PrimKind::Marker, layers::kSelection, 0);
            ring.marker = MarkerShape::Ring;
            ring.size = options_.hotspotRadius * 0.5;
            ring.points.push_back(c);
            ring.style.fill = Paint(options_.selectionColor, true);
            ring.style.stroke.enabled = false;
            ring.bounds = core::Box(core::Vec2(c.x - ring.size, c.y - ring.size),
                                    core::Vec2(c.x + ring.size, c.y + ring.size));
            overlay->add(ring);

            // Four corner handles, so a caller can see the drag affordance and so
            // the bounding-box-for-handles rule has a real consumer.
            const double h = options_.selectionHandleSize;
            const double off = options_.hotspotRadius * 0.5;
            const core::Vec2 corners[4] = {
                core::Vec2(c.x - off, c.y - off), core::Vec2(c.x + off, c.y - off),
                core::Vec2(c.x + off, c.y + off), core::Vec2(c.x - off, c.y + off) };
            for (int k = 0; k < 4; ++k) {
                Primitive handle = overlayBase(PrimKind::Marker, layers::kSelection, 1);
                handle.marker = MarkerShape::HandleSquare;
                handle.size = h;
                handle.points.push_back(corners[k]);
                handle.style.fill = Paint(options_.selectionColor, true);
                handle.style.stroke.enabled = false;
                handle.bounds = core::Box(
                    core::Vec2(corners[k].x - h * 0.5, corners[k].y - h * 0.5),
                    core::Vec2(corners[k].x + h * 0.5, corners[k].y + h * 0.5));
                overlay->add(handle);
            }
        } else if (ref.kind == core::ObjectKind::Bond) {
            const core::BondRecord* b = doc.bonds().alive(ref.id);
            if (!b) continue;
            const core::AtomRecord* pa = doc.atoms().alive(b->beginAtom);
            const core::AtomRecord* pb = doc.atoms().alive(b->endAtom);
            if (!pa || !pb) continue;
            Primitive line = overlayBase(PrimKind::Segment, layers::kSelection, 0);
            line.points.push_back(core::Vec2(pa->position.x + dragDelta.x, pa->position.y + dragDelta.y));
            line.points.push_back(core::Vec2(pb->position.x + dragDelta.x, pb->position.y + dragDelta.y));
            line.style.stroke = Paint(options_.selectionColor, true);
            line.style.strokeWidth = doc.defaultLineWidth() * 3.0;
            line.style.cap = CapStyle::Round;
            line.bounds = geom::computeBounds(line, doc.defaultLineWidth());
            overlay->add(line);
        } else if (ref.kind == core::ObjectKind::Text) {
            const core::TextRecord* t = doc.texts().alive(ref.id);
            if (!t) continue;
            Primitive box = overlayBase(PrimKind::Rect, layers::kSelection, 0);
            const double size = t->size > 0.0 ? t->size : doc.defaultTextSize();
            const std::string plain = doc.plainText(ref.id);
            const double w = size * static_cast<double>(plain.size() + 1) * 0.6;
            const double ox = t->position.x + dragDelta.x, oy = t->position.y + dragDelta.y;
            box.points.push_back(core::Vec2(ox, oy));
            box.points.push_back(core::Vec2(ox + w, oy + size * 1.35));
            box.style.stroke = Paint(options_.selectionColor, true);
            box.style.strokeWidth = doc.defaultLineWidth() * 1.5;
            box.style.fill.enabled = false;
            box.bounds = core::Box(box.points[0], box.points[1]);
            overlay->add(box);
        }
    }
}

void SceneBuilder::buildHotspot(const core::Document& doc, const core::ObjectRef& hotspot,
                                const core::ViewTransform& view, OverlayScene* overlay) {
    (void)view;
    if (!overlay || !hotspot.valid()) return;
    if (hotspot.kind == core::ObjectKind::Atom) {
        const core::AtomRecord* a = doc.atoms().alive(hotspot.id);
        if (!a) return;
        Primitive ring = overlayBase(PrimKind::Marker, layers::kHighlight, 0);
        ring.marker = MarkerShape::Circle;
        ring.size = options_.hotspotRadius;
        ring.points.push_back(a->position);
        // A hotspot is a thin outline, not a filled disc, so the object under it
        // stays readable. Applied by the backend as a scaled ring outline.
        ring.style.fill = Paint(0x00000000u, false);
        ring.style.stroke = Paint(options_.hotspotColor, true);
        ring.style.strokeWidth = 0.9;
        ring.bounds = core::Box(
            core::Vec2(a->position.x - options_.hotspotRadius * 0.5, a->position.y - options_.hotspotRadius * 0.5),
            core::Vec2(a->position.x + options_.hotspotRadius * 0.5, a->position.y + options_.hotspotRadius * 0.5));
        overlay->add(ring);
    } else if (hotspot.kind == core::ObjectKind::Bond) {
        const core::BondRecord* b = doc.bonds().alive(hotspot.id);
        if (!b) return;
        const core::AtomRecord* pa = doc.atoms().alive(b->beginAtom);
        const core::AtomRecord* pb = doc.atoms().alive(b->endAtom);
        if (!pa || !pb) return;
        Primitive line = overlayBase(PrimKind::Segment, layers::kHighlight, 0);
        line.points.push_back(pa->position);
        line.points.push_back(pb->position);
        line.style.stroke = Paint(options_.hotspotColor, true);
        line.style.strokeWidth = doc.defaultLineWidth() * 4.0;
        line.style.cap = CapStyle::Round;
        line.bounds = geom::computeBounds(line, doc.defaultLineWidth());
        overlay->add(line);
    }
}

// T01 publishes PreviewScene as plain descriptions. Mapping them here keeps the
// tool layer free of any rendering dependency - the boundary INTERFACES.md s15
// states ('PreviewScene is only a description; how to draw it is T03's business').
void SceneBuilder::buildPreview(const core::PreviewScene& preview,
                                const core::ViewTransform& view, OverlayScene* overlay) {
    (void)view;
    if (!overlay) return;
    const std::vector<core::PreviewItem>& items = preview.items();
    auto applyPreviewDash = [](const core::PreviewItem& item, Primitive& p) {
        switch (item.style.dash) {
        case static_cast<uint8_t>(core::DashStyle::Dashed): p.style.dash = DashPattern(3.0, 2.0); break;
        case static_cast<uint8_t>(core::DashStyle::Dotted): p.style.dash = DashPattern(0.8, 1.6); break;
        case static_cast<uint8_t>(core::DashStyle::Wavy): p.style.dash = DashPattern(2.0, 1.0); break;
        default: break;
        }
    };
    for (size_t i = 0; i < items.size(); ++i) {
        const core::PreviewItem& item = items[i];
        const uint32_t color = (item.style.colorRGBA == 0xFF000000u || item.style.colorRGBA == 0u)
            ? options_.previewColor : item.style.colorRGBA;
        switch (item.primitive) {
            case core::PreviewPrimitive::Segment: {
                if (item.points.size() < 2) break;
                Primitive p = overlayBase(PrimKind::Segment, layers::kPreview, 0);
                p.points = item.points;
                p.style.stroke = Paint(color, true);
                p.style.strokeWidth = item.style.width > 0.0 ? item.style.width : 1.0;
                applyPreviewDash(item, p);
                p.style.cap = CapStyle::Round;
                p.bounds = geom::computeBounds(p, 1.0);
                overlay->add(p);
                break;
            }
            case core::PreviewPrimitive::Polyline: {
                if (item.points.size() < 2) break;
                Primitive p = overlayBase(PrimKind::Polyline, layers::kPreview, 0);
                p.points = item.points;
                p.style.stroke = Paint(color, true);
                p.style.strokeWidth = item.style.width > 0.0 ? item.style.width : 1.0;
                applyPreviewDash(item, p);
                p.bounds = geom::computeBounds(p, 1.0);
                overlay->add(p);
                break;
            }
            case core::PreviewPrimitive::Rect: {
                if (item.points.size() < 2) break;
                Primitive p = overlayBase(PrimKind::Rect, layers::kPreview, 0);
                p.points = item.points;
                p.style.stroke = Paint(color, true);
                p.style.strokeWidth = item.style.width > 0.0 ? item.style.width : 1.0;
                applyPreviewDash(item, p);
                p.style.fill.enabled = false;
                if (item.fillEnabled) p.style.fill = Paint(item.fillRGBA, true);
                p.bounds = geom::computeBounds(p, 1.0);
                overlay->add(p);
                break;
            }
            case core::PreviewPrimitive::Circle: {
                if (item.points.empty()) break;
                Primitive p = overlayBase(PrimKind::Ellipse, layers::kPreview, 0);
                const double r = item.points.size() > 1 ? item.points[1].x : 1.0;
                p.points.push_back(item.points[0]);
                p.points.push_back(core::Vec2(r, r));
                p.style.stroke = Paint(color, true);
                p.style.strokeWidth = item.style.width > 0.0 ? item.style.width : 1.0;
                applyPreviewDash(item, p);
                p.style.fill.enabled = false;
                if (item.fillEnabled) p.style.fill = Paint(item.fillRGBA, true);
                p.bounds = core::Box(core::Vec2(item.points[0].x - r, item.points[0].y - r),
                                     core::Vec2(item.points[0].x + r, item.points[0].y + r));
                overlay->add(p);
                break;
            }
            case core::PreviewPrimitive::Polygon: {
                if (item.points.size() < 3) break;
                Primitive p = overlayBase(PrimKind::Polygon, layers::kPreview, 0);
                p.points = item.points;
                p.style.stroke = Paint(color, true);
                p.style.strokeWidth = item.style.width > 0.0 ? item.style.width : 1.0;
                applyPreviewDash(item, p);
                p.style.fill.enabled = false;
                if (item.fillEnabled) p.style.fill = Paint(item.fillRGBA, true);
                p.bounds = geom::computeBounds(p, 1.0);
                overlay->add(p);
                break;
            }
            case core::PreviewPrimitive::Label: {
                if (item.points.empty()) break;
                Primitive p = overlayBase(PrimKind::Text, layers::kPreview, 0);
                p.text = item.text;
                p.textSize = 10.0;
                p.points.push_back(item.points[0]);
                p.style.fill = Paint(color, true);
                p.style.stroke.enabled = false;
                p.bounds = core::Box(item.points[0],
                    core::Vec2(item.points[0].x + 10.0 * static_cast<double>(item.text.size()),
                               item.points[0].y + 12.0));
                overlay->add(p);
                break;
            }
            case core::PreviewPrimitive::Highlight: {
                // An existing object drawn highlighted: a ring for an atom, a thick
                // line for a bond. Reuses the hotspot builder so the highlight and
                // the hover look identical, which is what a user expects.
                if (doc_) buildHotspot(*doc_, item.subject, view, overlay);
                break;
            }
            case core::PreviewPrimitive::Ghost:
            case core::PreviewPrimitive::GlyphTransform: {
                // Both need the existing object's world geometry transformed by the
                // gesture delta. The CALLER supplies that geometry as points, so the
                // preview stays a description and the builder stays document-only.
                if (item.points.size() < 2) break;
                Primitive p = overlayBase(PrimKind::Polyline, layers::kPreview, 0);
                p.points = item.points;
                p.style.stroke = Paint(color, true);
                p.style.strokeWidth = item.style.width > 0.0 ? item.style.width : 1.0;
                applyPreviewDash(item, p);
                if (item.style.dash == 0) p.style.dash = DashPattern(2.0, 1.5);   // ghosts are dashed by default
                p.bounds = geom::computeBounds(p, 1.0);
                overlay->add(p);
                break;
            }
        }
    }
}

void SceneBuilder::buildMarquee(const core::Vec2& a, const core::Vec2& b,
                                const core::ViewTransform& view, OverlayScene* overlay) {
    (void)view;
    if (!overlay) return;
    Primitive p = overlayBase(PrimKind::Rect, layers::kGuide, 0);
    p.points.push_back(a);
    p.points.push_back(b);
    p.style.stroke = Paint(options_.selectionColor, true);
    p.style.strokeWidth = 1.1;
    p.style.dash = DashPattern(2.0, 1.5);
    p.style.fill = Paint(makeColor(0x1A, 0x73, 0xE8, 0x28), true);
    p.bounds = core::Box(core::Vec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                         core::Vec2(std::max(a.x, b.x), std::max(a.y, b.y)));
    overlay->add(p);
}

} // namespace render
} // namespace r62
