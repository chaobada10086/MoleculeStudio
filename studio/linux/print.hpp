#pragma once

#include "interop.hpp"
#include "i18n.hpp"
#include "cairo_scene.hpp"
#include <cairo-pdf.h>
#include <gtk/gtk.h>
#include <algorithm>
#include <memory>

namespace linux_print {
using namespace r62;
using core::Error;
using core::ErrorCode;
using core::Vec2;

class Pages {
    static constexpr double resolution = 8.;
    studioio::Page page_;
    render::Scene scene_;
    render::geom::TextMeasurer* glyphs_;
    render::View view_;
    render::Batcher batcher_;
    render::Batch batch_;
    bool prepared_ = false;

public:
    explicit Pages(const integration::Session& session, bool fitDrawing = false)
        : page_(studioio::Page::get(session.doc)), scene_(session.scene), glyphs_(session.measurer.get()) {
        view_.defaultStrokeWidth = session.doc.defaultLineWidth();
        view_.defaultTextSize = session.doc.defaultTextSize();
        if (fitDrawing && page_.valid()) {
            page_.across = page_.down = 1;
            const auto bounds = session.scene.bounds();
            if (!bounds.empty() && bounds.min.finite() && bounds.max.finite()) {
                constexpr double margin = 12.;
                const double scale = std::min({1., (page_.width - 2 * margin) / std::max(1., bounds.width()),
                    (page_.height - 2 * margin) / std::max(1., bounds.height())});
                view_.transform.scale = scale;
                page_.x = bounds.min.x + (bounds.width() - page_.width / scale) * .5;
                page_.y = bounds.min.y + (bounds.height() - page_.height / scale) * .5;
            }
        }
        view_.transform.dpiScale = resolution;
        view_.widthPx = page_.width * resolution;
        view_.heightPx = page_.height * resolution;
    }

    bool valid() const { return page_.valid() && std::isfinite(page_.x) && std::isfinite(page_.y); }
    int count() const { return page_.across * page_.down; }
    double width() const { return page_.width; }
    double height() const { return page_.height; }

    void draw(cairo_t* cr, int index, double printableWidth, double printableHeight) {
        const double scale = std::min({1., printableWidth / width(), printableHeight / height()});
        if (scale <= 0. || index < 0 || index >= count()) return;
        if (!prepared_) {
            batcher_.sync(scene_, view_, glyphs_, nullptr, true);
            prepared_ = true;
        }
        const int column = index % page_.across, row = index / page_.across;
        view_.transform.center = Vec2(page_.x + column * width(), page_.y + row * height());
        batcher_.buildBatch(scene_, view_, true, &batch_, nullptr, true);
        cairo_save(cr);
        cairo_translate(cr, (printableWidth - width() * scale) / 2, (printableHeight - height() * scale) / 2);
        cairo_scale(cr, scale, scale);
        cairo_rectangle(cr, 0, 0, width(), height());
        cairo_clip(cr);
        cairo_scale(cr, 1. / resolution, 1. / resolution);
        linux_cairo::draw(cr, scene_, view_, batch_);
        cairo_restore(cr);
    }
};

inline Error exportPdf(const integration::Session& session, const std::string& path, bool fitDrawing = false) {
    Pages pages(session, fitDrawing);
    if (!pages.valid()) return {ErrorCode::InvalidArgument, "纸张设置无效"};
    std::string bytes;
    auto* surface = cairo_pdf_surface_create_for_stream(+[](void* closure, const unsigned char* data,
                                                           unsigned int length)->cairo_status_t {
        try {
            static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
            return CAIRO_STATUS_SUCCESS;
        } catch (...) {
            return CAIRO_STATUS_WRITE_ERROR;
        }
    }, &bytes, pages.width(), pages.height());
    std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> pdf(surface, cairo_surface_destroy);
    std::unique_ptr<cairo_t, decltype(&cairo_destroy)> cr(cairo_create(pdf.get()), cairo_destroy);
    for (int index = 0; index < pages.count() && cairo_status(cr.get()) == CAIRO_STATUS_SUCCESS; ++index) {
        pages.draw(cr.get(), index, pages.width(), pages.height());
        cairo_show_page(cr.get());
    }
    auto status = cairo_status(cr.get());
    cr.reset();
    cairo_surface_finish(pdf.get());
    if (status == CAIRO_STATUS_SUCCESS) status = cairo_surface_status(pdf.get());
    if (status != CAIRO_STATUS_SUCCESS) return {ErrorCode::IoError, cairo_status_to_string(status)};
    persistence::AtomicWriteOptions options;
    options.sourcePath = session.sourcePath;
    return persistence::writeFileAtomic(path, bytes, options);
}

struct PrintJob {
    Pages pages;
    Error failure;

    explicit PrintJob(const integration::Session& session) : pages(session) {}

    GtkPrintOperation* create(const std::string& title) {
        auto* operation = gtk_print_operation_new();
        gtk_print_operation_set_unit(operation, GTK_UNIT_POINTS);
        gtk_print_operation_set_allow_async(operation, FALSE);
        gtk_print_operation_set_show_progress(operation, TRUE);
        gtk_print_operation_set_job_name(operation, title.c_str());
        auto* setup = gtk_page_setup_new();
        const auto width = pages.width(), height = pages.height();
        auto* paper = gtk_paper_size_new_custom("moleculestudio", linux_i18n::tr("文档纸张"),
            std::min(width, height), std::max(width, height), GTK_UNIT_POINTS);
        gtk_page_setup_set_paper_size_and_default_margins(setup, paper);
        gtk_paper_size_free(paper);
        gtk_page_setup_set_orientation(setup, width > height ? GTK_PAGE_ORIENTATION_LANDSCAPE : GTK_PAGE_ORIENTATION_PORTRAIT);
        gtk_print_operation_set_default_page_setup(operation, setup);
        gtk_print_operation_set_embed_page_setup(operation, TRUE);
        g_object_unref(setup);
        g_signal_connect(operation, "begin-print",
            G_CALLBACK(+[](GtkPrintOperation* op, GtkPrintContext*, PrintJob* job) {
                gtk_print_operation_set_n_pages(op, job->pages.count());
            }), this);
        g_signal_connect(operation, "draw-page",
            G_CALLBACK(+[](GtkPrintOperation* op, GtkPrintContext* context, gint index, PrintJob* job) {
                auto* cr = gtk_print_context_get_cairo_context(context);
                try {
                    job->pages.draw(cr, index, gtk_print_context_get_width(context), gtk_print_context_get_height(context));
                    if (cairo_status(cr) == CAIRO_STATUS_SUCCESS) return;
                    job->failure = Error(ErrorCode::IoError, cairo_status_to_string(cairo_status(cr)));
                } catch (const std::exception& e) {
                    job->failure = Error(ErrorCode::IoError, e.what());
                }
                gtk_print_operation_cancel(op);
            }), this);
        return operation;
    }
};

inline Error print(GtkWindow* owner, const integration::Session& session, const std::string& title, bool& submitted) {
    PrintJob job(session);
    if (!job.pages.valid()) return {ErrorCode::InvalidArgument, "纸张设置无效"};
    auto* operation = job.create(title);
    GError* error = nullptr;
    const auto result = gtk_print_operation_run(operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, owner, &error);
    g_object_unref(operation);
    submitted = result == GTK_PRINT_OPERATION_RESULT_APPLY;
    if (error) {
        job.failure = {ErrorCode::IoError, error->message};
        g_error_free(error);
    }
    return job.failure;
}
}
