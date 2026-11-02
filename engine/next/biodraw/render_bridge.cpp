// =============================================================================
// re_round62_viewer/next/biodraw/render_bridge.cpp
//
// T14 - geometry to T03 primitives, plus a GPU-free SVG evidence writer.
// =============================================================================

#include "next/biodraw/render_bridge.hpp"
#include "next/biodraw/model.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace r62 {
namespace biodraw {

bool requestBioRenderCompatibility(int major, int minor) {
    return render::requestRenderCompatibility(major, minor);
}

namespace {

// T03 layer band for BioDraw CONTENT. It sits on the content band, below every
// overlay, so a selection highlight is always drawn over a figure.
render::LayerId contentLayer() {
    return render::LayerId(render::layers::kContent);
}

bool isQuadContour(const BioContour& contour) {
    for (size_t i = 0; i < contour.kind.size(); ++i)
        if (contour.kind[i] == kPointQuad) return true;
    return false;
}

} // namespace

render::Style bioRenderStyle(const BioShapeParams& params, const core::LineStyle& style,
                             uint32_t fillRGBA, double documentLineWidth) {
    render::Style out = render::styleFromLineStyle(style, documentLineWidth);
    out.strokeWidth = resolveStrokeWidth(params, documentLineWidth);
    out.stroke = render::Paint(render::resolvePaint(style.colorRGBA), true);
    out.fill = render::Paint(render::resolvePaint(fillRGBA), true);
    // EvenOdd: a figure made of several closed contours uses the odd-even rule so
    // a nucleus drawn INSIDE a body subtracts rather than merges. T03 documents
    // subpaths + fillRule as exactly this mechanism.
    out.fillRule = render::FillRule::EvenOdd;
    out.join = render::JoinStyle::Round;
    out.cap = render::CapStyle::Round;
    return out;
}

core::Error appendBioPrimitives(const BioGeometry& geometry, const core::ObjectRef& source,
                                uint64_t version, render::Scene* scene) {
    if (!scene) return core::Error(core::ErrorCode::OutOfRange, "null scene");
    const BioShapeParams& params = geometry.params;

    for (size_t i = 0; i < geometry.elements.size(); ++i) {
        const BioSubElement& element = geometry.elements[i];
        const uint32_t rgba = resolveSubElementColor(params, element.key);
        for (size_t k = 0; k < element.contours.size(); ++k) {
            const BioContour& contour = element.contours[k];
            if (contour.points.size() < 2) continue;
            render::Primitive primitive;
            primitive.source = source;
            primitive.layer = contentLayer();
            primitive.z = static_cast<int32_t>(i);
            primitive.version = version;
            primitive.style = bioRenderStyle(params, params.style, rgba, geometry.strokeWidth);
            if (isQuadContour(contour)) {
                // Published as a Curve with T03's PointKind, so T03 flattens it.
                primitive.kind = render::PrimKind::Curve;
                primitive.points = contour.points;
                primitive.pointKind.reserve(contour.kind.size());
                for (size_t j = 0; j < contour.kind.size(); ++j)
                    primitive.pointKind.push_back(contour.kind[j] == kPointQuad
                                                     ? static_cast<uint8_t>(render::PointKind::QuadControl)
                                                     : static_cast<uint8_t>(render::PointKind::Corner));
            } else if (contour.closed && contour.filled) {
                primitive.kind = render::PrimKind::Polygon;
                render::Subpath subpath;
                subpath.points = contour.points;
                subpath.closed = true;
                primitive.subpaths.push_back(subpath);
                // A FILLED contour also gets its outline stroked, which is how a
                // membrane head reads as a ring rather than a flat dot.
                primitive.points = contour.points;
                primitive.style.stroke.enabled = true;
            } else {
                primitive.kind = render::PrimKind::Polyline;
                primitive.points = contour.points;
                primitive.style.fill.enabled = false;
            }
            // The bounds are the contour's own box inflated by half the stroke
            // width, matching bioShapeBounds so culling and hit testing agree.
            core::Box box(contour.points[0], contour.points[0]);
            for (size_t j = 1; j < contour.points.size(); ++j) box.add(contour.points[j]);
            const double inflate = 0.5 * geometry.strokeWidth;
            box.min.x -= inflate; box.min.y -= inflate;
            box.max.x += inflate; box.max.y += inflate;
            primitive.bounds = box;
            scene->add(primitive);
        }
    }
    return core::Error::success();
}

core::Error appendBioPrimitivesFor(const core::Document& document, core::ObjectId id,
                                   render::Scene* scene) {
    if (!scene) return core::Error(core::ErrorCode::OutOfRange, "null scene");
    if (!isBioShape(document, id)) return core::Error::success();
    BioShapeParams params;
    const core::Error error = readBioShape(document, id, &params, 0);
    if (!error.ok()) return error;
    const BioGeometry geometry = buildGlyphGeometry(params, document.defaultLineWidth());
    return appendBioPrimitives(geometry, core::ObjectRef(core::ObjectKind::BioShape, id),
                               bioShapeSignature(document, id), scene);
}

// ---------------------------------------------------------------------------
// SVG evidence writer
// ---------------------------------------------------------------------------
// Deliberately NOT a second renderer: it walks the SAME BioContour list the bridge
// publishes and emits SVG, so what is archived is what would be drawn. It exists
// so the geometry can be inspected on a machine with no GPU and archived in a
// diffable text form.
namespace {

std::string colourToCss(uint32_t rgba) {
    const uint32_t resolved = render::resolvePaint(rgba);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                  render::colorRed(resolved), render::colorGreen(resolved),
                  render::colorBlue(resolved));
    return std::string(buffer);
}

std::string number(double v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", v);
    return std::string(buffer);
}

} // namespace

std::string bioGeometryToSvg(const BioGeometry& geometry, const std::string& title,
                             double padding) {
    const core::Box bounds = geometry.hitBounds;
    const double minX = bounds.min.x - padding;
    const double minY = bounds.min.y - padding;
    const double width = (bounds.max.x - bounds.min.x) + 2.0 * padding;
    const double height = (bounds.max.y - bounds.min.y) + 2.0 * padding;

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
        << number(minX) << " " << number(minY) << " " << number(width) << " "
        << number(height) << "\" width=\"" << number(width) << "\" height=\""
        << number(height) << "\">\n";
    out << "  <title>" << title << "</title>\n";
    out << "  <rect x=\"" << number(minX) << "\" y=\"" << number(minY)
        << "\" width=\"" << number(width) << "\" height=\"" << number(height)
        << "\" fill=\"#FFFFFF\"/>\n";

    const BioShapeParams& params = geometry.params;
    for (size_t i = 0; i < geometry.elements.size(); ++i) {
        const BioSubElement& element = geometry.elements[i];
        const uint32_t rgba = resolveSubElementColor(params, element.key);
        for (size_t k = 0; k < element.contours.size(); ++k) {
            const BioContour& contour = element.contours[k];
            if (contour.points.size() < 2) continue;
            std::ostringstream path;
            path << "M " << number(contour.points[0].x) << " " << number(contour.points[0].y);
            for (size_t j = 1; j < contour.points.size(); ++j) {
                if (contour.kind[j] == kPointQuad && j + 1 < contour.points.size()) {
                    path << " Q " << number(contour.points[j].x) << " "
                         << number(contour.points[j].y) << " "
                         << number(contour.points[j + 1].x) << " "
                         << number(contour.points[j + 1].y);
                    ++j;
                } else {
                    path << " L " << number(contour.points[j].x) << " "
                         << number(contour.points[j].y);
                }
            }
            if (contour.closed) path << " Z";
            out << "  <path d=\"" << path.str() << "\" fill=\""
                << (contour.filled ? colourToCss(rgba) : std::string("none")) << "\" stroke=\""
                << colourToCss(params.style.colorRGBA) << "\" stroke-width=\""
                << number(geometry.strokeWidth) << "\"/>\n";
        }
    }
    out << "</svg>\n";
    return out.str();
}

} // namespace biodraw
} // namespace r62
