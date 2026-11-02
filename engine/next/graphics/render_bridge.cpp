#include "next/graphics/render_bridge.hpp"

#include "next/graphics/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace r62 {
namespace graphics {
namespace {

// Renderer-side origin for the shapes. A shape is its OWN closed loop, so it is
// a Polygon with one subpath; the dot-fill / grid / stroke paths add more.
void setStrokeStyle(render::Style* s, const ShapeParams& p, double defaultWidth) {
    const bool dashed = appearanceDashed(p.appearance) || p.line == LineVariant::Dashed;
    s->strokeWidth = p.style.width > 0.0 ? p.style.width : defaultWidth;
    s->stroke = render::Paint(render::resolvePaint(p.style.colorRGBA), true);
    if (dashed) {
        double on = 0.0, off = 0.0;
        appearanceDashPattern(p.appearance, s->strokeWidth, &on, &off);
        if (on <= 0.0) { on = 4.0 * s->strokeWidth; off = 2.0 * s->strokeWidth; }
        s->dash = render::DashPattern(on, off);
    }
    if (p.line == LineVariant::Wavy) {
        s->wavyAmplitude = s->strokeWidth * 2.0;
        s->wavyWavelength = s->wavyAmplitude * 4.0;
    }
    s->cap = render::CapStyle::Round;
    s->join = render::JoinStyle::Round;
    s->miterLimit = 4.0;
    s->fillRule = render::FillRule::NonZero;
}

void setStrokeStyle(render::Style* s, const BracketParams& p, double defaultWidth) {
    s->strokeWidth = p.style.width > 0.0 ? p.style.width : defaultWidth;
    s->stroke = render::Paint(render::resolvePaint(p.style.colorRGBA), true);
    s->cap = render::CapStyle::Round;
    s->join = render::JoinStyle::Round;
    s->miterLimit = 4.0;
}

void setStrokeStyle(render::Style* s, const TableGrid& g, double defaultWidth) {
    s->strokeWidth = g.borderWidth > 0.0 ? g.borderWidth : defaultWidth;
    s->stroke = render::Paint(0x000000FFu, true);
    s->cap = render::CapStyle::Butt;
    s->join = render::JoinStyle::Miter;
    if (g.borders.outer == TableBorderStyle::Dashed || g.borders.inner == TableBorderStyle::Dashed)
        s->dash = render::DashPattern(4.0 * s->strokeWidth, 2.0 * s->strokeWidth);
}

render::Subpath toSubpath(const Contour& c) {
    render::Subpath sp;
    sp.points = c.points;
    sp.kind = c.kind;
    sp.closed = c.closed;
    return sp;
}

render::Primitive makePoly(const Contour& c, const render::Style& style,
                           const core::ObjectRef& source, uint64_t version,
                           uint32_t layer, int32_t z, bool fill, uint32_t fillRGBA) {
    render::Primitive p;
    p.kind = c.closed ? render::PrimKind::Polygon : render::PrimKind::Polyline;
    p.layer = render::LayerId(layer);
    p.z = z;
    p.source = source;
    p.style = style;
    p.version = version;
    if (fill) {
        p.style.fill = render::Paint(render::resolvePaint(fillRGBA), true);
        render::Subpath sp = toSubpath(c);
        p.subpaths.push_back(sp);
        p.points = c.points;
    } else {
        p.style.fill.enabled = false;
        p.points = c.points;
    }
    p.bounds = render::geom::computeBounds(p, style.strokeWidth > 0.0 ? style.strokeWidth : 0.6);
    return p;
}

render::Primitive makeCurve(const Contour& c, const render::Style& style,
                            const core::ObjectRef& source, uint64_t version,
                            uint32_t layer, int32_t z) {
    render::Primitive p;
    p.kind = render::PrimKind::Curve;
    p.layer = render::LayerId(layer);
    p.z = z;
    p.source = source;
    p.style = style;
    p.style.fill.enabled = false;
    p.version = version;
    p.points = c.points;
    p.pointKind = c.kind;
    p.bounds = render::geom::computeBounds(p, style.strokeWidth > 0.0 ? style.strokeWidth : 0.6);
    return p;
}

render::Primitive makeSegment(const core::Vec2& a, const core::Vec2& b,
                              const render::Style& style, const core::ObjectRef& source,
                              uint64_t version, uint32_t layer, int32_t z) {
    render::Primitive p;
    p.kind = render::PrimKind::Segment;
    p.layer = render::LayerId(layer);
    p.z = z;
    p.source = source;
    p.style = style;
    p.style.fill.enabled = false;
    p.version = version;
    p.points.push_back(a);
    p.points.push_back(b);
    p.bounds = render::geom::computeBounds(p, style.strokeWidth > 0.0 ? style.strokeWidth : 0.6);
    return p;
}

// Dagger bars are encoded as kind==2 point pairs; expand them into segments.
void appendDaggerBars(const Contour& c, const render::Style& style,
                      const core::ObjectRef& source, uint64_t version,
                      uint32_t layer, int32_t z, std::vector<render::Primitive>* out) {
    for (size_t k = 0; k + 1 < c.points.size() && c.kind.size() == c.points.size(); ++k) {
        if (c.kind[k] == 2 && c.kind[k + 1] == 2) {
            out->push_back(makeSegment(c.points[k], c.points[k + 1], style, source, version,
                                       layer, z + 1));
            ++k;
        }
    }
}

} // namespace

render::Style renderStyleFor(const ShapeParams& p, double documentLineWidth) {
    render::Style s;
    setStrokeStyle(&s, p, documentLineWidth);
    if (p.hasInterior()) {
        const uint32_t fill = appearanceFillColor(p.appearance, p.fillRGBA);
        s.fill = render::Paint(render::resolvePaint(fill), true);
    } else {
        s.fill.enabled = false;
    }
    return s;
}

render::Style renderStyleFor(const BracketParams& p, double documentLineWidth) {
    render::Style s;
    setStrokeStyle(&s, p, documentLineWidth);
    s.fill.enabled = false;
    return s;
}

render::Style renderStyleFor(const TableGrid& g, double documentLineWidth) {
    render::Style s;
    setStrokeStyle(&s, g, documentLineWidth);
    s.fill.enabled = false;
    return s;
}

// ===========================================================================
// Shapes
// ===========================================================================
core::Error appendShapePrimitives(const ShapeParams& params, const core::ObjectRef& source,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  double documentLineWidth,
                                  std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    StrokeSpec stroke = strokeSpecFor(params, documentLineWidth);
    ShapeGeometry g;
    core::Error e = buildShapeGeometry(params, stroke, &g);
    if (!e.ok()) return e;

    const render::Style style = renderStyleFor(params, documentLineWidth);

    // Shadows first so they paint under the object.
    for (size_t i = 0; i < g.shadows.size(); ++i) {
        render::Style sh = style;
        sh.stroke = render::Paint(0x00000060u, true);
        sh.fill = render::Paint(0x00000040u, true);
        Contour c = g.shadows[i];
        c.closed = g.shadows[i].closed;
        out->push_back(makePoly(c, sh, source, version, render::layers::kShadows, baseZ,
                                c.closed && g.hasFill, 0x00000040u));
    }
    if (g.hasFill) {
        for (size_t i = 0; i < g.fills.size(); ++i)
            out->push_back(makePoly(g.fills[i], style, source, version, layer, baseZ, true,
                                    appearanceFillColor(params.appearance, params.fillRGBA)));
    }
    for (size_t i = 0; i < g.strokes.size(); ++i) {
        render::Style st = style;
        st.fill.enabled = false;
        out->push_back(makePoly(g.strokes[i], st, source, version, layer, baseZ + 1, false, 0u));
    }
    return core::Error::success();
}

// ===========================================================================
// Brackets
// ===========================================================================
core::Error appendBracketPrimitives(const BracketParams& params, const core::ObjectRef& source,
                                    uint64_t version, uint32_t layer, int32_t baseZ,
                                    double documentLineWidth, double labelTextSize,
                                    std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    StrokeSpec stroke = strokeSpecFor(params, documentLineWidth);
    BracketGeometry g;
    core::Error e = buildBracketGeometry(params, stroke, labelTextSize, &g);
    if (!e.ok()) return e;
    const render::Style style = renderStyleFor(params, documentLineWidth);

    for (size_t i = 0; i < g.runs.size(); ++i) {
        const Contour& c = g.runs[i];
        const bool curved = (params.family == BracketFamily::Curly ||
                             params.family == BracketFamily::Parenthesis);
        if (params.family == BracketFamily::Dagger) {
            Contour spine;
            for (size_t k = 0; k < c.points.size() && c.kind.size() == c.points.size(); ++k) {
                if (c.kind[k] == 2) continue;
                spine.points.push_back(c.points[k]);
                spine.kind.push_back(0);
            }
            spine.closed = false;
            if (spine.points.size() >= 2u)
                out->push_back(makePoly(spine, style, source, version, layer, baseZ, false, 0u));
            appendDaggerBars(c, style, source, version, layer, baseZ, out);
        } else if (curved) {
            out->push_back(makeCurve(c, style, source, version, layer, baseZ));
        } else {
            render::Style st = style;
            st.fill.enabled = false;
            out->push_back(makePoly(c, st, source, version, layer, baseZ, false, 0u));
        }
    }
    // Labels: real Text primitives, positioned from the CURRENT frame. The bridge
    // does not lay out glyphs; T03/T09 measure and rasterize them.
    for (size_t i = 0; i < g.labelPositions.size() && i < params.labels.size(); ++i) {
        render::Primitive t;
        t.kind = render::PrimKind::Text;
        t.layer = render::LayerId(layer);
        t.z = baseZ + 5;
        t.source = source;
        t.style = style;
        t.style.fill.enabled = false;
        t.style.stroke.enabled = false;
        t.version = version;
        t.text = params.labels[i].text;
        t.textSize = params.labels[i].size > 0.0 ? params.labels[i].size : labelTextSize;
        t.points.push_back(g.labelPositions[i]);
        t.bounds = core::Box(core::Vec2(g.labelPositions[i].x - t.textSize,
                                        g.labelPositions[i].y - t.textSize),
                             core::Vec2(g.labelPositions[i].x + t.textSize,
                                        g.labelPositions[i].y + t.textSize));
        out->push_back(t);
    }
    return core::Error::success();
}

// ===========================================================================
// Tables
// ===========================================================================
uint64_t tableSignature(const core::Document& document, core::ObjectId id) {
    TableData d;
    bool used = false;
    if (!readTable(document, id, &d, &used).ok()) return 0;
    uint64_t h = 0x7AB1Eull;
    struct Mix {
        static uint64_t mix(uint64_t h2, uint64_t v) {
            h2 ^= v + 0x9E3779B97F4A7C15ull + (h2 << 6) + (h2 >> 2);
            return h2;
        }
        static uint64_t bits(double v) {
            uint64_t u = 0;
            std::memcpy(&u, &v, sizeof(u));
            return u;
        }
    };
    h = Mix::mix(h, Mix::bits(d.grid.frame.min.x));
    h = Mix::mix(h, Mix::bits(d.grid.frame.min.y));
    h = Mix::mix(h, Mix::bits(d.grid.frame.max.x));
    h = Mix::mix(h, Mix::bits(d.grid.frame.max.y));
    for (size_t i = 0; i < d.grid.rowHeights.size(); ++i) h = Mix::mix(h, Mix::bits(d.grid.rowHeights[i]));
    for (size_t i = 0; i < d.grid.columnWidths.size(); ++i) h = Mix::mix(h, Mix::bits(d.grid.columnWidths[i]));
    h = Mix::mix(h, static_cast<uint64_t>(d.grid.borders.outer));
    h = Mix::mix(h, static_cast<uint64_t>(d.grid.borders.inner));
    h = Mix::mix(h, Mix::bits(d.grid.borderWidth));
    if (!d.grid.title.empty())
        for (size_t i = 0; i < d.grid.title.size(); ++i)
            h = Mix::mix(h, static_cast<unsigned char>(d.grid.title[i]));
    for (size_t i = 0; i < d.cells.size(); ++i) {
        h = Mix::mix(h, static_cast<uint64_t>(d.cells[i].kind));
        h = Mix::mix(h, d.cells[i].text);
        h = Mix::mix(h, d.cells[i].embed.id);
        h = Mix::mix(h, static_cast<uint64_t>(d.cells[i].embed.kind));
        h = Mix::mix(h, Mix::bits(d.cells[i].textOffset.x));
        h = Mix::mix(h, Mix::bits(d.cells[i].textOffset.y));
        if (d.cells[i].kind == CellKind::Text && d.cells[i].text != core::kInvalidObjectId) {
            const std::string s = document.plainText(d.cells[i].text);
            for (size_t k = 0; k < s.size(); ++k)
                h = Mix::mix(h, static_cast<unsigned char>(s[k]));
        }
        h = Mix::mix(h, 0x5EEDull);
    }
    return h;
}

core::Error appendTablePrimitives(const core::Document& document, core::ObjectId id,
                                  uint64_t version, uint32_t layer, int32_t baseZ,
                                  double documentLineWidth,
                                  std::vector<render::Primitive>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    TableData d;
    bool used = false;
    core::Error e = readTable(document, id, &d, &used);
    if (!e.ok()) return e;
    const render::Style style = renderStyleFor(d.grid, documentLineWidth);
    const core::ObjectRef source(core::ObjectKind::Table, id);

    // Cell outlines. Borders::None suppresses the corresponding lines rather than
    // drawing a zero-width stroke (which would still cost a primitive).
    const bool outer = d.grid.borders.outer != TableBorderStyle::None;
    const bool inner = d.grid.borders.inner != TableBorderStyle::None;
    for (uint32_t r = 0; r < d.grid.rows(); ++r) {
        for (uint32_t c = 0; c < d.grid.columns(); ++c) {
            const core::Box b = d.grid.cellBox(r, c);
            if (outer || inner) {
                render::Primitive p;
                p.kind = render::PrimKind::Rect;
                p.layer = render::LayerId(layer);
                p.z = baseZ;
                p.source = source;
                p.style = style;
                p.style.fill.enabled = false;
                p.version = version;
                p.points.push_back(b.min);
                p.points.push_back(b.max);
                p.bounds = render::geom::computeBounds(p, style.strokeWidth > 0.0 ? style.strokeWidth : 0.6);
                out->push_back(p);
            }
            // Text cells are emitted as Text primitives; the CELL TEXT objects also
            // live in the text slab, so a consumer that draws the text slab will see
            // them once. Emitting them here as well is what makes a table renderable
            // by a caller that only walks tables; the scene builder must pick ONE
            // (see RESULTS.md, the double-draw note).
            const TableCell* cell = d.at(r, c);
            if (!cell || cell->kind != CellKind::Text || cell->text == core::kInvalidObjectId) continue;
            const std::string s = document.plainText(cell->text);
            if (s.empty()) continue;
            const core::Vec2 centre((b.min.x + b.max.x) * 0.5 + cell->textOffset.x,
                                    (b.min.y + b.max.y) * 0.5 + cell->textOffset.y);
            render::Primitive t;
            t.kind = render::PrimKind::Text;
            t.layer = render::LayerId(layer);
            t.z = baseZ + 2;
            t.source = core::ObjectRef(core::ObjectKind::Text, cell->text);
            t.style = style;
            t.style.fill = render::Paint(0x000000FFu, true);
            t.style.stroke.enabled = false;
            t.version = version;
            t.text = s;
            t.textSize = 0.0;   // 0 == the document default, resolved by T03
            t.points.push_back(centre);
            const double pad = 12.0;
            t.bounds = core::Box(core::Vec2(centre.x - pad, centre.y - pad),
                                 core::Vec2(centre.x + pad, centre.y + pad));
            out->push_back(t);
        }
    }
    if (!d.grid.title.empty()) {
        render::Primitive t;
        t.kind = render::PrimKind::Text;
        t.layer = render::LayerId(layer);
        t.z = baseZ + 6;
        t.source = source;
        t.style = style;
        t.style.fill = render::Paint(0x000000FFu, true);
        t.style.stroke.enabled = false;
        t.version = version;
        t.text = d.grid.title;
        t.textSize = 0.0;
        t.points.push_back(core::Vec2(d.grid.frame.min.x, d.grid.frame.min.y - 6.0));
        const double pad = 20.0;
        t.bounds = core::Box(core::Vec2(d.grid.frame.min.x - pad, d.grid.frame.min.y - 6.0 - pad),
                             core::Vec2(d.grid.frame.max.x + pad, d.grid.frame.min.y - 6.0 + pad));
        out->push_back(t);
    }
    return core::Error::success();
}

// ===========================================================================
// Convenience wrappers
// ===========================================================================
core::Error appendShapePrimitivesFor(const core::Document& document, core::ObjectId id,
                                     uint32_t layer, int32_t baseZ,
                                     std::vector<render::Primitive>* out) {
    ShapeParams p;
    bool used = false;
    core::Error e = readShape(document, id, &p, &used);
    if (!e.ok()) return e;
    const uint64_t version = shapeSignature(p, document.defaultLineWidth());
    return appendShapePrimitives(p, core::ObjectRef(core::ObjectKind::Arrow, id), version,
                                 layer, baseZ, document.defaultLineWidth(), out);
}

core::Error appendBracketPrimitivesFor(const core::Document& document, core::ObjectId id,
                                       uint32_t layer, int32_t baseZ, double labelTextSize,
                                       std::vector<render::Primitive>* out) {
    BracketParams p;
    bool used = false;
    core::Error e = readBracket(document, id, &p, &used);
    if (!e.ok()) return e;
    const uint64_t version = bracketSignature(p, document.defaultLineWidth());
    return appendBracketPrimitives(p, core::ObjectRef(core::ObjectKind::Bracket, id), version,
                                   layer, baseZ, document.defaultLineWidth(), labelTextSize, out);
}

core::Error appendTablePrimitivesFor(const core::Document& document, core::ObjectId id,
                                     uint32_t layer, int32_t baseZ,
                                     std::vector<render::Primitive>* out) {
    const uint64_t version = tableSignature(document, id);
    if (version == 0ull) return core::Error(core::ErrorCode::NotFound, "No live table with that id");
    return appendTablePrimitives(document, id, version, layer, baseZ,
                                document.defaultLineWidth(), out);
}

} // namespace graphics
} // namespace r62
