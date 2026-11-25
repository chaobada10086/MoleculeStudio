#pragma once

#include "worker.hpp"
#include "raster_image.hpp"
#include "next/persistence/persistence.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace linux_ocsr {
using linux_i18n::tr;
inline const char* interpreter = "/usr/lib/moleculestudio-ocsr/python/bin/python3.10";
inline const char* scripts = "/usr/share/moleculestudio-ocsr";

inline std::vector<std::string> pickFiles(GtkWindow* owner, bool multiple = false, bool* pasteImage = nullptr) {
    auto* dialog = gtk_file_chooser_dialog_new(tr("选择图片或 PDF"), owner, GTK_FILE_CHOOSER_ACTION_OPEN,
                                               tr("取消"), GTK_RESPONSE_CANCEL, tr("打开"), GTK_RESPONSE_ACCEPT, nullptr);
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, tr("图片或 PDF"));
    for (const char* mime : {"application/pdf","image/png","image/jpeg","image/webp","image/gif","image/bmp","image/tiff"})
        gtk_file_filter_add_mime_type(filter, mime);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_select_multiple(chooser, multiple);
    if (pasteImage) gtk_dialog_add_button(GTK_DIALOG(dialog), tr("粘贴剪贴板"), 1);
    std::vector<std::string> paths;
    const auto choice = gtk_dialog_run(GTK_DIALOG(dialog));
    if (pasteImage) *pasteImage = choice == 1;
    if (choice == GTK_RESPONSE_ACCEPT) {
        GSList* files = gtk_file_chooser_get_filenames(chooser);
        for (auto* file = files; file; file = file->next)
            paths.emplace_back(static_cast<const char*>(file->data));
        g_slist_free_full(files, g_free);
    }
    gtk_widget_destroy(dialog);
    return paths;
}

inline std::string pickFile(GtkWindow* owner, const std::string& cache, std::string& failure) {
    bool paste = false;
    const auto paths = pickFiles(owner, false, &paste);
    if (!paste) return paths.empty() ? std::string() : paths.front();
    std::string bytes;
    const char* type = nullptr;
    const auto error = linux_image::clipboardBytes(bytes, type);
    if (!error.ok()) { failure = error.message; return {}; }
    if (!type || !linux_image::decode(bytes, type)) { failure = "剪贴板中没有可读取的图片"; return {}; }
    const std::unique_ptr<gchar, decltype(&g_free)> digest(g_compute_checksum_for_data(
        G_CHECKSUM_SHA256, reinterpret_cast<const guchar*>(bytes.data()), bytes.size()), g_free);
    if (g_mkdir_with_parents((cache + "/clipboard").c_str(), 0700) != 0) {
        failure = "无法创建图像识别缓存";
        return {};
    }
    const auto file = cache + "/clipboard/" + digest.get() + "." + type;
    const auto written = r62::persistence::writeFileAtomic(file, bytes);
    if (!written.ok()) { failure = written.message; return {}; }
    return file;
}

inline int pickPage(GtkWindow* owner) {
    auto* dialog = gtk_dialog_new_with_buttons(tr("PDF 页码"), owner, GTK_DIALOG_MODAL,
                                               tr("取消"), GTK_RESPONSE_CANCEL, tr("打开"), GTK_RESPONSE_OK, nullptr);
    auto* input = gtk_spin_button_new_with_range(1, 600, 1);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), input);
    gtk_widget_show_all(dialog);
    const int page = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK ?
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(input)) : 0;
    gtk_widget_destroy(dialog);
    return page;
}

struct Crop {
    int x = 0, y = 0, width = 0, height = 0;
};

struct Source {
    std::string path;
    std::vector<std::string> arguments;
    int page = 1;
    int width = 0, height = 0;
    bool pdf = false;
};

inline const tinyxml2::XMLElement* response(tinyxml2::XMLDocument&, const std::string&, std::string&);

inline const tinyxml2::XMLElement* exchange(GtkWindow* owner, const Source& source, const char* message,
        const tinyxml2::XMLDocument& request, tinyxml2::XMLDocument& result, std::string& failure, guint timeout = 120) {
    tinyxml2::XMLPrinter body(nullptr, true);
    request.Print(&body);
    return response(result, linux_worker::run(owner, message, source.arguments, std::string(body.CStr()) + "\n",
        failure, timeout, true), failure);
}

inline void statusDialog(GtkWindow* owner, const std::string& message, GtkMessageType type) {
    auto* dialog = gtk_message_dialog_new(owner, GTK_DIALOG_MODAL, type, GTK_BUTTONS_CLOSE,
        "%s", linux_i18n::message(message).c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

struct Selection {
    GdkPixbuf* preview;
    double x0, y0, x1, y1;
    bool dragging = false;
    std::vector<Crop> candidates;
    void end(double x, double y) {
        x1 = std::clamp(x, 0., double(gdk_pixbuf_get_width(preview)));
        y1 = std::clamp(y, 0., double(gdk_pixbuf_get_height(preview)));
    }

    Crop pixels(int width, int height) const {
        const int imageWidth = gdk_pixbuf_get_width(preview), imageHeight = gdk_pixbuf_get_height(preview);
        const int left = int(std::floor(std::min(x0, x1) * width / imageWidth));
        const int top = int(std::floor(std::min(y0, y1) * height / imageHeight));
        const int right = int(std::ceil(std::max(x0, x1) * width / imageWidth));
        const int bottom = int(std::ceil(std::max(y0, y1) * height / imageHeight));
        return {left, top, right - left, bottom - top};
    }

    void activateCandidate() {
        if (std::abs(x1 - x0) > 3 || std::abs(y1 - y0) > 3) return;
        const Crop* match = nullptr;
        for (const auto& box : candidates) {
            if (x1 < box.x || y1 < box.y || x1 > box.x + box.width || y1 > box.y + box.height) continue;
            if (!match || box.width * box.height < match->width * match->height) match = &box;
        }
        if (!match) return;
        x0 = match->x; y0 = match->y;
        x1 = match->x + match->width; y1 = match->y + match->height;
    }
};

inline void setCrop(tinyxml2::XMLElement& request, const Crop& crop) {
    request.SetAttribute("crop_x", crop.x);
    request.SetAttribute("crop_y", crop.y);
    request.SetAttribute("crop_w", crop.width);
    request.SetAttribute("crop_h", crop.height);
}

inline std::vector<Crop> detect(GtkWindow* owner, const Source& source, int previewWidth, int previewHeight,
                                std::string& failure) {
    tinyxml2::XMLDocument request, result;
    auto* node = request.NewElement("request");
    request.InsertEndChild(node);
    node->SetAttribute("op", "detect");
    node->SetAttribute("file", source.path.c_str());
    node->SetAttribute("page", source.page);
    node->SetAttribute("dpi", 300);
    const auto* found = exchange(owner, source, "正在检测页面上的候选结构…", request, result, failure);
    std::vector<Crop> boxes;
    if (!found) return boxes;
    for (auto* element = found->FirstChildElement("region"); element; element = element->NextSiblingElement("region")) {
        const auto* pixels = element->Attribute("pixels");
        if (!pixels) continue;
        std::string text = pixels;
        std::replace(text.begin(), text.end(), ',', ' ');
        const double sx = double(previewWidth) / source.width, sy = double(previewHeight) / source.height;
        std::istringstream input(text);
        int x, y, width, height;
        if (!(input >> x >> y >> width >> height) || x < 0 || y < 0 || width <= 0 || height <= 0 ||
            width > source.width - x || height > source.height - y) continue;
        boxes.push_back({int(x * sx), int(y * sy), std::max(1, int(std::ceil(width * sx))),
            std::max(1, int(std::ceil(height * sy)))});
    }
    return boxes;
}

inline void exportCrop(GtkWindow* owner, const Source& source, const Crop& crop) {
    if (crop.width <= 0 || crop.height <= 0) {
        statusDialog(owner, "请先选择要导出的区域", GTK_MESSAGE_INFO);
        return;
    }
    auto* dialog = gtk_file_chooser_dialog_new(tr("导出裁剪…"), owner, GTK_FILE_CHOOSER_ACTION_SAVE,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("保存"), GTK_RESPONSE_ACCEPT, nullptr);
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_current_name(chooser, source.pdf ? "crop.pdf" : "crop.png");
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    std::string path;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const std::unique_ptr<gchar, decltype(&g_free)> file(gtk_file_chooser_get_filename(chooser), g_free);
        if (file) path = file.get();
    }
    gtk_widget_destroy(dialog);
    if (path.empty()) return;
    const std::unique_ptr<gchar, decltype(&g_free)> name(g_ascii_strdown(path.c_str(), -1), g_free);
    const bool pdf = source.pdf && g_str_has_suffix(name.get(), ".pdf");
    if (!pdf && !g_str_has_suffix(name.get(), ".png")) {
        statusDialog(owner, source.pdf ? "请使用 .png 或 .pdf 扩展名" : "请使用 .png 扩展名", GTK_MESSAGE_INFO);
        return;
    }

    std::string failure;
    tinyxml2::XMLDocument request, result;
    auto* node = request.NewElement("request");
    request.InsertEndChild(node);
    node->SetAttribute("op", "export_crop");
    node->SetAttribute("file", source.path.c_str());
    node->SetAttribute("page", source.page);
    node->SetAttribute("dpi", 300);
    node->SetAttribute("target_dpi", 600);
    node->SetAttribute("formats", pdf ? "pdf" : "png");
    setCrop(*node, crop);
    const auto* exported = exchange(owner, source, "正在导出裁剪…", request, result, failure);
    if (exported) {
        const auto* file = exported->Attribute(pdf ? "pdf" : "image");
        gchar* raw = nullptr;
        gsize size = 0;
        if (!file || !g_file_get_contents(file, &raw, &size, nullptr))
            failure = "无法读取导出的裁剪文件";
        else {
            const std::unique_ptr<gchar, decltype(&g_free)> bytes(raw, g_free);
            r62::persistence::AtomicWriteOptions options;
            options.sourcePath = source.path;
            const auto saved = r62::persistence::writeFileAtomic(path, std::string(bytes.get(), size), options);
            if (!saved.ok()) failure = saved.message;
        }
    }
    if (!failure.empty()) statusDialog(owner, failure, GTK_MESSAGE_ERROR);
    else if (exported) statusDialog(owner, "已导出裁剪", GTK_MESSAGE_INFO);
}

inline bool pickRegion(GtkWindow* owner, GdkPixbuf* preview, const Source& source, tinyxml2::XMLElement& request) {
    const int imageWidth = gdk_pixbuf_get_width(preview), imageHeight = gdk_pixbuf_get_height(preview);
    Selection selection{preview, 0, 0, double(imageWidth), double(imageHeight)};
    auto* dialog = gtk_dialog_new_with_buttons(tr("框选要识别的分子"), owner, GTK_DIALOG_MODAL,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("自动检测"), 1, tr("导出裁剪…"), 2, tr("识别"), GTK_RESPONSE_OK, nullptr);
    auto* canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(canvas, imageWidth, imageHeight);
    gtk_widget_set_halign(canvas, GTK_ALIGN_CENTER);
    gtk_widget_add_events(canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(canvas, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, Selection* s) -> gboolean {
        gdk_cairo_set_source_pixbuf(cr, s->preview, 0, 0);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, .14, .58, .32);
        cairo_set_line_width(cr, 1.5);
        for (const auto& box : s->candidates) cairo_rectangle(cr, box.x, box.y, box.width, box.height);
        cairo_stroke(cr);
        cairo_rectangle(cr, std::min(s->x0, s->x1), std::min(s->y0, s->y1),
                        std::abs(s->x1-s->x0), std::abs(s->y1-s->y0));
        cairo_set_source_rgba(cr, .1, .48, .78, .12);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, .1, .4, .68);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
        return TRUE;
    }), &selection);
    g_signal_connect(canvas, "button-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, Selection* s) -> gboolean {
        if (e->button != 1) return FALSE;
        s->end(e->x, e->y);
        s->x0 = s->x1;
        s->y0 = s->y1;
        s->dragging = true;
        gtk_widget_queue_draw(w);
        return TRUE;
    }), &selection);
    g_signal_connect(canvas, "motion-notify-event", G_CALLBACK(+[](GtkWidget* w, GdkEventMotion* e, Selection* s) -> gboolean {
        if (!s->dragging) return FALSE;
        s->end(e->x, e->y);
        gtk_widget_queue_draw(w);
        return TRUE;
    }), &selection);
    g_signal_connect(canvas, "button-release-event", G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, Selection* s) -> gboolean {
        if (e->button != 1 || !s->dragging) return FALSE;
        s->dragging = false;
        s->end(e->x, e->y);
        s->activateCandidate();
        gtk_widget_queue_draw(w);
        return TRUE;
    }), &selection);
    auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    auto* help = gtk_label_new("");
    gtk_box_pack_start(content, canvas, FALSE, FALSE, 0);
    gtk_box_pack_start(content, help, FALSE, FALSE, 4);
    gtk_widget_show_all(dialog);
    int choice;
    while ((choice = gtk_dialog_run(GTK_DIALOG(dialog))) == 1 || choice == 2) {
        if (choice == 1) {
            std::string error;
            selection.candidates = detect(GTK_WINDOW(dialog), source, imageWidth, imageHeight, error);
            gtk_widget_queue_draw(canvas);
            gtk_label_set_text(GTK_LABEL(help), error.empty() ?
                tr(selection.candidates.empty() ? "未检测到候选，请手动框选" : "点击绿色候选框，或手动框选") :
                linux_i18n::message(error).c_str());
        } else {
            exportCrop(GTK_WINDOW(dialog), source, selection.pixels(source.width, source.height));
        }
    }
    gtk_widget_destroy(dialog);
    if (choice != GTK_RESPONSE_OK) return false;
    setCrop(request, selection.pixels(source.width, source.height));
    return true;
}

inline const tinyxml2::XMLElement* response(tinyxml2::XMLDocument& document, const std::string& value, std::string& failure) {
    if (value.empty()) return nullptr;
    if (document.Parse(value.c_str()) != tinyxml2::XML_SUCCESS || !document.RootElement()) {
        failure = "无法读取图片识别服务的响应";
        return nullptr;
    }
    const auto* node = document.RootElement();
    if (!node->Attribute("ok", "1")) {
        const auto* message = node->Attribute("error");
        failure = message ? message : "图片识别失败";
        return nullptr;
    }
    return node;
}

enum class ReviewAction { Insert, Copy, Edit };

struct Recognition {
    std::string path, record;
    std::vector<std::string> arguments;
    ReviewAction action = ReviewAction::Insert;
};

inline void confirm(GtkWindow* owner, const Recognition& recognized, std::string& failure) {
    if (recognized.record.empty()) return;
    Source service;
    service.arguments = recognized.arguments;
    tinyxml2::XMLDocument request, result;
    auto* node = request.NewElement("request");
    request.InsertEndChild(node);
    node->SetAttribute("op", "confirm");
    node->SetAttribute("record", recognized.record.c_str());
    exchange(owner, service, "正在保存识别记录…", request, result, failure, 25);
}

inline Recognition import(GtkWindow* owner, std::string& failure) {
    if (!g_file_test(interpreter, G_FILE_TEST_IS_EXECUTABLE)) {
        failure = "请安装 moleculestudio-ocsr 图片转结构运行时";
        return {};
    }
    const std::string cache = std::string(g_get_user_cache_dir()) + "/moleculestudio-native/ocsr";
    const std::string records = std::string(g_get_user_data_dir()) + "/moleculestudio-native/recognition_records";
    Source source;
    source.path = pickFile(owner, cache, failure);
    if (source.path.empty()) return {};
    gchar* lowered = g_ascii_strdown(source.path.c_str(), -1);
    source.pdf = g_str_has_suffix(lowered, ".pdf");
    g_free(lowered);
    source.page = source.pdf ? pickPage(owner) : 1;
    if (!source.page) return {};
    source.arguments = {interpreter, std::string(scripts) + "/ocsr_service.py",
        "--models", std::string(scripts) + "/models", "--cache", cache, "--records", records, "--device", "cpu"};
    auto render = source.arguments;
    render.insert(render.end(), {"--render", "--file", source.path, "--page", std::to_string(source.page), "--dpi", "300"});
    tinyxml2::XMLDocument renderInfo;
    const auto* shown = response(renderInfo, linux_worker::run(owner, "正在准备预览…", render, "", failure, 60, true), failure);
    if (!shown) return {};
    const char* previewFile = shown->Attribute("image");
    source.width = shown->IntAttribute("width");
    source.height = shown->IntAttribute("height");
    if (!previewFile || source.width <= 0 || source.height <= 0) {
        failure = "图片预览缺少有效尺寸";
        return {};
    }
    auto* pixbuf = gdk_pixbuf_new_from_file_at_scale(previewFile, 760, 540, TRUE, nullptr);
    if (!pixbuf) { failure = "预览图片无法打开"; return {}; }
    tinyxml2::XMLDocument xml;
    auto* request = xml.NewElement("request");
    xml.InsertEndChild(request);
    request->SetAttribute("op", "recognize");
    request->SetAttribute("id", "1");
    request->SetAttribute("page_image", previewFile);
    request->SetAttribute("source_kind", source.pdf ? "pdf" : "image");
    request->SetAttribute("source_path", source.path.c_str());
    request->SetAttribute("page", source.page);
    request->SetAttribute("dpi", 300);
    request->SetAttribute("vector", source.pdf ? 1 : 0);
    const bool accepted = pickRegion(owner, pixbuf, source, *request);
    g_object_unref(pixbuf);
    if (!accepted) return {};
    tinyxml2::XMLDocument result;
    const auto* candidate = exchange(owner, source, "正在识别化学结构…", xml, result, failure, 180);
    if (!candidate) return {};
    const auto* cdxml = candidate->Attribute("cdxml");
    const bool insertable = cdxml && candidate->Attribute("insertable", "1");
    auto* dialog = gtk_dialog_new_with_buttons(tr("核对识别结果"), owner, GTK_DIALOG_MODAL,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("复制结构"), 1, tr("编辑预览"), 2,
        tr("插入结构"), GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, insertable);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), 1, insertable);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), 2, insertable);
    auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    auto* images = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(content, GTK_WIDGET(images), TRUE, TRUE, 4);
    for (const auto& image : {std::make_pair("overlay", "叠加"), std::make_pair("crop", "原图")}) {
        const auto* file = candidate->Attribute(image.first);
        auto* review = file ? gdk_pixbuf_new_from_file_at_scale(file, 690, 440, TRUE, nullptr) : nullptr;
        if (review) {
            gtk_notebook_append_page(images, gtk_image_new_from_pixbuf(review), gtk_label_new(tr(image.second)));
            g_object_unref(review);
        }
    }
    if (!gtk_notebook_get_n_pages(images)) gtk_widget_destroy(GTK_WIDGET(images));
    std::string note = std::string(tr("原子")) + "  " + std::to_string(candidate->IntAttribute("atoms")) + "   " +
        tr("化学键") + "  " + std::to_string(candidate->IntAttribute("bonds"));
    if (!insertable) note += std::string("\n") + tr("候选结构未通过插入校验");
    for (auto* warning = candidate->FirstChildElement("warning"); warning; warning = warning->NextSiblingElement("warning"))
        if (warning->GetText()) note += "\n" + linux_i18n::message(warning->GetText());
    auto* label = gtk_label_new(note.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 69);
    gtk_box_pack_start(content, label, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    const auto choice = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (!insertable || (choice != GTK_RESPONSE_OK && choice != 1 && choice != 2)) return {};
    const char* record = candidate->Attribute("record");
    const auto action = choice == 2 ? ReviewAction::Edit : choice == 1 ? ReviewAction::Copy : ReviewAction::Insert;
    return {cdxml, record ? record : "", std::move(source.arguments), action};
}
}
