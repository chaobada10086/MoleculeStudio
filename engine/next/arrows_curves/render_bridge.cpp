#include "next/arrows_curves/render_bridge.hpp"

#include <cmath>
#include <cstring>

namespace r62 {
namespace arrows {

namespace {

uint64_t fnv1a(const std::string& text, uint64_t seed) {
    uint64_t h = seed;
    for (size_t i = 0; i < text.size(); ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(text[i]));
        h *= 1099511628211ull;
    }
    return h;
}

void hashDouble(uint64_t* h, double v) {
    // Hash the BITS, so two values that print the same but differ (a -0.0 vs 0.0,
    // or a last-bit difference from a different arithmetic order) do not collide
    // silently and serve a stale mesh.
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    *h ^= bits;
    *h *= 1099511628211ull;
}

} // namespace

uint64_t arrowSignatureOf(const ArrowParams& params, double documentLineWidth) {
    // The parameter record ALREADY encodes every style and geometry field, so
    // hashing it is complete by construction: a field added to ArrowParams and to
    // encodeArrowParams() automatically enters the signature. Forgetting to update
    // a hand-written field list is the classic stale-cache bug.
    uint64_t h = arrowParamsSignature(params);
    hashDouble(&h, documentLineWidth);
    return h;
}

uint64_t curveSignatureOf(const std::vector<core::Vec2>& points,
                          const std::vector<uint8_t>& pointKind, bool closed,
                          const CurveStyleSpec& style, double documentLineWidth) {
    uint64_t h = 1469598103934665603ull;
    h = fnv1a(encodeCurveStyle(style), h);
    hashDouble(&h, documentLineWidth);
    hashDouble(&h, closed ? 1.0 : 0.0);
    hashDouble(&h, static_cast<double>(points.size()));
    for (size_t i = 0; i < points.size(); ++i) {
        hashDouble(&h, points[i].x);
        hashDouble(&h, points[i].y);
    }
    for (size_t i = 0; i < pointKind.size(); ++i) hashDouble(&h, pointKind[i]);
    return h;
}

render::Style arrowStyle(const ArrowGeometry& geometry) {
    render::Style s;
    s.stroke = render::Paint(0x000000FFu, true);
    s.strokeWidth = geometry.strokeWidth;
    s.cap = render::CapStyle::Butt;
    s.join = render::JoinStyle::Miter;
    s.fillRule = render::FillRule::NonZero;
    if (geometry.dashOn > 0.0)
        s.dash = render::DashPattern(geometry.dashOn, geometry.dashOff);
    s.fill = render::Paint(0x000000FFu, !geometry.hollow);
    // A core::LineStyle has one `dash` ENUM member and cannot express a dash
    // PATTERN in document units, which is what a width-proportional arrow dash
    // needs. The bridge therefore sets the pattern directly; the mapping from the
    // persisted enum happens once, in model.cpp. styleFromLineStyle() is not used
    // here on purpose and that reason is recorded rather than left implicit.
    return s;
}

render::Style curveStyle(const CurveGeometry& geometry) {
    render::Style s;
    s.strokeWidth = geometry.strokeWidth;
    s.cap = render::CapStyle::Round;
    s.join = render::JoinStyle::Round;
    // EvenOdd so a SELF-CROSSING freehand shape fills the way a user expects and
    // the same rule the rasterizer applies; the hole story is then consistent
    // between the two.
    s.fillRule = render::FillRule::EvenOdd;
    if (geometry.dashOn > 0.0)
        s.dash = render::DashPattern(geometry.dashOn, geometry.dashOff);
    const uint32_t colour = geometry.fillRGBA ? geometry.fillRGBA : 0x000000FFu;
    s.stroke = render::Paint(colour, true);
    s.fill = render::Paint(colour, geometry.fillEnabled);
    return s;
}

core::Error appendArrowPrimitives(const ArrowGeometry& geometry,
                                  const core::ObjectRef& source,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output list");
    if (!geometry.valid) return core::Error::success();   // nothing to draw, not an error
    const render::Style style = arrowStyle(geometry);
    const double defaultWidth = geometry.strokeWidth > 0.0 ? geometry.strokeWidth : 0.6;

    for (size_t s = 0; s < geometry.shafts.size(); ++s) {
        if (geometry.shafts[s].size() < 2) continue;
        render::Primitive p;
        p.kind = render::PrimKind::Polyline;
        p.layer = render::LayerId(layer);
        p.z = baseZ;
        p.source = source;
        p.points = geometry.shafts[s];
        p.style = style;
        p.version = version;
        p.bounds = render::geom::computeBounds(p, defaultWidth);
        out->push_back(p);
    }
    for (size_t i = 0; i < geometry.heads.size(); ++i) {
        const HeadGeometry& h = geometry.heads[i];
        if (h.outline.size() >= 3) {
            render::Primitive p;
            // A filled head is a Polygon (fillable); a hollow one is still a
            // Polygon but with fill disabled, so the outline strokes.
            p.kind = render::PrimKind::Polygon;
            p.layer = render::LayerId(layer);
            p.z = baseZ + 1;   // heads above their shaft
            p.source = source;
            p.points = h.outline;
            p.style = style;
            p.style.fill = render::Paint(0x000000FFu, h.filled && !geometry.hollow);
            p.version = version;
            p.bounds = render::geom::computeBounds(p, defaultWidth);
            out->push_back(p);
        }
        for (size_t k = 0; k + 1 < h.strokes.size(); k += 2) {
            render::Primitive p;
            p.kind = render::PrimKind::Segment;
            p.layer = render::LayerId(layer);
            p.z = baseZ + 1;
            p.source = source;
            p.points.push_back(h.strokes[k]);
            p.points.push_back(h.strokes[k + 1]);
            p.style = style;
            p.style.fill = render::Paint(0x000000FFu, false);
            p.version = version;
            p.bounds = render::geom::computeBounds(p, defaultWidth);
            out->push_back(p);
        }
    }
    for (size_t i = 0; i < geometry.markers.size(); ++i) {
        render::Primitive p;
        p.kind = render::PrimKind::Polyline;
        p.layer = render::LayerId(layer);
        p.z = baseZ + 2;   // the no-go mark reads on top of everything
        p.source = source;
        p.points = geometry.markers[i];
        p.style = style;
        p.style.strokeWidth = geometry.strokeWidth * 1.4;
        p.style.fill = render::Paint(0x000000FFu, false);
        p.version = version;
        p.bounds = render::geom::computeBounds(p, defaultWidth);
        out->push_back(p);
    }
    return core::Error::success();
}

core::Error appendCurvePrimitives(const CurveGeometry& geometry,
                                  const core::ObjectRef& source,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output list");
    if (!geometry.valid) return core::Error::success();
    const render::Style style = curveStyle(geometry);
    const double defaultWidth = geometry.strokeWidth > 0.0 ? geometry.strokeWidth : 0.6;

    if (geometry.closed && geometry.fillEnabled && geometry.contours.size() >= 1) {
        // ONE Polygon with subpaths: contours[0] is the outer boundary and every
        // later contour is a HOLE. That is exactly how T03 expresses a fill with
        // holes (glyph O/B, an annulus), so a self-crossing or holed freehand shape
        // fills correctly without any new primitive kind.
        render::Primitive p;
        p.kind = render::PrimKind::Polygon;
        p.layer = render::LayerId(layer);
        p.z = baseZ;
        p.source = source;
        p.style = style;
        p.version = version;
        for (size_t c = 0; c < geometry.contours.size(); ++c) {
            render::Subpath sp;
            sp.points = geometry.contours[c];
            sp.closed = true;
            p.subpaths.push_back(sp);
        }
        p.bounds = render::geom::computeBounds(p, defaultWidth);
        out->push_back(p);
    }

    for (size_t c = 0; c < geometry.contours.size(); ++c) {
        if (geometry.contours[c].size() < 2) continue;
        render::Primitive p;
        p.kind = render::PrimKind::Polyline;
        p.layer = render::LayerId(layer);
        p.z = baseZ + 1;
        p.source = source;
        p.points = geometry.contours[c];
        if (geometry.closed) p.points.push_back(geometry.contours[c][0]);
        p.style = style;
        p.style.fill = render::Paint(0x000000FFu, false);   // the stroke pass is unfilled
        p.version = version;
        p.bounds = render::geom::computeBounds(p, defaultWidth);
        out->push_back(p);
    }
    return core::Error::success();
}

core::Error appendArrow(const ArrowParams& params, double documentLineWidth,
                        const Tessellation& tess, const core::ObjectRef& source,
                        uint32_t layer, std::vector<render::Primitive>* out) {
    ArrowGeometry g;
    const core::Error e = buildArrowGeometry(params, documentLineWidth, tess, &g);
    if (!e.ok()) return e;
    const uint64_t version = arrowSignatureOf(params, documentLineWidth);
    return appendArrowPrimitives(g, source, version, layer, 0, out);
}

core::Error appendCurve(const std::vector<core::Vec2>& points,
                        const std::vector<uint8_t>& pointKind, bool closed,
                        const CurveStyleSpec& style, double documentLineWidth,
                        const Tessellation& tess, const core::ObjectRef& source,
                        uint32_t layer, std::vector<render::Primitive>* out) {
    CurveGeometry g;
    const core::Error e = buildCurveGeometry(points, pointKind, closed, style,
                                             documentLineWidth, tess, &g);
    if (!e.ok()) return e;
    const uint64_t version = curveSignatureOf(points, pointKind, closed, style,
                                              documentLineWidth);
    return appendCurvePrimitives(g, source, version, layer, 0, out);
}

void appendArrowHandles(const std::vector<Handle>& handles,
                        const core::ObjectRef& source,
                        const core::ViewTransform& view, uint32_t layer,
                        std::vector<render::Primitive>* out) {
    if (!out) return;
    // The handle box is sized in DEVICE pixels and converted, so it stays the same
    // physical size at every zoom - which is what a handle has to do to be
    // grabbable.
    const double half = view.deviceRadiusToDocument(3.0);
    for (size_t i = 0; i < handles.size(); ++i) {
        render::Primitive p;
        p.kind = render::PrimKind::Rect;
        p.layer = render::LayerId(layer);
        p.z = static_cast<int32_t>(i);
        p.source = source;
        p.points.push_back(core::Vec2(handles[i].point.x - half, handles[i].point.y - half));
        p.points.push_back(core::Vec2(handles[i].point.x + half, handles[i].point.y + half));
        p.style.stroke = render::Paint(0x1A73E8FFu, true);
        p.style.fill = render::Paint(0x1A73E8FFu, false);
        p.style.strokeWidth = view.deviceRadiusToDocument(1.0);
        p.version = static_cast<uint64_t>(i) + 1;
        p.bounds = render::geom::computeBounds(p, 0.6);
        out->push_back(p);
    }
}

} // namespace arrows
} // namespace r62
