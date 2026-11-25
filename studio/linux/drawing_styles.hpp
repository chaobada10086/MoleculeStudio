#pragma once

#include "drawing_style.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include <gtk/gtk.h>
#include <algorithm>

namespace linux_drawing {
using studio_drawing::Style;
using studio_drawing::fields;
using studio_drawing::journalStyles;
using linux_i18n::tr;

inline std::string canonicalName(const std::wstring& name) {
    auto* folded = g_utf8_casefold(linux_i18n::utf8(name).c_str(), -1);
    std::string result = folded ? folded : "";
    g_free(folded);
    return result;
}

struct Preferences {
    Style defaults;
    std::vector<Style> presets;
    static constexpr size_t maxPresets = 100;

    static Preferences load() {
        const linux_config::Preferences file("drawing.ini");
        Preferences result;
        result.defaults = read(file, "Default.");
        const auto storedCount = file.text("Styles", "Count");
        char* end = nullptr;
        const auto count = g_ascii_strtoull(storedCount.c_str(), &end, 10);
        if (*end || count > maxPresets) return result;
        for (size_t i = 0; i < count; ++i) {
            const auto prefix = templatePrefix(i);
            const auto name = linux_i18n::wide(file.text("Styles", (prefix + "Preset").c_str()));
            if (!Style::safe(name, 64) || isBuiltin(name) || result.find(name) != result.presets.end()) continue;
            result.presets.push_back(read(file, prefix));
        }
        return result;
    }

    bool save(std::string& error) const {
        if (!defaults.valid() || presets.size() > maxPresets) {
            error = "请检查字体名称及详细绘图参数。";
            return false;
        }
        std::map<std::string, std::string> entries;
        write(defaults, "Default.", entries);
        entries["Count"] = std::to_string(presets.size());
        for (size_t i = 0; i < presets.size(); ++i) write(presets[i], templatePrefix(i), entries);
        return linux_config::Preferences("drawing.ini").replaceGroup("Styles", entries, &error);
    }

    bool store(const Style& style) {
        if (!style.valid() || !Style::safe(style.preset, 64) || isBuiltin(style.preset)) return false;
        auto at = find(style.preset);
        if (at != presets.end()) *at = style;
        else if (presets.size() < maxPresets) presets.push_back(style);
        else return false;
        return true;
    }

    bool erase(const std::wstring& name) {
        auto at = find(name);
        if (at == presets.end()) return false;
        presets.erase(at);
        return true;
    }

private:
    static bool isBuiltin(const std::wstring& name) {
        const auto folded = canonicalName(name);
        return std::any_of(journalStyles().begin(), journalStyles().end(), [&](const auto& preset) {
            return canonicalName(preset.name) == folded;
        });
    }

    std::vector<Style>::iterator find(const std::wstring& name) {
        const auto folded = canonicalName(name);
        return std::find_if(presets.begin(), presets.end(), [&](const auto& preset) {
            return canonicalName(preset.preset) == folded;
        });
    }

    static std::string templatePrefix(size_t index) { return "Template." + std::to_string(index) + "."; }

    static Style read(const linux_config::Preferences& file, const std::string& prefix) {
        Style style;
        const auto value = [&](const char* key) { return file.text("Styles", (prefix + key).c_str()); };
        const auto name = linux_i18n::wide(value("Preset"));
        if (const auto* preset = studio_drawing::journalStyle(name)) style.use(*preset);
        else if (Style::safe(name, 80)) style.preset = name;
        for (const auto& field : fields) {
            const auto stored = value(field.key);
            if (stored.empty()) continue;
            char* end = nullptr;
            const auto number = g_ascii_strtod(stored.c_str(), &end);
            if (!*end && std::isfinite(number) && number >= field.low && number <= field.high)
                style.*field.member = number;
        }
        const auto labelFont = linux_i18n::wide(value("Font"));
        if (Style::safe(labelFont, 120)) style.fontName = labelFont;
        const auto captionFont = linux_i18n::wide(value("CaptionFont"));
        style.captionFontName = Style::safe(captionFont, 120) ? captionFont : style.fontName;
        return style;
    }

    static void write(const Style& style, const std::string& prefix, std::map<std::string, std::string>& entries) {
        entries[prefix + "Preset"] = linux_i18n::utf8(style.preset);
        entries[prefix + "Font"] = linux_i18n::utf8(style.fontName);
        entries[prefix + "CaptionFont"] = linux_i18n::utf8(style.captionFontName);
        for (const auto& field : fields) {
            char value[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_dtostr(value, sizeof(value), style.*field.member);
            entries[prefix + field.key] = value;
        }
    }
};

class Editor {
    bool ownsDialog;
    Preferences draft = Preferences::load();
    GtkWidget* dialog;
    GtkWidget* styles;
    GtkWidget* remove;
    GtkWidget* status;
    GtkWidget* applyCurrent;
    GtkWidget* atomFont;
    GtkWidget* captionFont;
    std::array<GtkWidget*, fields.size()> numbers;
    bool filling = false;

    void setFields(const Style& style) const {
        for (size_t i = 0; i < fields.size(); ++i) {
            char value[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(value, sizeof(value), "%.12g", style.*fields[i].member);
            gtk_entry_set_text(GTK_ENTRY(numbers[i]), value);
        }
        gtk_entry_set_text(GTK_ENTRY(atomFont), linux_i18n::utf8(style.fontName).c_str());
        gtk_entry_set_text(GTK_ENTRY(captionFont), linux_i18n::utf8(style.captionFontName).c_str());
    }

public:
    bool changed = false;
    bool readFields(Style& style) const {
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto* input = gtk_entry_get_text(GTK_ENTRY(numbers[i]));
            const auto& field = fields[i];
            double parsed;
            if (!linux_config::parseNumber(input, parsed) || parsed < field.low || parsed > field.high) {
                const auto message = std::string(tr("请输入有效数值")) + " " + tr(fields[i].label);
                gtk_label_set_text(GTK_LABEL(status), message.c_str());
                gtk_widget_grab_focus(numbers[i]);
                return false;
            }
            style.*field.member = parsed;
        }
        style.fontName = linux_i18n::wide(gtk_entry_get_text(GTK_ENTRY(atomFont)));
        style.captionFontName = linux_i18n::wide(gtk_entry_get_text(GTK_ENTRY(captionFont)));
        const auto* selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(styles));
        style.preset = linux_i18n::wide(selected ? selected : "自定义参数");
        if (style.valid()) return true;
        gtk_label_set_text(GTK_LABEL(status), tr("请检查字体名称及详细绘图参数。"));
        return false;
    }

private:
    void refreshList(const Style& current) {
        auto* combo = GTK_COMBO_BOX_TEXT(styles);
        filling = true;
        gtk_combo_box_text_remove_all(combo);
        auto append = [&](const std::wstring& name) {
            const auto text = linux_i18n::utf8(name);
            gtk_combo_box_text_append(combo, text.c_str(), tr(text.c_str()));
        };
        for (const auto& preset : journalStyles()) append(preset.name);
        for (const auto& preset : draft.presets) append(preset.preset);
        const auto name = linux_i18n::utf8(current.preset);
        if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), name.c_str())) {
            append(current.preset);
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), name.c_str());
        }
        const bool custom = std::any_of(draft.presets.begin(), draft.presets.end(), [&](const auto& preset) {
            return preset.preset == current.preset;
        });
        gtk_widget_set_sensitive(remove, custom);
        setFields(current);
        filling = false;
    }

    void choose() {
        if (filling) return;
        const auto* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(styles));
        if (!id) return;
        const auto name = linux_i18n::wide(id);
        Style chosen = draft.defaults;
        if (const auto* builtin = studio_drawing::journalStyle(name)) chosen.use(*builtin);
        else for (const auto& custom : draft.presets)
            if (name == custom.preset) { chosen = custom; break; }
        gtk_widget_set_sensitive(remove, !studio_drawing::journalStyle(name) &&
            std::any_of(draft.presets.begin(), draft.presets.end(), [&](const auto& p) { return p.preset == name; }));
        setFields(chosen);
        gtk_label_set_text(GTK_LABEL(status), "");
    }

    void saveCustom() {
        Style style;
        if (!readFields(style)) return;
        auto* question = gtk_dialog_new_with_buttons(tr("保存为自定义样式"), GTK_WINDOW(dialog), GTK_DIALOG_MODAL,
            tr("取消"), GTK_RESPONSE_CANCEL, tr("保存"), GTK_RESPONSE_ACCEPT, nullptr);
        auto* input = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(input), 64);
        gtk_entry_set_activates_default(GTK_ENTRY(input), TRUE);
        gtk_entry_set_placeholder_text(GTK_ENTRY(input), tr("样式名称"));
        if (gtk_widget_get_sensitive(remove))
            gtk_entry_set_text(GTK_ENTRY(input), linux_i18n::utf8(style.preset).c_str());
        gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(question))), 12);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(question))), input);
        gtk_dialog_set_default_response(GTK_DIALOG(question), GTK_RESPONSE_ACCEPT);
        gtk_widget_show_all(question);
        const bool confirmed = gtk_dialog_run(GTK_DIALOG(question)) == GTK_RESPONSE_ACCEPT;
        auto* name = g_strdup(gtk_entry_get_text(GTK_ENTRY(input)));
        style.preset = linux_i18n::wide(g_strstrip(name));
        g_free(name);
        gtk_widget_destroy(question);
        if (!confirmed) return;
        if (!draft.store(style)) {
            gtk_label_set_text(GTK_LABEL(status), tr("请使用有效且不与内置预设重复的样式名称。"));
            return;
        }
        refreshList(style);
        gtk_label_set_text(GTK_LABEL(status), "");
    }

    void deleteCustom() {
        const auto* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(styles));
        if (!id || !draft.erase(linux_i18n::wide(id))) return;
        refreshList(draft.defaults);
        gtk_label_set_text(GTK_LABEL(status), "");
    }

public:
    explicit Editor(GtkWindow* owner, GtkBox* content = nullptr) {
        ownsDialog = content == nullptr;
        if (ownsDialog) {
            dialog = gtk_dialog_new_with_buttons(tr("绘图预设"), owner, GTK_DIALOG_MODAL,
                tr("取消"), GTK_RESPONSE_CANCEL, tr("应用"), GTK_RESPONSE_ACCEPT, nullptr);
            gtk_window_set_default_size(GTK_WINDOW(dialog), 580, -1);
            content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
        } else dialog = GTK_WIDGET(owner);
        gtk_container_set_border_width(GTK_CONTAINER(content), 12);
        auto* presets = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        styles = gtk_combo_box_text_new();
        auto* add = gtk_button_new_with_label(tr("保存为自定义样式…"));
        remove = gtk_button_new_with_label(tr("删除自定义样式"));
        gtk_box_pack_start(GTK_BOX(presets), styles, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(presets), add, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(presets), remove, FALSE, FALSE, 0);
        gtk_box_pack_start(content, presets, FALSE, FALSE, 4);
        auto* grid = GTK_GRID(gtk_grid_new());
        gtk_grid_set_row_spacing(grid, 6);
        gtk_grid_set_column_spacing(grid, 10);
        gtk_box_pack_start(content, GTK_WIDGET(grid), FALSE, FALSE, 6);
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto& field = fields[i];
            auto* input = gtk_entry_new();
            gtk_entry_set_input_purpose(GTK_ENTRY(input), GTK_INPUT_PURPOSE_NUMBER);
            gtk_entry_set_width_chars(GTK_ENTRY(input), 15);
            numbers[i] = input;
            appendRow(grid, int(i), field.label, input);
        }
        atomFont = gtk_entry_new();
        captionFont = gtk_entry_new();
        const auto fontRow = [&](int row, const char* label, GtkWidget* input) {
            gtk_entry_set_max_length(GTK_ENTRY(input), 120);
            auto* select = gtk_button_new_with_label(tr("选择字体…"));
            g_signal_connect(select, "clicked", G_CALLBACK(+[](GtkButton*, GtkEntry* field) {
                auto* owner = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(field)));
                auto* choice = gtk_font_chooser_dialog_new(tr("选择字体"), owner);
                auto* picker = GTK_FONT_CHOOSER(choice);
                gtk_window_set_modal(GTK_WINDOW(choice), TRUE);
                gtk_font_chooser_set_level(picker, GTK_FONT_CHOOSER_LEVEL_FAMILY);
                gtk_font_chooser_set_font(picker, gtk_entry_get_text(field));
                if (gtk_dialog_run(GTK_DIALOG(choice)) == GTK_RESPONSE_OK) {
                    if (auto* family = gtk_font_chooser_get_font_family(picker))
                        gtk_entry_set_text(field, pango_font_family_get_name(family));
                }
                gtk_widget_destroy(choice);
            }), input);
            appendRow(grid, row, label, input);
            gtk_grid_attach(grid, select, 2, row, 1, 1);
        };
        fontRow(int(fields.size()), "原子字体", atomFont);
        fontRow(int(fields.size() + 1), "文字字体", captionFont);
        applyCurrent = gtk_check_button_new_with_label(tr("同时应用到当前文档的默认格式"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(applyCurrent), TRUE);
        gtk_box_pack_start(content, applyCurrent, FALSE, FALSE, 6);
        status = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(status), 0);
        gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
        gtk_box_pack_start(content, status, FALSE, FALSE, 4);
        g_signal_connect(styles, "changed", G_CALLBACK(+[](GtkComboBox*, Editor* editor) { editor->choose(); }), this);
        g_signal_connect(add, "clicked", G_CALLBACK(+[](GtkButton*, Editor* editor) { editor->saveCustom(); }), this);
        g_signal_connect(remove, "clicked", G_CALLBACK(+[](GtkButton*, Editor* editor) { editor->deleteCustom(); }), this);
        refreshList(draft.defaults);
        const auto mark = +[](GtkWidget*, Editor* editor) { editor->changed = true; };
        for (auto* field : numbers) g_signal_connect(field, "changed", G_CALLBACK(mark), this);
        for (auto* field : {atomFont, captionFont, styles}) g_signal_connect(field, "changed", G_CALLBACK(mark), this);
        g_signal_connect(applyCurrent, "toggled", G_CALLBACK(mark), this);
    }

    ~Editor() { if (ownsDialog) gtk_widget_destroy(dialog); }
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    bool store(const Style& style, bool& current) {
        draft.defaults = style;
        std::string error;
        if (draft.save(error)) {
            current = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(applyCurrent));
            changed = false;
            return true;
        }
        gtk_label_set_text(GTK_LABEL(status), linux_i18n::message(error).c_str());
        return false;
    }

    bool run(Style& style, bool& current) {
        gtk_widget_show_all(dialog);
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            if (readFields(style) && store(style, current)) return true;
        return false;
    }

private:
    static void appendRow(GtkGrid* grid, int row, const char* label, GtkWidget* input) {
        auto* shortLabel = gtk_label_new_with_mnemonic(tr(label));
        gtk_label_set_xalign(GTK_LABEL(shortLabel), 0);
        gtk_label_set_mnemonic_widget(GTK_LABEL(shortLabel), input);
        gtk_widget_set_hexpand(input, TRUE);
        gtk_grid_attach(grid, shortLabel, 0, row, 1, 1);
        gtk_grid_attach(grid, input, 1, row, 1, 1);
    }
};
}
