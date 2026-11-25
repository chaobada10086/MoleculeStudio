#pragma once

#include "config.hpp"
#include "i18n.hpp"
#include "interop.hpp"
#include <gtk/gtk.h>
#include <algorithm>

namespace linux_page {
using studioio::Page;
using r62::core::Vec2;
using pagedefaults::millimeters;
using pagedefaults::points;
using pagedefaults::papers;
using linux_i18n::tr;

inline std::string number(double value) {
    char text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(text, sizeof(text), "%.12g", value);
    return text;
}

inline pagedefaults::Values defaults() {
    linux_config::Preferences file;
    const auto get = [&](const char* key, double fallback) {
        double value;
        const auto text = file.text("Page", key);
        return linux_config::parseNumber(text.c_str(), value) ? value : fallback;
    };
    pagedefaults::Values result;
    result.enabled = file.flag("Page", "Default", false);
    if (!result.enabled) return result;
    result.width = points(get("WidthMM", 210));
    result.height = points(get("HeightMM", 297));
    const double across = get("Across", 1), down = get("Down", 1);
    if (across < 1 || across > 20 || down < 1 || down > 20 ||
            std::floor(across) != across || std::floor(down) != down) return {};
    result.across = int(across);
    result.down = int(down);
    return pagedefaults::valid(result) ? result : pagedefaults::Values();
}

inline void initialize(r62::core::Document& document) {
    const auto saved = defaults();
    if (!saved.enabled) return;
    const Page page{saved.width, saved.height, -saved.width * .5, -saved.height * .5, saved.across, saved.down};
    page.store(document);
}

inline void grid(cairo_t* cr, Vec2 first, Vec2 last, int across, int down) {
    const double width = (last.x - first.x) / across;
    const double height = (last.y - first.y) / down;
    for (int column = 1; column < across; ++column) {
        const double x = first.x + width * column;
        cairo_move_to(cr, x, first.y);
        cairo_line_to(cr, x, last.y);
    }
    for (int row = 1; row < down; ++row) {
        const double y = first.y + height * row;
        cairo_move_to(cr, first.x, y);
        cairo_line_to(cr, last.x, y);
    }
    cairo_stroke(cr);
}

inline void canvasGrid(cairo_t* cr, const Page& page, const r62::core::ViewTransform& view) {
    if (!page.valid() || (page.across == 1 && page.down == 1) || !std::isfinite(page.x) || !std::isfinite(page.y)) return;
    cairo_save(cr);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, .33, .41, .49, .55);
    const double dashes[]{4, 3};
    cairo_set_dash(cr, dashes, 2, 0);
    grid(cr, view.toDevice({page.x, page.y}),
        view.toDevice({page.x + page.width * page.across, page.y + page.height * page.down}), page.across, page.down);
    cairo_restore(cr);
}

class Editor {
    bool ownsDialog;
    Page initial;
    GtkWidget *dialog, *paper, *orientation, *makeDefault, *preview, *error;
    std::array<GtkWidget*, 4> fields;
    bool updating = false;

    bool read(Page& page) const {
        double values[4];
        for (size_t i = 0; i < fields.size(); ++i)
            if (!linux_config::parseNumber(gtk_entry_get_text(GTK_ENTRY(fields[i])), values[i])) return false;
        if (values[0] < 25.4 || values[0] > 5080 || values[1] < 25.4 || values[1] > 5080 ||
            values[2] < 1 || values[2] > 20 || values[3] < 1 || values[3] > 20 ||
            std::floor(values[2]) != values[2] || std::floor(values[3]) != values[3]) return false;
        page = initial;
        if (std::abs(points(values[0]) - initial.width) > .00015) page.width = points(values[0]);
        if (std::abs(points(values[1]) - initial.height) > .00015) page.height = points(values[1]);
        page.across = int(values[2]);
        page.down = int(values[3]);
        return true;
    }

    void setSizes(double width, double height) const {
        gtk_entry_set_text(GTK_ENTRY(fields[0]), number(std::round(width * 10000) / 10000).c_str());
        gtk_entry_set_text(GTK_ENTRY(fields[1]), number(std::round(height * 10000) / 10000).c_str());
    }

    void changeSize() {
        if (updating) return;
        changed = true;
        updating = true;
        const bool landscape = gtk_combo_box_get_active(GTK_COMBO_BOX(orientation)) == 1;
        const int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(paper));
        if (selected >= 0 && size_t(selected) < papers.size()) {
            const auto& preset = papers[size_t(selected)];
            setSizes(landscape ? preset.heightMM : preset.widthMM, landscape ? preset.widthMM : preset.heightMM);
        } else {
            double width, height;
            if (linux_config::parseNumber(gtk_entry_get_text(GTK_ENTRY(fields[0])), width) &&
                    linux_config::parseNumber(gtk_entry_get_text(GTK_ENTRY(fields[1])), height) &&
                    landscape != (width > height)) setSizes(height, width);
        }
        updating = false;
        updatePreview();
    }

    void dimensionsChanged(GtkWidget* field) {
        if (updating) return;
        changed = true;
        updating = true;
        if (field == fields[0] || field == fields[1]) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(paper), int(papers.size()));
            double width, height;
            if (linux_config::parseNumber(gtk_entry_get_text(GTK_ENTRY(fields[0])), width) &&
                linux_config::parseNumber(gtk_entry_get_text(GTK_ENTRY(fields[1])), height))
                gtk_combo_box_set_active(GTK_COMBO_BOX(orientation), width > height ? 1 : 0);
        }
        updating = false;
        updatePreview();
    }

    void updatePreview() const {
        gtk_label_set_text(GTK_LABEL(error), "");
        gtk_widget_queue_draw(preview);
    }

    void draw(cairo_t* cr) const {
        Page page;
        if (!read(page)) return;
        const auto width = page.width * page.across, height = page.height * page.down;
        const double viewportWidth = gtk_widget_get_allocated_width(preview);
        const double viewportHeight = gtk_widget_get_allocated_height(preview);
        const double scale = std::min((viewportWidth - 12.) / width, (viewportHeight - 12.) / height);
        if (scale <= 0.) return;
        const Vec2 first((viewportWidth - width * scale) / 2, (viewportHeight - height * scale) / 2);
        const Vec2 last = first + Vec2(width * scale, height * scale);
        cairo_rectangle(cr, first.x, first.y, last.x - first.x, last.y - first.y);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 1);
        cairo_set_source_rgb(cr, .43, .5, .57);
        cairo_stroke(cr);
        grid(cr, first, last, page.across, page.down);
    }

public:
    Editor(GtkWindow* owner, const Page& page, GtkBox* content = nullptr) : initial(page) {
        ownsDialog = content == nullptr;
        if (ownsDialog) {
            dialog = gtk_dialog_new_with_buttons(tr("页面与排版"), owner, GTK_DIALOG_MODAL,
                tr("关闭"), GTK_RESPONSE_CLOSE, tr("保存设置"), GTK_RESPONSE_ACCEPT, nullptr);
            gtk_window_set_default_size(GTK_WINDOW(dialog), 550, -1);
            content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
        } else dialog = GTK_WIDGET(owner);
        gtk_container_set_border_width(GTK_CONTAINER(content), 12);
        auto* horizontal = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
        gtk_box_pack_start(content, horizontal, TRUE, TRUE, 4);
        auto* form = GTK_GRID(gtk_grid_new());
        gtk_grid_set_row_spacing(form, 7);
        gtk_grid_set_column_spacing(form, 10);
        gtk_box_pack_start(GTK_BOX(horizontal), GTK_WIDGET(form), TRUE, TRUE, 0);
        const auto add = [&](int index, const char* caption, GtkWidget* control) {
            auto* label = gtk_label_new_with_mnemonic(tr(caption));
            gtk_label_set_xalign(GTK_LABEL(label), 0.);
            gtk_label_set_mnemonic_widget(GTK_LABEL(label), control);
            gtk_widget_set_hexpand(control, TRUE);
            const int row = (index / 2) * 2, column = index % 2;
            gtk_grid_attach(form, label, column, row, 1, 1);
            gtk_grid_attach(form, control, column, row + 1, 1, 1);
        };
        paper = gtk_combo_box_text_new();
        orientation = gtk_combo_box_text_new();
        int paperIndex = int(papers.size());
        for (size_t i = 0; i < papers.size(); ++i) {
            const auto& preset = papers[i];
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(paper), preset.name);
            if (std::abs(millimeters(std::min(page.width, page.height)) - preset.widthMM) < .1 &&
                    std::abs(millimeters(std::max(page.width, page.height)) - preset.heightMM) < .1) paperIndex = int(i);
        }
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(paper), tr("自定义"));
        gtk_combo_box_set_active(GTK_COMBO_BOX(paper), paperIndex);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(orientation), tr("纵向"));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(orientation), tr("横向"));
        gtk_combo_box_set_active(GTK_COMBO_BOX(orientation), page.width > page.height ? 1 : 0);
        add(0, "纸张尺寸", paper);
        add(1, "方向", orientation);
        const char* captions[]{"宽度（mm）", "高度（mm）", "横向张数", "纵向张数"};
        for (size_t i = 0; i < fields.size(); ++i) {
            fields[i] = gtk_entry_new();
            gtk_entry_set_width_chars(GTK_ENTRY(fields[i]), 12);
            gtk_entry_set_input_purpose(GTK_ENTRY(fields[i]), GTK_INPUT_PURPOSE_NUMBER);
            add(int(i + 2), captions[i], fields[i]);
        }
        setSizes(millimeters(page.width), millimeters(page.height));
        gtk_entry_set_text(GTK_ENTRY(fields[2]), std::to_string(page.across).c_str());
        gtk_entry_set_text(GTK_ENTRY(fields[3]), std::to_string(page.down).c_str());
        preview = gtk_drawing_area_new();
        gtk_widget_set_size_request(preview, 175, 180);
        gtk_box_pack_start(GTK_BOX(horizontal), preview, TRUE, TRUE, 0);
        makeDefault = gtk_check_button_new_with_label(tr("同时设为新建文档的默认页面（宽高与张数）"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(makeDefault), defaults().enabled);
        gtk_box_pack_start(content, makeDefault, FALSE, FALSE, 6);
        error = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(error), 0.);
        gtk_label_set_line_wrap(GTK_LABEL(error), TRUE);
        gtk_box_pack_start(content, error, FALSE, FALSE, 4);
        g_signal_connect(makeDefault, "toggled", G_CALLBACK(+[](GtkToggleButton*, Editor* self) {
            self->changed = true;
            self->updatePreview();
        }), this);
        for (auto* combo : {paper, orientation})
            g_signal_connect(combo, "changed", G_CALLBACK(+[](GtkComboBox*, Editor* self) { self->changeSize(); }), this);
        for (auto* field : fields)
            g_signal_connect(field, "changed", G_CALLBACK(+[](GtkWidget* changed, Editor* self) {
                self->dimensionsChanged(changed);
            }), this);
        g_signal_connect(preview, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, Editor* self)->gboolean {
            self->draw(cr);
            return FALSE;
        }), this);
    }

    ~Editor() { if (ownsDialog) gtk_widget_destroy(dialog); }
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    bool changed = false;

    void saved(const Page& page) {
        changed = false;
        initial = page;
        const bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(makeDefault));
        gtk_label_set_text(GTK_LABEL(error), tr(enabled ?
            "已保存 · 当前文档已更新，并设为新建文档默认页面" : "已保存 · 设置已更新"));
    }

    bool validate(Page& page) const {
        if (read(page) && page.valid()) return true;
        gtk_label_set_text(GTK_LABEL(error), tr("请输入有效数值：纸张宽高 25.4–5080 mm，横向和纵向各 1–20 张。"));
        return false;
    }

    bool store(const Page& page) const {
        const bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(makeDefault));
        const auto previous = defaults();
        if (!enabled && !previous.enabled) return true;
        std::string failure;
        if (linux_config::Preferences().replaceGroup("Page", {
            {"Default", enabled ? "true" : "false"}, {"WidthMM", number(millimeters(page.width))},
            {"HeightMM", number(millimeters(page.height))}, {"Across", std::to_string(page.across)},
            {"Down", std::to_string(page.down)}
        }, &failure)) return true;
        gtk_label_set_text(GTK_LABEL(error), linux_i18n::message(failure).c_str());
        return false;
    }

    bool run(Page& page) {
        gtk_widget_show_all(dialog);
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            if (validate(page) && store(page)) return true;
        return false;
    }
};
}
