#pragma once

#include "cairo_scene.hpp"
#include "interop.hpp"
#include <cairo-svg.h>

namespace linux_office {
inline r62::core::Error writePreview(const std::string& source, const std::string& target, const std::string& embedded = {}, int frameWidth = 0, int frameHeight = 0) {
    using namespace r62;
    using core::ErrorCode;
    integration::Session session;
    auto result = session.open(source);
    if (!result.ok()) return result;
    session.setSelection(session.all());
    const auto bounds = studioio::selectionBounds(session);
    if (bounds.empty()) return {ErrorCode::NotFound, "文档中没有可以生成预览的绘图对象"};
    const double extent = std::max(bounds.width(), bounds.height());
    if (!std::isfinite(extent) || extent < 0) return {ErrorCode::InvalidArgument, "预览尺寸无效"};
    double scale = extent > 720 ? 720. / extent : 1.;
    constexpr double margin = 4;
    double width = std::max(1., bounds.width() * scale) + 2 * margin;
    double height = std::max(1., bounds.height() * scale) + 2 * margin;

    if (frameWidth || frameHeight) {
        if (frameWidth <= 0 || frameHeight <= 0)
            return {ErrorCode::InvalidArgument, "无效的 Office 对象尺寸"};
        // The host frame is in hundredths of a millimetre. Keep its aspect ratio
        // while bounding preview resolution, and fit all ink with uniform scale.
        const double pointsPerUnit = std::min(72. / 2540., 720. / std::max(frameWidth, frameHeight));
        width = frameWidth * pointsPerUnit;
        height = frameHeight * pointsPerUnit;
        const double inset = std::min(margin, .1 * std::min(width, height));
        scale = std::min((width - 2 * inset) / std::max(1., bounds.width()),
                         (height - 2 * inset) / std::max(1., bounds.height()));
    }

    std::string bytes;
    const auto write = +[](void* opaque, const unsigned char* data, unsigned int size) -> cairo_status_t {
        auto& output = *static_cast<std::string*>(opaque);
        if (size > 64u * 1024 * 1024 - output.size()) return CAIRO_STATUS_WRITE_ERROR;
        try {
            output.append(reinterpret_cast<const char*>(data), size);
            return CAIRO_STATUS_SUCCESS;
        } catch (...) {
            return CAIRO_STATUS_WRITE_ERROR;
        }
    };
    linux_cairo::Surface surface(cairo_svg_surface_create_for_stream(write, &bytes, width, height), cairo_surface_destroy);
    cairo_svg_surface_set_document_unit(surface.get(), CAIRO_SVG_UNIT_PT);
    std::unique_ptr<cairo_t, decltype(&cairo_destroy)> cr(cairo_create(surface.get()), cairo_destroy);
    auto view = session.renderView;
    view.clipEnabled = false;
    view.widthPx = width;
    view.heightPx = height;
    view.transform.center = bounds.min + (bounds.max - bounds.min) * .5;
    view.transform.viewportCenter = {width / 2, height / 2};
    view.transform.scale = scale;
    view.transform.dpiScale = 1;
    linux_cairo::draw(cr.get(), session.scene, view, session.measurer.get());
    auto status = cairo_status(cr.get());
    cr.reset();
    cairo_surface_finish(surface.get());
    if (status == CAIRO_STATUS_SUCCESS) status = cairo_surface_status(surface.get());
    if (status != CAIRO_STATUS_SUCCESS) return {ErrorCode::IoError, cairo_status_to_string(status)};
    persistence::AtomicWriteOptions options;
    options.sourcePath = session.sourcePath;
    result = persistence::writeFileAtomic(target, bytes, options);
    if (!result.ok() || embedded.empty()) return result;
    session.doc.documentExtensions().set("studio.ole.fixed-preview", "1");
    std::ostringstream native;
    result = integration::saveSafeNative(session.doc, native);
    if (!result.ok()) return result;
    return persistence::writeFileAtomic(embedded, native.str(), options);
}
}
