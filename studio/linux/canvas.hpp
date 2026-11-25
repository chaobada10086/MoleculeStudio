#pragma once

#include "interop.hpp"
#include "cairo_scene.hpp"
#include "next/render/scene_api.hpp"
#include <gtk/gtk.h>
#include <png.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>
#include <numeric>

namespace linux_canvas {
// Keep the device batch for stationary content while selection/hotspot overlays change.
class ContentCache {
    r62::render::Batcher batcher_;
    r62::render::Batch batch_;
    r62::render::View view_;
    uint64_t serial_=0, generation_=0;
    bool valid_=false;
    bool dragPreview_=false;
public:
    const r62::render::Batch* current(const r62::render::Scene& scene, const r62::render::View& view, uint64_t serial) const {
        if (!valid_ || serial_ != serial || generation_ != scene.generation() || !view.viewportEquals(view_) ||
            view.defaultStrokeWidth != view_.defaultStrokeWidth || view.defaultTextSize != view_.defaultTextSize) return nullptr;
        return &batch_;
    }
    explicit ContentCache(bool dragPreview=false) : dragPreview_(dragPreview) {}
    bool drawSelectionAt(cairo_t* cr, const r62::render::Scene& scene, const r62::render::View& view,
                         uint64_t serial, const std::vector<uint8_t>& mask) {
        if (!valid_ || serial != serial_ || generation_ != scene.generation() || mask.size() != scene.size() ||
            view.defaultStrokeWidth != view_.defaultStrokeWidth || view.defaultTextSize != view_.defaultTextSize) return false;
        if (view.transform.docPerDevice() != view_.transform.docPerDevice()) {
            // Straight segments have the same world geometry at every zoom. Curves
            // and other outlines may need a different flattening tolerance.
            for (size_t i = 0; i < mask.size(); ++i) {
                if (!mask[i]) continue;
                const auto& p = scene.primitives()[i];
                if ((p.kind != r62::render::PrimKind::Segment && p.kind != r62::render::PrimKind::Polyline) ||
                    p.points.size() != 2 || !p.subpaths.empty() || p.style.wavyAmplitude > 0) return false;
            }
        }
        r62::render::Batch batch;
        batcher_.buildBatch(scene, view, true, &batch, nullptr, true);
        linux_cairo::draw(cr, scene, view, batch, true, &mask);
        return true;
    }
    const r62::render::Batch& prepare(const r62::render::Scene& scene, const r62::render::View& view,
              r62::render::geom::TextMeasurer* measurer, uint64_t serial) {
        if(dragPreview_ || !valid_ || serial_!=serial || generation_!=scene.generation() || !view.viewportEquals(view_) ||
           view.defaultStrokeWidth!=view_.defaultStrokeWidth || view.defaultTextSize!=view_.defaultTextSize){
            batcher_.sync(scene,view,measurer,nullptr,true);
            batcher_.buildBatch(scene,view,true,&batch_,nullptr,true);
            view_=view;serial_=serial;generation_=scene.generation();valid_=true;
        }
        return batch_;
    }
    void draw(cairo_t* cr, const r62::render::Scene& scene, const r62::render::View& view,
              r62::render::geom::TextMeasurer* measurer, uint64_t serial) {
        linux_cairo::draw(cr,scene,view,prepare(scene,view,measurer,serial),dragPreview_);
    }
};

// Overscan interactive pan and wheel previews. The idle frame always redraws vectors.
class PanCache {
    linux_cairo::Surface pixels_{nullptr, cairo_surface_destroy};
    r62::render::View view_;
    uint64_t serial_ = 0, generation_ = 0;
    int factor_ = 0;
    static constexpr int margin = 192;
public:
    struct ProjectedSelection {
        const r62::render::Scene& scene;
        const r62::render::View& view;
        const r62::render::Batch* batch;
        const std::vector<uint8_t>& mask;
        ContentCache* content;
        uint64_t serial;
    };
    explicit PanCache(bool transparent=false) : transparent_(transparent) {}
private:
    const bool transparent_;
public:
    bool wheel = false;
    void clear() { pixels_.reset(); }
    static void drawVectors(cairo_t* cr, const r62::render::Scene& scene,const r62::render::View& view,
                            r62::render::geom::TextMeasurer* measurer,bool preview,const std::vector<size_t>* subset) {
        if (!subset) { linux_cairo::draw(cr,scene,view,measurer,preview);return; }
        r62::render::Scene part;
        part.reserve(subset->size());
        for (auto i:*subset) part.add(scene.primitives()[i]);
        linux_cairo::draw(cr,part,view,measurer,preview);
    }

    bool draw(cairo_t* cr, const r62::render::Scene& scene, const r62::render::View& view,
              r62::render::geom::TextMeasurer* measurer, uint64_t serial, int factor,
              const ProjectedSelection* projected = nullptr,const std::vector<size_t>* subset=nullptr) {
        const int width = int(view.widthPx) + 2 * margin, height = int(view.heightPx) + 2 * margin;
        if (factor < 1 || double(width) * height * factor * factor > 16e6 || view.clipEnabled) return false;
        auto shift = view.transform.toDevice(view_.transform.center) - view_.transform.viewportCenter;
        auto comparable = view;
        comparable.transform.center = view_.transform.center;
        double ratio = 1.;
        if (wheel && pixels_) {
            ratio = view.transform.scale / view_.transform.scale;
            comparable.transform.scale = view_.transform.scale;
        }
        auto left = view.transform.viewportCenter.x + shift.x - ratio * (view_.transform.viewportCenter.x + margin);
        auto top = view.transform.viewportCenter.y + shift.y - ratio * (view_.transform.viewportCenter.y + margin);
        if (!pixels_ || serial != serial_ || scene.generation() != generation_ || factor != factor_ ||
                !view_.viewportEquals(comparable) || view.defaultStrokeWidth != view_.defaultStrokeWidth ||
                view.defaultTextSize != view_.defaultTextSize || ratio > 2.5 || left > 0 || top > 0 ||
                left + width * ratio < view.widthPx || top + height * ratio < view.heightPx) {
            pixels_.reset(cairo_image_surface_create(transparent_ ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, width * factor, height * factor));
            cairo_surface_set_device_scale(pixels_.get(), factor, factor);
            auto* buffer = cairo_create(pixels_.get());
            if (!transparent_) {
                cairo_set_source_rgb(buffer, 1, 1, 1);
                cairo_paint(buffer);
            }
            auto expanded = view;
            expanded.widthPx = width;
            expanded.heightPx = height;
            expanded.transform.viewportCenter = expanded.transform.viewportCenter + r62::core::Vec2(margin, margin);
            if (projected && projected->batch && projected->mask.size() == projected->scene.size()) {
                // All projected ink was in the source viewport. Only its origin changes.
                const auto origin = expanded.transform.toDevice(projected->view.transform.toDocument({0,0}));
                cairo_translate(buffer, origin.x, origin.y);
                linux_cairo::draw(buffer, projected->scene, projected->view, *projected->batch, true, &projected->mask);
            } else if (!projected || !projected->content || !projected->content->drawSelectionAt(
                    buffer, projected->scene, expanded, projected->serial, projected->mask)) {
                drawVectors(buffer,scene,expanded,measurer,transparent_,subset);
            }
            const auto status = cairo_status(buffer);
            cairo_destroy(buffer);
            if (status != CAIRO_STATUS_SUCCESS) { clear(); return false; }
            view_ = view;
            serial_ = serial;
            generation_ = scene.generation();
            factor_ = factor;
            ratio = 1.;
            left = top = -margin;
        }
        if (!cr) return true; // Idle preparation only populates the surface.
        cairo_save(cr);
        cairo_translate(cr, std::round(left * factor) / factor, std::round(top * factor) / factor);
        cairo_scale(cr, ratio, ratio);
        cairo_set_source_surface(cr, pixels_.get(), 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), ratio == 1. ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
        cairo_restore(cr);
        return true;
    }
};
// Consecutive runs in paint order can be composited independently while the
// selected objects translate. Stretched bonds use the engine's current geometry.
class PartialMoveLayers {
    struct Layer {
        uint8_t motion;
        std::vector<size_t> indices;
        std::vector<uint8_t> mask;
        PanCache cache{true};
        explicit Layer(uint8_t kind):motion(kind){}
    };
    std::vector<Layer> layers_;
    std::vector<uint8_t> motion_;
    uint64_t generation_ = 0;
    int factor_ = 0;
    int width_ = 0, height_ = 0;
    size_t nextLayer_ = 0;
public:
    void clear() { layers_.clear(); motion_.clear(); nextLayer_ = 0; }
    bool prepare(const r62::integration::Session& s, const std::vector<uint8_t>& motion, int factor) {
        if (!layers_.empty() && generation_ == s.scene.generation() && factor_ == factor &&
            width_ == s.renderView.widthPx && height_ == s.renderView.heightPx && motion_ == motion)
            return true;
        clear();
        std::vector<size_t> order(motion.size());
        std::iota(order.begin(), order.end(), size_t{0});
        const auto& primitives=s.scene.primitives();
        const auto less=[&](size_t a,size_t b){
            return std::tie(primitives[a].layer.value,primitives[a].z) < std::tie(primitives[b].layer.value,primitives[b].z);
        };
        if (!std::is_sorted(order.begin(),order.end(),less)) std::stable_sort(order.begin(),order.end(),less);
        size_t surfaces=0;
        for (auto i:order) {
            if (layers_.empty() || layers_.back().motion!=motion[i]) {
                if (motion[i]!=2) ++surfaces;
                if (layers_.size()>=8 || surfaces*(s.renderView.widthPx+384)*(s.renderView.heightPx+384)*factor*factor>32e6) {
                    clear();return false;
                }
                layers_.emplace_back(motion[i]);
            }
            layers_.back().indices.push_back(i);
        }
        for (auto& layer:layers_) {
            if (layer.motion==2) continue;
            layer.mask.assign(s.gpuScene.size(),0);
            for (auto i:layer.indices) layer.mask[i+2]=1;
        }
        motion_ = motion;
        width_ = s.renderView.widthPx;
        height_ = s.renderView.heightPx;
        generation_ = s.scene.generation();
        factor_ = factor;
        return !layers_.empty();
    }
    bool prepareSelected(const r62::integration::Session& s, int factor) {
        using namespace r62::core;
        if (s.gpuScene.size() != s.scene.size()+2 ||
            !std::all_of(s.selected.begin(), s.selected.end(), [](ObjectRef ref) {
                return ref.kind == ObjectKind::Atom || ref.kind == ObjectKind::Bond;
            })) return false;
        std::unordered_set<ObjectRef,studioio::ObjectRefHash> selected;
        studioio::collectExpandedObjects(s.doc, s.selected, false, selected);
        std::vector<uint8_t> motion(s.scene.size(), 0);
        for (size_t i=0; i<motion.size(); ++i) {
            const auto ref = s.scene.primitives()[i].source;
            if (selected.count(ref)) motion[i] = 1;
            else if (ref.kind == ObjectKind::Bond) {
                if (const auto* bond = s.doc.bonds().alive(ref.id))
                    if (selected.count({ObjectKind::Atom,bond->beginAtom}) != selected.count({ObjectKind::Atom,bond->endAtom}))
                        motion[i] = 2;
            }
        }
        // Whole-document motion uses the separate projected move cache.
        if (std::all_of(motion.begin(),motion.end(),[](uint8_t value){return value==1;})) return false;
        if (!prepare(s,motion,factor)) return false;
        nextLayer_ = 0;
        return true;
    }
    bool warmNext(const r62::integration::Session& s, int factor, const r62::render::Batch* batch, ContentCache& content) {
        while (nextLayer_ < layers_.size()) {
            auto& layer = layers_[nextLayer_++];
            if (layer.motion == 2) continue;
            const PanCache::ProjectedSelection projected{s.gpuScene,s.renderView,batch,layer.mask,&content,s.serial};
            layer.cache.draw(nullptr,s.scene,s.renderView,s.measurer.get(),s.serial,factor,&projected,&layer.indices);
            break;
        }
        return nextLayer_ < layers_.size();
    }
    void draw(cairo_t* cr, r62::integration::Session& s, const r62::render::View& view, int factor,
              const r62::render::Batch* batch, ContentCache& content) {
        for (auto& layer:layers_) {
            if (layer.motion==2) {
                r62::render::Scene changing;
                for (auto i:layer.indices) changing.add(s.dragScene.primitives()[i+2]);
                linux_cairo::draw(cr,changing,view,s.measurer.get());
                continue;
            }
            auto shifted=view;
            if (layer.motion==1) shifted.transform.center=shifted.transform.center-s.projectedMoveOffset();
            const PanCache::ProjectedSelection projected{s.gpuScene,view,batch,layer.mask,&content,s.serial};
            if (!layer.cache.draw(cr,s.scene,shifted,s.measurer.get(),s.serial,factor,&projected,&layer.indices))
                PanCache::drawVectors(cr,s.scene,shifted,s.measurer.get(),false,&layer.indices);
        }
    }
};
inline r62::core::Error paint(cairo_t* cr, const r62::render::Scene& scene,
                             const r62::render::Scene* overlay, const r62::render::View& view,
                             r62::render::geom::TextMeasurer* measurer, ContentCache* cache=nullptr, uint64_t serial=0, PanCache* pan=nullptr, int factor=1, bool dragPreview=false) {
    cairo_save(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    if (!pan || !pan->draw(cr,scene,view,measurer,serial,factor)) {
        if (cache) cache->draw(cr,scene,view,measurer,serial);
        else linux_cairo::draw(cr,scene,view,measurer,dragPreview);
    }
    if (overlay) linux_cairo::draw(cr, *overlay, view, measurer);
    cairo_restore(cr);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
        return {r62::core::ErrorCode::IoError, cairo_status_to_string(cairo_status(cr))};
    return {};
}
// Complex vector paths on X11 can require thousands of server-side operations.
// Rasterize at the monitor's native pixel size, then present the frame once.
class FrameBuffer {
    linux_cairo::Surface surface_{nullptr, cairo_surface_destroy};
    std::unique_ptr<ContentCache> selectionPreviewCache_;
    struct SelectionFrame {
        using Box = r62::core::Box;
        static Box empty() {
            const auto infinity = std::numeric_limits<double>::infinity();
            return {{infinity,infinity},{-infinity,-infinity}};
        }
        linux_cairo::Surface pixels{nullptr,cairo_surface_destroy};
        linux_cairo::Surface scratch{nullptr,cairo_surface_destroy};
        std::vector<Box> bounds;
        std::vector<uint8_t> movingMask;
        std::vector<uint8_t> visible;
        Box previous = empty();
        r62::render::View view;
        uint64_t serial = 0;
        int factor = 0;

        bool draw(cairo_t* cr, const r62::render::Scene& scene, const r62::render::View& next,
                  const r62::render::Batch& batch, const std::vector<uint8_t>& moving,
                  uint64_t nextSerial, int scale) {
            const bool reset = !pixels || serial != nextSerial || scale != factor || !view.viewportEquals(next) ||
                view.defaultTextSize != next.defaultTextSize || view.defaultStrokeWidth != next.defaultStrokeWidth ||
                movingMask != moving;
            if (reset) {
                pixels.reset(cairo_image_surface_create(CAIRO_FORMAT_RGB24, int(next.widthPx)*scale, int(next.heightPx)*scale));
                scratch.reset(cairo_image_surface_create(CAIRO_FORMAT_RGB24, int(next.widthPx)*scale, int(next.heightPx)*scale));
                if (cairo_surface_status(pixels.get()) != CAIRO_STATUS_SUCCESS ||
                    cairo_surface_status(scratch.get()) != CAIRO_STATUS_SUCCESS) return false;
                cairo_surface_set_device_scale(pixels.get(),scale,scale);
                cairo_surface_set_device_scale(scratch.get(),scale,scale);
                movingMask=moving;
                bounds.assign(moving.size(),empty());
                visible.resize(moving.size());
                view=next; serial=nextSerial; factor=scale;
            } else {
                for (size_t i=0; i<moving.size(); ++i) if (moving[i]) bounds[i]=empty();
            }
            // Bound the actual projected ink, including glyph contours and stroke caps.
            for (const auto& range : batch.triangleRanges) {
                const auto i = range.primitiveIndex;
                if (!reset && !moving[i]) continue;
                if (range.count) {
                    const auto* points = batch.triangleVertices.data() + size_t(range.first)*2;
                    for (int j=0; j<range.count; ++j) bounds[i].add({points[2*j],points[2*j+1]});
                } else {
                    for (const auto& path : scene.primitives()[i].subpaths)
                        for (const auto& p : path.points) bounds[i].add(view.transform.toDevice(p));
                }
            }
            auto current = empty();
            for (size_t i=0; i<moving.size(); ++i) {
                const auto& p = scene.primitives()[i];
                if ((reset || moving[i]) && linux_image::isImage(p)) {
                    for (auto j : {0,2,3}) bounds[i].add(view.transform.toDevice(p.points[j]));
                    bounds[i].add(view.transform.toDevice(p.points[2]+p.points[3]-p.points[0]));
                }
                if (moving[i] && !bounds[i].empty()) { current.add(bounds[i].min); current.add(bounds[i].max); }
            }
            auto damage = current;
            if (!previous.empty()) { damage.add(previous.min); damage.add(previous.max); }
            previous = current;
            if (reset) damage = {{0,0},{view.widthPx,view.heightPx}};
            if (!damage.empty()) {
                damage.min={std::max(0.,std::floor(damage.min.x)-2),std::max(0.,std::floor(damage.min.y)-2)};
                damage.max={std::min(double(view.widthPx),std::ceil(damage.max.x)+2),std::min(double(view.heightPx),std::ceil(damage.max.y)+2)};
                if (!damage.empty()) {
                    // Cairo can rasterize a clipped edge slightly differently from the full
                    // path. Draw intersecting paths unclipped and copy back just the damage.
                    auto* frame = cairo_create(scratch.get());
                    cairo_rectangle(frame,damage.min.x,damage.min.y,damage.width(),damage.height());
                    cairo_set_source_rgb(frame,1,1,1);
                    cairo_fill(frame);
                    for (size_t i=0; i<bounds.size(); ++i) {
                        const auto& box=bounds[i];
                        visible[i]=box.max.x>=damage.min.x-1 && box.max.y>=damage.min.y-1 &&
                            box.min.x<=damage.max.x+1 && box.min.y<=damage.max.y+1;
                    }
                    linux_cairo::draw(frame,scene,view,batch,true,&visible);
                    const auto status = cairo_status(frame);
                    cairo_destroy(frame);
                    if (status != CAIRO_STATUS_SUCCESS) return false;
                    auto* retained=cairo_create(pixels.get());
                    cairo_set_source_surface(retained,scratch.get(),0,0);
                    cairo_rectangle(retained,damage.min.x,damage.min.y,damage.width(),damage.height());
                    cairo_fill(retained);
                    const auto copied=cairo_status(retained);
                    cairo_destroy(retained);
                    if (copied != CAIRO_STATUS_SUCCESS) return false;
                }
            }
            cairo_save(cr);
            cairo_set_source_surface(cr,pixels.get(),0,0);
            cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
            return true;
        }
    };
    std::unique_ptr<SelectionFrame> selectionFrame_;
    r62::render::View savedView_;
    uint64_t savedSerial_ = 0, savedGeneration_ = 0;
    int savedFactor_ = 0;
    bool entireDrawing_ = false;
    r62::render::Scene paper_;


    struct Refinement {
        linux_cairo::Surface pixels{nullptr, cairo_surface_destroy};
        std::vector<size_t> order;
        size_t next = 0;
        r62::render::View view;
        int factor = 0;
        uint64_t serial = 0, generation = 0;
    };
    std::unique_ptr<Refinement> refinement_;
    std::unique_ptr<Refinement> overview_;
    r62::core::Box drawingBounds_;

    bool overviewReady() const {
        return overview_ && overview_->serial == savedSerial_ && overview_->generation == savedGeneration_ &&
            overview_->factor == savedFactor_ && overview_->next == overview_->order.size() &&
            cairo_surface_status(overview_->pixels.get()) == CAIRO_STATUS_SUCCESS;
    }


    double savedPixelDrift(const r62::render::View& view, int factor) const {
        const double ratio = view.transform.scale / savedView_.transform.scale;
        const auto origin = view.transform.toDevice(savedView_.transform.toDocument({0, 0}));
        return std::max({std::abs(origin.x), std::abs(origin.y),
            std::abs(origin.x + (ratio - 1.) * view.widthPx),
            std::abs(origin.y + (ratio - 1.) * view.heightPx)}) * factor;
    }

    // Reuse the last exact content layer from the first wheel event.
    // Selection handles and hover feedback are drawn in the current view.
    bool presentSaved(cairo_t* destination, const r62::render::Scene& scene,
                      const r62::render::Scene* overlay, const r62::render::View& view,
                      r62::render::geom::TextMeasurer* measurer, uint64_t serial,
                      int factor, bool allowZoom) {
        if (!surface_ || savedFactor_ != factor || savedSerial_ != serial ||
            savedGeneration_ != scene.generation() || savedView_.defaultStrokeWidth != view.defaultStrokeWidth ||
            savedView_.defaultTextSize != view.defaultTextSize) return false;
        auto comparable = view;
        comparable.transform.scale = savedView_.transform.scale;
        comparable.transform.center = savedView_.transform.center;
        if (!savedView_.viewportEquals(comparable)) return false;
        double ratio = view.transform.scale / savedView_.transform.scale;
        auto origin = view.transform.toDevice(savedView_.transform.toDocument({0, 0}));
        // Reversing wheel steps can leave floating-point residue in both
        // center and scale. Compare motion at both ends in physical pixels.
        constexpr double epsilon = 1e-6;
        if (savedPixelDrift(view, factor) <= epsilon) {
            ratio = 1.;
            origin = {};
        } else if (!allowZoom) return false;
        const bool uncoversPaper = origin.x > epsilon || origin.y > epsilon ||
            origin.x + ratio * view.widthPx < view.widthPx - epsilon ||
            origin.y + ratio * view.heightPx < view.heightPx - epsilon;
        if (ratio > 2.5 || ratio < .35 || (uncoversPaper && !entireDrawing_ && !overviewReady())) return false;
        cairo_save(destination);
        if (uncoversPaper) {
            cairo_set_source_rgb(destination, 1, 1, 1);
            cairo_paint(destination);
            linux_cairo::draw(destination, paper_, view, measurer);
            if (!entireDrawing_) {
                const auto& coarse = overview_->view;
                const auto at = view.transform.toDevice(coarse.transform.toDocument({0, 0}));
                const auto zoom = view.transform.scale * view.transform.dpiScale /
                    (coarse.transform.scale * coarse.transform.dpiScale);
                cairo_save(destination);
                cairo_translate(destination, at.x, at.y);
                cairo_scale(destination, zoom, zoom);
                cairo_set_source_surface(destination, overview_->pixels.get(), 0, 0);
                cairo_pattern_set_filter(cairo_get_source(destination), CAIRO_FILTER_BILINEAR);
                cairo_rectangle(destination, 0, 0, coarse.widthPx, coarse.heightPx);
                cairo_fill(destination);
                cairo_restore(destination);
            }
            cairo_rectangle(destination, origin.x, origin.y, ratio * view.widthPx, ratio * view.heightPx);
            cairo_clip(destination);
        }
        cairo_translate(destination, origin.x, origin.y);
        cairo_scale(destination, ratio, ratio);
        cairo_set_source_surface(destination, surface_.get(), 0, 0);
        cairo_pattern_set_filter(cairo_get_source(destination), ratio == 1. ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cairo_get_source(destination), CAIRO_EXTEND_PAD);
        cairo_paint(destination);
        cairo_restore(destination);
        if (overlay) linux_cairo::draw(destination, *overlay, view, measurer);
        return true;
    }
    void remember(const r62::render::Scene& scene, const r62::render::View& view, uint64_t serial, int factor) {
        // Outside the old viewport we may synthesize paper only if every
        // drawing primitive was fully covered, including its stroked edges.
        entireDrawing_ = true;
        drawingBounds_ = {{1,1},{-1,-1}};
        paper_.clear();
        for (const auto& primitive : scene.primitives()) {
            if (primitive.layer.value == 0) { paper_.add(primitive); continue; }
            if (!primitive.bounds.empty() && primitive.bounds.min.finite() && primitive.bounds.max.finite()) {
                const double stroke = std::max(view.defaultStrokeWidth, primitive.style.strokeWidth);
                const r62::core::Vec2 pad(stroke, stroke);
                if (drawingBounds_.empty()) drawingBounds_ = primitive.bounds;
                drawingBounds_.add(primitive.bounds.min - pad);
                drawingBounds_.add(primitive.bounds.max + pad);
            }
            if (!entireDrawing_) continue;
            const auto lo = view.transform.toDevice(primitive.bounds.min);
            const auto hi = view.transform.toDevice(primitive.bounds.max);
            const double pad = 2 + std::max(view.defaultStrokeWidth, primitive.style.strokeWidth) *
                view.transform.scale * view.transform.dpiScale;
            entireDrawing_ = lo.x >= pad && lo.y >= pad && hi.x <= view.widthPx - pad && hi.y <= view.heightPx - pad;
        }
        savedView_ = view;
        savedSerial_ = serial;
        savedGeneration_ = scene.generation();
        savedFactor_ = factor;
    }

    static std::unique_ptr<Refinement> makeJob(const r62::render::Scene& scene, const r62::render::View& view,
                                               uint64_t serial, int factor) {
        auto job = std::make_unique<Refinement>();
        job->pixels.reset(cairo_image_surface_create(CAIRO_FORMAT_RGB24, int(view.widthPx) * factor,
                                                    int(view.heightPx) * factor));
        if (cairo_surface_status(job->pixels.get()) != CAIRO_STATUS_SUCCESS) return {};
        cairo_surface_set_device_scale(job->pixels.get(), factor, factor);
        auto* cr = cairo_create(job->pixels.get());
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_destroy(cr);
        job->view = view;
        job->serial = serial;
        job->generation = scene.generation();
        job->factor = factor;
        job->order.resize(scene.size());
        std::iota(job->order.begin(), job->order.end(), size_t{0});
        const auto& primitives = scene.primitives();
        std::stable_sort(job->order.begin(), job->order.end(), [&](size_t i, size_t j) {
            return std::tie(primitives[i].layer.value, primitives[i].z) <
                   std::tie(primitives[j].layer.value, primitives[j].z);
        });
        return job;
    }

    static bool drawPart(Refinement& job, const r62::render::Scene& scene, r62::render::geom::TextMeasurer* measurer) {
        const auto deadline = g_get_monotonic_time() + 4000;
        auto* cr = cairo_create(job.pixels.get());
        do {
            r62::render::Scene part;
            const size_t end = std::min(job.next + 128, job.order.size());
            part.reserve(end - job.next);
            while (job.next < end) part.add(scene.primitives()[job.order[job.next++]]);
            linux_cairo::draw(cr, part, job.view, measurer);
        } while (job.next < job.order.size() && g_get_monotonic_time() < deadline);
        const auto status = cairo_status(cr);
        cairo_destroy(cr);
        return status == CAIRO_STATUS_SUCCESS;
    }
public:
    bool coversDrawing(const r62::render::Scene& scene, const r62::render::View& view, uint64_t serial, int factor) const {
        return entireDrawing_ && savedFactor_ == factor && savedSerial_ == serial &&
            savedGeneration_ == scene.generation() && savedView_.viewportEquals(view);
    }
    bool needsOverview() const {
        return savedFactor_ && !entireDrawing_ && !drawingBounds_.empty() && !overviewReady();
    }

    // Prepare the off-screen region between input events. During a wheel gesture,
    // the native-resolution viewport remains on top of this full drawing.
    bool prepareOverview(const r62::render::Scene& scene, r62::render::geom::TextMeasurer* measurer, uint64_t serial) {
        if (!needsOverview() || savedSerial_ != serial || savedGeneration_ != scene.generation()) return false;
        if (!overview_ || overview_->serial != serial || overview_->generation != savedGeneration_ ||
            overview_->factor != savedFactor_) {
            auto view = savedView_;
            if (view.widthPx < 10 || view.heightPx < 10) return false;
            const auto& bounds = drawingBounds_;
            view.transform.scale = std::min((view.widthPx - 8.) / std::max(1., bounds.width()),
                                            (view.heightPx - 8.) / std::max(1., bounds.height())) / view.transform.dpiScale;
            view.transform.center = bounds.min + (bounds.max - bounds.min) * .5;
            view.transform.viewportCenter = {view.widthPx * .5, view.heightPx * .5};
            overview_ = makeJob(scene, view, serial, savedFactor_);
            if (!overview_) return false;
        }
        if (!drawPart(*overview_, scene, measurer)) { overview_.reset(); return false; }
        return !overviewReady();
    }

    void cancelRefinement() { refinement_.reset(); }

    bool beginRefinement(const r62::render::Scene& scene, const r62::render::View& view, uint64_t serial, int factor) {
        cancelRefinement();
        if (!surface_ || savedSerial_ != serial || savedGeneration_ != scene.generation() ||
            savedFactor_ != factor || !factor || view.clipEnabled || scene.size() < 4000 ||
            view.widthPx != savedView_.widthPx || view.heightPx != savedView_.heightPx ||
            savedPixelDrift(view, factor) <= 1e-6) return false;

        refinement_ = makeJob(scene, view, serial, factor);
        return bool(refinement_);
    }

    // Yield to GTK input between batches and only publish a complete exact frame.
    bool refine(const r62::render::Scene& scene, const r62::render::View& view,
                r62::render::geom::TextMeasurer* measurer, uint64_t serial, int factor) {
        auto& job = *refinement_;
        if (job.serial != serial || job.generation != scene.generation() || job.factor != factor ||
            !job.view.viewportEquals(view) || job.view.defaultStrokeWidth != view.defaultStrokeWidth ||
            job.view.defaultTextSize != view.defaultTextSize) return false;
        if (!drawPart(job, scene, measurer)) return false;
        if (job.next < job.order.size()) return true;
        surface_.swap(job.pixels);
        remember(scene, view, serial, factor);
        return false;
    }

    r62::core::Error paintFrame(cairo_t* destination, const r62::render::Scene& scene,
                                const r62::render::Scene* overlay, const r62::render::View& view,
                                r62::render::geom::TextMeasurer* measurer, ContentCache* content,
                                uint64_t serial, PanCache* pan, int factor, bool dragPreview, bool selectionPreview=false,
                                const std::vector<uint8_t>* moving=nullptr) {
        const int width = int(view.widthPx) * factor, height = int(view.heightPx) * factor;
        const bool stationaryDrag = dragPreview && !selectionPreview && !pan;
        const bool retain = content && !view.clipEnabled &&
            (scene.size() >= 4000 || (stationaryDrag && scene.size() >= 200));
        if (selectionPreview) {
            if (!selectionPreviewCache_) selectionPreviewCache_=std::make_unique<ContentCache>(true);
            content=selectionPreviewCache_.get();
        } else {
            selectionPreviewCache_.reset();
        }
        if (selectionPreview && moving && moving->size()==scene.size() && !moving->empty() && !pan &&
                width>0 && height>0 && double(width)*height<=8e6 && !view.clipEnabled) {
            if (!selectionFrame_) selectionFrame_=std::make_unique<SelectionFrame>();
            const auto& batch=content->prepare(scene,view,measurer,serial);
            if (selectionFrame_->draw(destination,scene,view,batch,*moving,serial,factor)) {
                if (overlay) linux_cairo::draw(destination,*overlay,view,measurer);
                return {};
            }
        }
        selectionFrame_.reset();
        if (retain && (!pan || pan->wheel || (entireDrawing_ && view.transform.scale == savedView_.transform.scale)) &&
            presentSaved(destination, scene, overlay, view, measurer, serial, factor, pan != nullptr)) return {};
        if (retain && pan)
            return paint(destination, scene, overlay, view, measurer, content, serial, pan, factor, dragPreview);
        savedFactor_ = 0;
        if (pan || scene.size() < 200 || width <= 0 || height <= 0 ||
            double(width) * height > 16e6 || (!retain && cairo_surface_get_type(cairo_get_target(destination)) == CAIRO_SURFACE_TYPE_IMAGE)) {
            surface_.reset();
            return paint(destination, scene, overlay, view, measurer, content, serial, pan, factor, dragPreview);
        }
        if (!surface_ || cairo_image_surface_get_width(surface_.get()) != width ||
            cairo_image_surface_get_height(surface_.get()) != height) {
            surface_.reset(cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height));
        }
        if (cairo_surface_status(surface_.get()) != CAIRO_STATUS_SUCCESS) {
            surface_.reset();
            return paint(destination, scene, overlay, view, measurer, content, serial);
        }
        cairo_surface_set_device_scale(surface_.get(), factor, factor);
        auto* frame = cairo_create(surface_.get());
        auto result = paint(frame, scene, retain ? nullptr : overlay, view, measurer, content, serial, nullptr, factor, dragPreview);
        cairo_destroy(frame);
        if (!result.ok()) return result;
        if (retain) {
            remember(scene, view, serial, factor);
        }
        cairo_save(destination);
        cairo_set_source_surface(destination, surface_.get(), 0, 0);
        cairo_pattern_set_filter(cairo_get_source(destination), CAIRO_FILTER_NEAREST);
        cairo_paint(destination);
        cairo_restore(destination);
        if (retain && overlay) linux_cairo::draw(destination, *overlay, view, measurer);
        const auto status = cairo_status(destination);
        return status == CAIRO_STATUS_SUCCESS ? r62::core::Error() :
            r62::core::Error(r62::core::ErrorCode::IoError, cairo_status_to_string(status));
    }
};
inline bool writeClipboardPng(GdkPixbuf* image, int displayScale, GByteArray* bytes) {
    png_structp writer = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!writer) return false;
    png_infop info = png_create_info_struct(writer);
    if (!info) {
        png_destroy_write_struct(&writer, nullptr);
        return false;
    }
    if (setjmp(png_jmpbuf(writer))) {
        png_destroy_write_struct(&writer, &info);
        return false;
    }
    png_set_write_fn(writer, bytes, [](png_structp png, png_bytep data, png_size_t count) {
        g_byte_array_append(static_cast<GByteArray*>(png_get_io_ptr(png)), data, count);
    }, nullptr);
    const int height = gdk_pixbuf_get_height(image);
    png_set_IHDR(writer, info, gdk_pixbuf_get_width(image), height, 8, PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    // GDI+ records the display resolution in the Windows clipboard PNG.
    // Office uses this to convert pixels to the initial size on the page.
    const auto pixelsPerMetre = static_cast<png_uint_32>(96.0 * displayScale / 0.0254);
    png_set_pHYs(writer, info, pixelsPerMetre, pixelsPerMetre, PNG_RESOLUTION_METER);
    png_set_sRGB(writer, info, PNG_sRGB_INTENT_PERCEPTUAL);
    png_set_gAMA_fixed(writer, info, 45455);
    png_write_info(writer, info);
    auto* pixels = gdk_pixbuf_get_pixels(image);
    const int stride = gdk_pixbuf_get_rowstride(image);
    for (int y = 0; y < height; ++y) png_write_row(writer, pixels + y * stride);
    png_write_end(writer, info);
    png_destroy_write_struct(&writer, &info);
    return true;
}

inline r62::core::Error copySelectionPng(r62::integration::Session& session, int displayScale) {
    using r62::core::ErrorCode;
    using r62::core::Vec2;
    if (session.selected.empty()) return {ErrorCode::InvalidArgument, "请先选择要复制的结构"};

    const auto refs = studioio::expanded(session.doc, session.selected, true);
    const auto bounds = studioio::selectionBounds(session);
    r62::render::Scene content;
    for (const auto& primitive : session.scene.primitives())
        if (refs.count(primitive.source)) content.add(primitive);
    if (bounds.empty() || content.empty()) return {ErrorCode::NotFound, "选区没有可绘制的内容"};

    constexpr int margin = 10;
    constexpr int maxEdge = 4096;
    const double width = std::max(1., bounds.width());
    const double height = std::max(1., bounds.height());
    if (!std::isfinite(width) || !std::isfinite(height))
        return {ErrorCode::InvalidArgument, "选区尺寸无效，无法生成 PNG"};
    const double scale = std::min(300. / 72., (maxEdge - 2. * margin) / std::max(width, height));
    const int pixelsWide=std::min(maxEdge, int(std::ceil(width * scale)) + 2 * margin);
    const int pixelsHigh=std::min(maxEdge, int(std::ceil(height * scale)) + 2 * margin);
    auto view = session.renderView;
    view.widthPx = pixelsWide;
    view.heightPx = pixelsHigh;
    view.clipEnabled = false;
    view.transform.center = bounds.min + (bounds.max - bounds.min) * .5;
    view.transform.viewportCenter = Vec2(pixelsWide * .5, pixelsHigh * .5);
    view.transform.scale = scale;
    view.transform.dpiScale = 1.;
    linux_cairo::Surface surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixelsWide, pixelsHigh),
        cairo_surface_destroy);
    std::unique_ptr<cairo_t, decltype(&cairo_destroy)> cr(cairo_create(surface.get()), cairo_destroy);
    linux_cairo::draw(cr.get(), content, view, session.measurer.get());
    if (cairo_status(cr.get()) != CAIRO_STATUS_SUCCESS)
        return {ErrorCode::IoError, cairo_status_to_string(cairo_status(cr.get()))};
    auto* image = gdk_pixbuf_get_from_surface(surface.get(), 0, 0, pixelsWide, pixelsHigh);
    if (!image) return {ErrorCode::IoError, "无法创建 PNG 图像"};
    std::unique_ptr<GByteArray, decltype(&g_byte_array_unref)> png(g_byte_array_new(), g_byte_array_unref);
    const bool encoded = writeClipboardPng(image, displayScale, png.get());
    g_object_unref(image);
    if (!encoded) return {ErrorCode::IoError, "PNG 编码失败"};

    auto result = session.clipboard.open();
    if (!result.ok()) return result;
    result = session.clipboard.publish(r62::selection::kPngClipboardFormat,
        std::string(reinterpret_cast<const char*>(png->data), png->len));
    session.clipboard.close();
    if (result.ok()) session.status = "已复制为 PNG";
    return result;
}
}
