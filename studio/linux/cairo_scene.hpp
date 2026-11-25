#pragma once

#include "raster_image.hpp"
#include "vector_metafile.hpp"
#include "next/render/batcher.hpp"
#include <algorithm>
#include <deque>
#include <tuple>

namespace linux_cairo {
using namespace r62;
using Surface = std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)>;

inline bool hasImages(const render::Scene& scene) {
    return std::any_of(scene.primitives().begin(), scene.primitives().end(), linux_image::isImage);
}

struct EmbeddedImage {
    Surface pixels{nullptr, cairo_surface_destroy};
    linux_image::SvgImage vector;
    linux_image::ImageFrame frame;
};

// Office preview export also runs without an initialized GDK display.
inline Surface imageSurface(GdkPixbuf* pixels) {
    const int width = gdk_pixbuf_get_width(pixels);
    const int height = gdk_pixbuf_get_height(pixels);
    Surface surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height), cairo_surface_destroy);
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS) return surface;
    const int channels = gdk_pixbuf_get_n_channels(pixels);
    const auto* source = gdk_pixbuf_read_pixels(pixels);
    auto* data = cairo_image_surface_get_data(surface.get());
    for (int y = 0; y < height; ++y) {
        const auto* pixel = source + size_t(y) * gdk_pixbuf_get_rowstride(pixels);
        auto* row = reinterpret_cast<uint32_t*>(data + size_t(y) * cairo_image_surface_get_stride(surface.get()));
        for (int x = 0; x < width; ++x, pixel += channels) {
            const uint32_t alpha = channels == 4 ? pixel[3] : 255;
            auto premultiply = [alpha](uint32_t c) { return (c * alpha + 127) / 255; };
            row[x] = (alpha << 24) | (premultiply(pixel[0]) << 16) | (premultiply(pixel[1]) << 8) | premultiply(pixel[2]);
        }
    }
    cairo_surface_mark_dirty(surface.get());
    return surface;
}

inline const EmbeddedImage& cachedImage(const render::Primitive& primitive) {
    struct Entry {
        std::string key;
        EmbeddedImage image;
        size_t bytes;
    };
    static std::deque<Entry> cache;
    static size_t bytes = 0;
    for (const auto& entry : cache)
        if (primitive.text == entry.key) return entry.image;

    EmbeddedImage image;
    linux_image::Metafile file;
    auto pixels = linux_image::load(primitive, &file, &image.frame);
    if (pixels) image.pixels = imageSurface(pixels.get());
    if (image.pixels && cairo_surface_status(image.pixels.get()) != CAIRO_STATUS_SUCCESS) image.pixels.reset();
    if (!image.pixels) image.vector = linux_image::vectorImage(file);
    const size_t cost = primitive.text.size() + image.vector.bytes +
        (image.pixels ? size_t(cairo_image_surface_get_stride(image.pixels.get())) *
            cairo_image_surface_get_height(image.pixels.get()) : 0);
    constexpr size_t cacheLimit = 96u * 1024 * 1024;
    while (!cache.empty() && (cache.size() >= 16 || bytes > cacheLimit || cost > cacheLimit - bytes)) {
        bytes -= cache.front().bytes;
        cache.pop_front();
    }
    // Keep a single oversized legacy image only until the next lookup.
    bytes += cost;
    cache.push_back({primitive.text, std::move(image), cost});
    return cache.back().image;
}

inline void drawImage(cairo_t* cr, const render::Primitive& primitive, const render::View& view) {
    const auto origin = view.transform.toDevice(primitive.points[0]);
    const auto u = view.transform.toDevice(primitive.points[2]) - origin;
    const auto v = view.transform.toDevice(primitive.points[3]) - origin;
    const double area = u.x * v.y - u.y * v.x;
    if (!std::isfinite(area) || std::abs(area) < 1e-8) return;
    const auto far = origin + u + v;
    double left, top, right, bottom;
    cairo_clip_extents(cr, &left, &top, &right, &bottom);
    if (std::max({origin.x, far.x, origin.x + u.x, origin.x + v.x}) < left ||
        std::min({origin.x, far.x, origin.x + u.x, origin.x + v.x}) > right ||
        std::max({origin.y, far.y, origin.y + u.y, origin.y + v.y}) < top ||
        std::min({origin.y, far.y, origin.y + u.y, origin.y + v.y}) > bottom) return;
    const auto& image = cachedImage(primitive);
    if (!image.pixels && !image.vector.handle) return;
    const double width = image.pixels ? cairo_image_surface_get_width(image.pixels.get()) : image.vector.width;
    const double height = image.pixels ? cairo_image_surface_get_height(image.pixels.get()) : image.vector.height;
    const bool framed = image.pixels && image.frame.width > 0;
    const double frameWidth = framed ? image.frame.width : width;
    const double frameHeight = framed ? image.frame.height : height;
    cairo_matrix_t transform;
    cairo_matrix_init(&transform, u.x / frameWidth, u.y / frameWidth, v.x / frameHeight, v.y / frameHeight, origin.x, origin.y);
    cairo_save(cr);
    cairo_transform(cr, &transform);
    if (image.pixels) {
        if (framed) {
            if (image.frame.x > 0 || image.frame.y > 0 ||
                width > image.frame.x + frameWidth || height > image.frame.y + frameHeight) {
                cairo_rectangle(cr, 0, 0, frameWidth, frameHeight);
                cairo_clip(cr);
            }
            cairo_translate(cr, -image.frame.x, -image.frame.y);
        }
        cairo_set_source_surface(cr, image.pixels.get(), 0, 0);
        if (cairo_surface_get_type(cairo_get_target(cr)) == CAIRO_SURFACE_TYPE_SVG) {
            // A finite image source maps directly to SVG <image> in drawing order.
            cairo_paint(cr);
        } else {
            cairo_rectangle(cr, 0, 0, width, height);
            cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
            cairo_fill(cr);
        }
    } else {
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_clip(cr);
        const RsvgRectangle viewport{0, 0, width, height};
        rsvg_handle_render_document(image.vector.handle.get(), cr, &viewport, nullptr);
    }
    cairo_restore(cr);
}

inline void drawRange(cairo_t* cr, const render::DrawRange& range, const render::Primitive& primitive,
                      const render::Batch& batch, const render::View& view, bool dragPreview) {
    cairo_new_path(cr);
    if (range.count == 0) {
        cairo_set_fill_rule(cr, primitive.style.fillRule == render::FillRule::EvenOdd
            ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
        for (const auto& part : primitive.subpaths) {
            if (part.points.size() < 3) continue;
            const auto first = view.transform.toDevice(part.points.front());
            cairo_move_to(cr, first.x, first.y);
            for (size_t i = 1; i < part.points.size(); ++i) {
                const auto point = view.transform.toDevice(part.points[i]);
                cairo_line_to(cr, point.x, point.y);
            }
            cairo_close_path(cr);
        }
    } else {
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
        const auto* vertices = batch.triangleVertices.data() + size_t(range.first) * 2;
        const auto winding = [](const float* v) {
            return (double(v[2]) - v[0]) * (double(v[5]) - v[1]) -
                   (double(v[4]) - v[0]) * (double(v[3]) - v[1]);
        };
        // Consecutive triangles in a cap or stroke quad often form a fan.
        // Its interior edges cancel in a winding fill. Emit only its boundary.
        for (int i = 0; i + 2 < range.count;) {
            const float* first = vertices + i * 2;
            const double area = winding(first);
            i += 3;
            if (area == 0.) continue;
            const float* last = first;
            while (dragPreview && i + 2 < range.count) {
                const float* next = vertices + i * 2;
                if (next[0] != first[0] || next[1] != first[1] ||
                    next[2] != last[4] || next[3] != last[5]) break;
                const double nextArea = winding(next);
                if (nextArea == 0. || (nextArea > 0.) != (area > 0.)) break;
                last = next;
                i += 3;
            }
            cairo_move_to(cr, first[0], first[1]);
            if (area > 0.) {
                cairo_line_to(cr, first[2], first[3]);
                for (const float* tri = first; tri <= last; tri += 6)
                    cairo_line_to(cr, tri[4], tri[5]);
            } else {
                cairo_line_to(cr, last[4], last[5]);
                for (const float* tri = last;; tri -= 6) {
                    cairo_line_to(cr, tri[2], tri[3]);
                    if (tri == first) break;
                }
            }
            cairo_close_path(cr);
        }
    }
    const auto color = render::resolvePaint(range.rgba);
    cairo_set_source_rgba(cr, render::colorRed(color) / 255., render::colorGreen(color) / 255.,
        render::colorBlue(color) / 255., render::colorAlpha(color) / 255.);
    cairo_fill(cr);
}

inline void draw(cairo_t* cr, const render::Scene& scene, const render::View& view, const render::Batch& batch,
                 bool dragPreview=false, const std::vector<uint8_t>* visible=nullptr) {
    struct Draw {
        size_t index;
        const render::DrawRange* range;
    };
    const auto& primitives = scene.primitives();
    std::vector<Draw> ordered;
    ordered.reserve(batch.triangleRanges.size());
    for (const auto& range : batch.triangleRanges)
        if (!visible || (*visible)[range.primitiveIndex]) ordered.push_back({range.primitiveIndex, &range});
    for (size_t i = 0; i < primitives.size(); ++i)
        if ((!visible || (*visible)[i]) && linux_image::isImage(primitives[i])) ordered.push_back({i, nullptr});
    const auto order = [&](const Draw& item) {
        const auto& primitive = primitives[item.index];
        return std::make_tuple(primitive.layer.value, primitive.z, item.index);
    };
    const auto earlier = [&](const Draw& a, const Draw& b) { return order(a) < order(b); };
    if (!std::is_sorted(ordered.begin(), ordered.end(), earlier))
        std::stable_sort(ordered.begin(), ordered.end(), earlier);
    for (const auto& item : ordered) {
        const auto& primitive = primitives[item.index];
        if (item.range) drawRange(cr, *item.range, primitive, batch, view, dragPreview);
        else drawImage(cr, primitive, view);
    }
}

inline void draw(cairo_t* cr, const render::Scene& scene, const render::View& view,
                 render::geom::TextMeasurer* measurer, bool dragPreview=false) {
    render::Batcher batcher;
    render::Batch batch;
    batcher.sync(scene, view, measurer, nullptr, true);
    batcher.buildBatch(scene, view, true, &batch, nullptr, true);
    draw(cr, scene, view, batch, dragPreview);
}
}
