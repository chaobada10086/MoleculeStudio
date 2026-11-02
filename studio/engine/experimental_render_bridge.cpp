#include "next/experimental/render_bridge.hpp"

#include "next/experimental/model.hpp"
#include "tlc_preferences.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace r62 {
namespace experimental {

namespace {

const uint32_t kLaneGuideColor = 0x808080C0u;
const uint32_t kGuideColor      = 0x404040FFu;
const uint32_t kWellColor       = 0x101010FFu;
const uint32_t kLabelColor      = 0x202020FFu;

uint32_t withOpacity(uint32_t rgba, double opacity) {
    uint32_t resolved = render::resolvePaint(rgba);
    if (opacity < 0.0) opacity = 0.0;
    if (opacity > 1.0) opacity = 1.0;
    const uint32_t alpha = static_cast<uint32_t>(opacity * 255.0 + 0.5) & 0xFFu;
    return (resolved & 0xFFFFFF00u) | alpha;
}

render::Primitive makeFilled(const std::vector<core::Vec2>& points,
                             const render::Style& style, const core::ObjectRef& source,
                             uint64_t version, uint32_t layer, int32_t z) {
    render::Primitive p;
    p.kind = render::PrimKind::Polygon;
    p.layer = render::LayerId(layer);
    p.z = z;
    p.source = source;
    p.style = style;
    p.version = version;
    p.points = points;
    render::Subpath sub;
    sub.points = points;
    sub.closed = true;
    p.subpaths.push_back(sub);
    p.bounds = render::geom::computeBounds(p, style.strokeWidth > 0.0 ? style.strokeWidth : 0.6);
    return p;
}

render::Primitive makeOutline(const std::vector<core::Vec2>& points,
                              const render::Style& style, const core::ObjectRef& source,
                              uint64_t version, uint32_t layer, int32_t z) {
    render::Primitive p;
    p.kind = render::PrimKind::Polygon;
    p.layer = render::LayerId(layer);
    p.z = z;
    p.source = source;
    p.style = style;
    p.style.fill.enabled = false;
    p.version = version;
    p.points = points;
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

void pushTextFallback(const FigureLabel& label, const render::Style& style,
                      const core::ObjectRef& source, uint64_t version, uint32_t layer,
                      int32_t z, double defaultTextSize,
                      std::vector<render::Primitive>* out) {
    render::Primitive p;
    p.kind = render::PrimKind::Text;
    p.layer = render::LayerId(layer);
    p.z = z;
    p.source = source;
    p.style = style;
    // A Text primitive is drawn from its fill paint. Leaving fill disabled (the
    // convention for a stroke-only primitive) makes T03 draw NO glyphs at all,
    // which is exactly the bug this comment exists to prevent.
    p.style.fill = render::Paint(kLabelColor, true);
    p.style.stroke.enabled = false;
    p.version = version;
    p.text = label.text;
    p.textSize = label.size > 0.0 ? label.size : defaultTextSize;
    p.points.push_back(label.position);
    // The fallback ink box is CONSERVATIVE and stated as such: without a T09
    // measurer there is no real ink extent, so culling/hit tests must over-cover
    // rather than under-cover.
    const double width = p.textSize * 0.6 * static_cast<double>(label.text.size() + 1);
    p.bounds = core::Box(core::Vec2(label.position.x, label.position.y - p.textSize),
                         core::Vec2(label.position.x + width, label.position.y + p.textSize * 0.3));
    out->push_back(p);
}

} // namespace

render::Style renderStyleFor(const PlateFigure& figure, double documentLineWidth) {
    render::Style style;
    style.strokeWidth = figure.border.width > 0.0 ? figure.border.width : documentLineWidth;
    style.stroke = render::Paint(render::resolvePaint(figure.border.colorRGBA), true);
    style.fill = render::Paint(render::resolvePaint(figure.plateFillRGBA), figure.plateFillEnabled);
    style.cap = render::CapStyle::Round;
    style.join = render::JoinStyle::Round;
    style.miterLimit = 4.0;
    style.fillRule = render::FillRule::NonZero;
    if (figure.border.dash == static_cast<uint8_t>(core::DashStyle::Dashed))
        style.dash = render::DashPattern(4.0, 2.0);
    else if (figure.border.dash == static_cast<uint8_t>(core::DashStyle::Dotted))
        style.dash = render::DashPattern(1.5, 1.5);
    return style;
}

void collectFigureLabels(const PlateFigure& figure, const PlateFrame& frame,
                         double defaultTextSize, std::vector<FigureLabel>* out) {
    if (!out) return;
    if (!figure.title.empty()) {
        FigureLabel label;
        label.text = figure.title;
        label.position = frame.pointAt(0.0, -1.18);
        label.size = defaultTextSize * 1.15;
        label.kind = "title";
        out->push_back(label);
    }
    for (size_t i = 0; i < figure.lanes.size(); ++i) {
        if (figure.lanes[i].label.empty()) continue;
        FigureLabel label;
        label.text = figure.lanes[i].label;
        label.position = frame.pointAt(figure.lanes[i].u, 1.14);
        label.size = defaultTextSize * 0.9;
        label.kind = "lane";
        out->push_back(label);
    }
    for (size_t i = 0; i < figure.spots.size(); ++i) {
        const Spot& spot = figure.spots[i];
        if (!spot.label.empty()) {
            FigureLabel label;
            label.text = spot.label;
            label.position = frame.pointAt(spot.u + spot.radiusU + 0.03, spot.v);
            label.size = defaultTextSize * 0.85;
            label.kind = "spot";
            out->push_back(label);
        }
    }
    for(const auto& placement:studio_tlc::rfLabels(figure,frame,defaultTextSize)){
        FigureLabel label;label.text=placement.text;label.kind="rf";label.position=placement.position;label.size=placement.size;out->push_back(label);
    }
}

static core::Error appendFigurePrimitivesStyled(const PlateFigure& figure, const PlateFrame& frame,
                                   const core::ObjectRef& source, uint64_t version,
                                   uint32_t layer, int32_t baseZ, double documentLineWidth,
                                   double defaultTextSize,
                                   text::GlyphCache* glyphs, const text::LayoutOptions& options,
                                   std::vector<render::Primitive>* out,bool showRf) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    BoardGeometry geometry;
    core::Error e = buildBoardGeometry(figure, frame, &geometry);
    if (!e.ok()) return e;

    const render::Style style = renderStyleFor(figure, documentLineWidth);
    core::Vec2 corners[4];
    boardCornerPolygon(frame, corners);
    std::vector<core::Vec2> board(corners, corners + 4);

    if (figure.plateFillEnabled) {
        render::Style fillStyle = style;
        fillStyle.fill = render::Paint(render::resolvePaint(figure.plateFillRGBA), true);
        out->push_back(makeFilled(board, fillStyle, source, version, layer, baseZ));
    }
    out->push_back(makeOutline(board, style, source, version, layer, baseZ + 1));

    if (figure.kind == FigureKind::TlcPlate) {
        if (figure.originDefined) {
            render::Style s = style;
            s.stroke = render::Paint(kGuideColor, true);
            out->push_back(makeSegment(frame.pointAt(-1.0, figure.originV),
                                       frame.pointAt(1.0, figure.originV), s, source, version,
                                       layer, baseZ + 2));
        }
        if (figure.frontDefined) {
            render::Style s = style;
            s.stroke = render::Paint(kGuideColor, true);
            s.dash = render::DashPattern(3.0, 2.0);
            out->push_back(makeSegment(frame.pointAt(-1.0, figure.frontV),
                                       frame.pointAt(1.0, figure.frontV), s, source, version,
                                       layer, baseZ + 2));
        }
    }

    if (figure.showLaneBorders) {
        render::Style s = style;
        s.stroke = render::Paint(kLaneGuideColor, true);
        s.strokeWidth = style.strokeWidth > 0.0 ? style.strokeWidth * 0.75 : 0.5;
        for (size_t i = 0; i < figure.lanes.size(); ++i) {
            const Lane& lane = figure.lanes[i];
            if (lane.dashedBorder)
                s.dash = render::DashPattern(2.0, 1.5);
            else
                s.dash = render::DashPattern(0.0, 0.0);
            const double half = lane.width * 0.5;
            out->push_back(makeSegment(frame.pointAt(lane.u - half, -figure.laneMarginV),
                                       frame.pointAt(lane.u - half, figure.laneMarginV),
                                       s, source, version, layer, baseZ + 3));
            out->push_back(makeSegment(frame.pointAt(lane.u + half, -figure.laneMarginV),
                                       frame.pointAt(lane.u + half, figure.laneMarginV),
                                       s, source, version, layer, baseZ + 3));
        }
    }

    if (figure.kind == FigureKind::GelElectrophoresis) {
        render::Style s = style;
        s.stroke = render::Paint(kWellColor, true);
        s.fill = render::Paint(0xFFFFFF80u, false);
        for (size_t i = 0; i < figure.lanes.size(); ++i) {
            const std::vector<core::Vec2> well =
                wellPolygon(frame, figure.lanes[i], -0.96, 0.10);
            out->push_back(makeOutline(well, s, source, version, layer, baseZ + 4));
        }
    }

    for (size_t i = 0; i < figure.bands.size(); ++i) {
        const Band& band = figure.bands[i];
        const Lane* lane = findLane(figure, band.laneId);
        if (!lane) continue;
        const std::vector<core::Vec2> poly = bandPolygon(frame, *lane, band);
        render::Style s = style;
        s.fill = render::Paint(withOpacity(band.fillRGBA, band.displayIntensity), true);
        out->push_back(makeFilled(poly, s, source, version, layer, baseZ + 5));
    }

    for (size_t i = 0; i < figure.spots.size(); ++i) {
        const Spot& spot = figure.spots[i];
        const std::vector<core::Vec2> poly =
            ellipsePolygon(frame, spot.u, spot.v, spot.radiusU, spot.radiusV, 24);
        render::Style s = style;
        s.stroke.enabled = false;
        s.fill = render::Paint(withOpacity(spot.fillRGBA, spot.opacity), true);
        out->push_back(makeFilled(poly, s, source, version, layer, baseZ + 6));
    }

    std::vector<FigureLabel> labels;
    collectFigureLabels(figure, frame, defaultTextSize, &labels);
    if(!showRf)labels.erase(std::remove_if(labels.begin(),labels.end(),[](const FigureLabel& label){return label.kind=="rf";}),labels.end());
    if (labels.empty()) return core::Error::success();

    render::Style labelStyle = style;
    labelStyle.stroke.enabled = false;
    labelStyle.fill = render::Paint(kLabelColor, true);
    if (glyphs) {
        text::TextLayoutCache cache(glyphs, 512);
        for (size_t i = 0; i < labels.size(); ++i) {
            std::vector<core::TextRun> runs;
            core::TextRun run;
            run.utf8 = labels[i].text;
            run.size = labels[i].size;
            runs.push_back(run);
            text::LayoutOptions opts = options;
            opts.defaultSize = labels[i].size > 0.0 ? labels[i].size : options.defaultSize;
            const text::SLayout* layout = 0;
            core::Error le = cache.layout(runs, labels[i].position, opts, &layout);
            if (!le.ok() || !layout) continue;
            std::vector<render::Primitive> prims;
            text::appendPrimitives(layout->layout, opts.defaultSize, kLabelColor, layer,
                                   baseZ + 20 + static_cast<int32_t>(i), &prims);
            for (size_t k = 0; k < prims.size(); ++k) {
                prims[k].source = source;
                prims[k].version = version;
                out->push_back(prims[k]);
            }
        }
    } else {
        for (size_t i = 0; i < labels.size(); ++i)
            pushTextFallback(labels[i], labelStyle, source, version, layer,
                             baseZ + 20 + static_cast<int32_t>(i), defaultTextSize, out);
    }
    return core::Error::success();
}

core::Error appendFigurePrimitives(const PlateFigure& figure,const PlateFrame& frame,
    const core::ObjectRef& source,uint64_t version,uint32_t layer,int32_t baseZ,
    double documentLineWidth,double defaultTextSize,text::GlyphCache* glyphs,
    const text::LayoutOptions& options,std::vector<render::Primitive>* out){
    return appendFigurePrimitivesStyled(figure,frame,source,version,layer,baseZ,documentLineWidth,defaultTextSize,glyphs,options,out,true);
}

core::Error appendFigurePrimitivesFor(const core::Document& document, core::ObjectId figureId,
                                      uint32_t layer, int32_t baseZ,
                                      text::GlyphCache* glyphs,
                                      const text::LayoutOptions& options,
                                      std::vector<render::Primitive>* out) {
    PlateFigure figure;
    PlateFrame frame;
    bool usedExtension = false;
    core::Error e = readFigureAndFrame(document, figureId, &figure, &frame, &usedExtension);
    if (!e.ok()) return e;
    auto show=document.extensions(figureRef(figureId)).findValue("studio.tlc.showRf");
    return appendFigurePrimitivesStyled(figure, frame, figureRef(figureId),
                                  figureObjectSignature(document, figureId), layer, baseZ,
                                  document.defaultLineWidth(), document.defaultTextSize(),
                                  glyphs, options, out,show&&*show=="1");
}

} // namespace experimental
} // namespace r62
