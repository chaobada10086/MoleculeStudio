#pragma once

#include "i18n.hpp"
#include "skin_palette.hpp"
#include <cstdio>

namespace linux_skin {
using linux_i18n::tr;
using studio_skin::Color;

inline std::string hex(Color color) {
    char bytes[8];
    std::snprintf(bytes, sizeof(bytes), "#%02X%02X%02X", unsigned(color.red), unsigned(color.green), unsigned(color.blue));
    return bytes;
}

inline bool parse(const std::string& input, Color& color) {
    if (input.size() != 7 || input.front() != '#') return false;
    unsigned value = 0;
    for (size_t i = 1; i < input.size(); ++i) {
        const auto next = g_ascii_xdigit_value(input[i]);
        if (next < 0) return false;
        value = (value << 4) | unsigned(next);
    }
    color = {int(value >> 16), int((value >> 8) & 255), int(value & 255)};
    return true;
}

struct Settings {
    int selected = 0;  // 0 uses the system style
    std::array<Color, 5> colors;

    Settings() {
        for (size_t i = 0; i < colors.size(); ++i) colors[i] = studio_skin::presets()[i].color;
    }
    static Settings load() {
        Settings result;
        linux_config::Preferences file;
        const auto index = file.text("Skin", "Selected");
        if (index.size() == 1 && index[0] >= '0' && index[0] <= '5') result.selected = index[0] - '0';
        for (size_t i = 0; i < result.colors.size(); ++i)
            parse(file.text("Skin", ("Color" + std::to_string(i + 1)).c_str()), result.colors[i]);
        return result;
    }
    bool save(std::string& error) const {
        std::map<std::string, std::string> entries{{"Selected", std::to_string(selected)}};
        for (size_t i = 0; i < colors.size(); ++i)
            entries.emplace("Color" + std::to_string(i + 1), hex(colors[i]));
        return linux_config::Preferences().replaceGroup("Skin", entries, &error);
    }
};

inline std::string stylesheet(const Settings& settings) {
    
    const studio_skin::Palette colors(settings.selected ? settings.colors[settings.selected - 1] : Color{23,117,109});
    const auto frame = hex(colors.frame), soft = hex(colors.soft), shade = hex(colors.tab), border = hex(colors.border);
    const auto accent = hex(colors.accent), hover = hex(colors.hover), selected = hex(colors.selected);
    return "window.moleculestudio-editor {background-color:" + frame + ";color:#22303B;}"
        "#canvas-caption-input,#canvas-caption-input text {background:#fff;color:#182631;caret-color:#182631;}"
        "#canvas-caption-input text selection {background:#c6e4f4;color:#182631;}"
        "#table-editor,#template-library {background:#f4f7f8;color:#243746;}"
        "#table-editor headerbar,#template-library headerbar {background:#edf3f5;color:#243746;}"
        "#table-editor spinbutton,#table-editor spinbutton entry {background:#fff;color:#243746;}"
        "#table-editor label,#template-library label {color:#243746;}"
        "#table-editor button,#template-library button,#template-library entry {background:#fff;color:#243746;border:1px solid #cfdbdf;border-radius:5px;box-shadow:none;}"
        "#table-editor button:hover,#template-library button:hover {background:#edf5f6;}"
        "#template-library button:checked {background:#e0f0f0;border-color:#38847f;}"
        "#table-editor treeview.view,#template-library list {background:#fff;color:#22323b;}"
        "#table-editor treeview header button {background:#edf3f5;color:#355468;border-radius:0;}"
        "#template-library row:selected {background:#d8ecec;}"
        "#arrow-palette {background:#fff;color:#243746;padding:4px;}"
        "#arrow-palette menuitem {color:#243746;padding:6px 10px;min-height:22px;}"
        "#arrow-palette menuitem:hover {background:#dfefef;color:#163e43;}"
        ".document-footer label {font-size:10.5px;color:#22303B;}"
        ".document-footer label.valence-warning {color:#b41e1e;}"
        ".document-footer #document-statistics {font-size:10px;color:#667584;}"
        ".canvas-scrollbar {-GtkScrollbar-has-backward-stepper:true;-GtkScrollbar-has-forward-stepper:true;}"
        ".canvas-scrollbar,.canvas-scrollbar contents,.canvas-scrollbar trough,.canvas-scrollbar button {"
        "background:#f0f0f0;border:0;box-shadow:none;padding:0;margin:0;border-radius:0;}"
        ".canvas-scrollbar.horizontal,.canvas-scrollbar.horizontal trough {min-height:16px;}"
        ".canvas-scrollbar.vertical,.canvas-scrollbar.vertical trough {min-width:16px;}"
        ".canvas-scrollbar slider {border:0;box-shadow:none;padding:0;margin:0;border-radius:0;}"
        ".canvas-scrollbar.horizontal slider {min-width:17px;min-height:16px;background:"
        "linear-gradient(to bottom,transparent 42%,#858585 42%,#858585 58%,transparent 58%);}"
        ".canvas-scrollbar.vertical slider {min-height:17px;min-width:16px;background:"
        "linear-gradient(to right,transparent 42%,#858585 42%,#858585 58%,transparent 58%);}"
        ".moleculestudio-editor .canvas-scrollbar button {min-width:16px;min-height:16px;"
        "padding:0;border:0;box-shadow:none;background:#f0f0f0;color:#858585;}"
        ".moleculestudio-editor .canvas-scrollbar button:hover {background:#dedede;color:#434343;}"
        ".moleculestudio-editor menubar {background-color:" + shade + ";color:#22303B;}"
        ".moleculestudio-editor menubar > menuitem:hover {background-color:" + selected + ";}"
        ".moleculestudio-editor button {background-image:none;background-color:" + soft +
            ";border-color:" + border + ";color:#22303B;}"
        ".moleculestudio-editor button:hover {background-color:" + hover + ";}"
        ".moleculestudio-editor button:active,.moleculestudio-editor button:checked {background-color:" + selected + ";}"
        ".moleculestudio-editor button:disabled {color:#747F88;}"
        ".moleculestudio-editor button:focus {outline-color:" + accent + ";}"
        ".moleculestudio-editor headerbar {min-height:39px;padding:0;background-image:none;background-color:"+shade+";color:#203044;}"
        ".moleculestudio-editor headerbar .document-brand label {font-size:14.6px;font-weight:bold;}"
        ".moleculestudio-editor headerbar button.titlebutton {background:transparent;border:0;border-radius:0;box-shadow:none;padding:0;min-width:36px;min-height:24px;}"
        ".moleculestudio-editor headerbar button.titlebutton:hover {background:"+hover+";}"
        ".moleculestudio-editor headerbar .document-mark {background-color:"+accent+";border-radius:6px;}"
        ".moleculestudio-editor headerbar .document-tab {border-radius:0;padding:0 4px 0 11px;border-top:2px solid transparent;}"
        ".moleculestudio-editor headerbar .document-tab label {font-size:13px;}"
        ".moleculestudio-editor headerbar button.document-action {background:transparent;border:0;box-shadow:none;padding:0;min-width:28px;min-height:24px;}"
        ".moleculestudio-editor headerbar button.document-action:hover {background:"+hover+";}"
        ".moleculestudio-editor headerbar .document-tab button {background:transparent;border:0;box-shadow:none;padding:0 4px;min-width:16px;min-height:22px;}"
        ".moleculestudio-editor headerbar .current-document {background-color:white;border-top-color:"+accent+";}"
        ".moleculestudio-editor headerbar .current-document label {font-weight:bold;}"
        ".moleculestudio-editor headerbar label {color:#203044;}"
        ".moleculestudio-editor headerbar button.selection-info {background:"+hover+";border:1px solid "+accent+";border-radius:7px;padding:1px 9px;min-height:22px;}"
        ".moleculestudio-editor headerbar button.selection-info:hover {background:"+selected+";}"
        ".moleculestudio-editor headerbar .selection-info label {color:"+hex(colors.strong)+";font-weight:bold;}"
        ".moleculestudio-editor .ribbon-card {background:white;color:#24374a;border-radius:4px;padding:3px 4px;}"
        ".moleculestudio-editor .ribbon-card label {font-size:10px;}"
        ".moleculestudio-editor button.ribbon-tool,.moleculestudio-editor button.ribbon-tab,.moleculestudio-editor button.ribbon-quick {min-height:20px;min-width:20px;padding:2px 3px;border:0;box-shadow:none;text-shadow:none;background-image:none;}"
        ".moleculestudio-editor button.ribbon-tool {background:transparent;}"
        ".moleculestudio-editor button.ai-ribbon-primary,.moleculestudio-editor button.ai-ribbon-settings {padding:8px 12px;border-radius:8px;}"
        ".moleculestudio-editor button.ai-ribbon-primary {background:"+hover+";}"
        ".moleculestudio-editor button.ai-ribbon-settings {background:"+soft+";}"
        ".moleculestudio-editor button.ai-ribbon-primary box box label,.moleculestudio-editor button.ai-ribbon-settings box box label {font-size:13px;font-weight:bold;}"
        "button.ribbon-tool.generation-style:checked,.moleculestudio-editor button.ribbon-tool.generation-style:checked {background:"+selected+";color:#203044;box-shadow:inset 0 0 0 1px "+accent+";border-radius:4px;}"
        ".moleculestudio-editor button.ribbon-tool.active-tool {background:"+selected+";box-shadow:inset 0 0 0 1px "+accent+";}"
        ".moleculestudio-editor button.ribbon-tab {background:transparent;padding:3px 9px;}"
        ".moleculestudio-editor menubar.ribbon-file {background:transparent;}"
        ".moleculestudio-editor menubar.ribbon-file > menuitem {font-size:13.5px;font-weight:bold;}"
        ".moleculestudio-editor button.ribbon-tab {font-size:13.5px;}"
        ".moleculestudio-editor button.ribbon-page {padding:3px 10px;}"
        ".moleculestudio-editor button.ribbon-page:checked {font-weight:bold;}"
        ".moleculestudio-editor .narrow-ribbon button.ribbon-tab {padding:3px 4px;}"
        ".moleculestudio-editor button.ribbon-tab:checked {background:"+accent+";color:white;}"
        ".moleculestudio-editor button.ribbon-quick {font-size:11px;background:"+hover+";padding:2px 7px;}"
        ".moleculestudio-editor button.ribbon-overflow {min-width:18px;padding:2px;}"
        ".moleculestudio-editor .home-card {border-radius:0;padding:3px 2px;}"
        ".moleculestudio-editor button.home-group-menu {color:"+accent+";min-width:20px;min-height:20px;padding:2px;}"
        ".moleculestudio-editor button.home-quick {min-width:76px;min-height:20px;}"
        ".moleculestudio-editor .narrow-ribbon button.home-quick {min-width:40px;}"
        ".moleculestudio-editor button.ribbon-tool:hover,.moleculestudio-editor button.ribbon-tab:hover {background:"+selected+";color:#203044;}";
}

inline bool applyStylesheet(const Settings& settings, std::string& error) {
    auto* screen = gdk_screen_get_default();
    if (!screen) { error = "无法加载窗口皮肤"; return false; }
    static auto* provider = gtk_css_provider_new();
    static bool installed = false;
    const auto css = stylesheet(settings);
    GError* failure = nullptr;
    if (!gtk_css_provider_load_from_data(provider, css.c_str(), css.size(), &failure)) {
        error = failure ? failure->message : "无法加载窗口皮肤";
        g_clear_error(&failure);
        return false;
    }
    if (!installed) {
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        installed = true;
    }
    return true;
}

struct Dialog {
    Settings palette;
    GtkComboBox* presets;
    GtkEntry* color;
    GtkColorChooser* button;
    GtkWidget* reset;
    bool syncing = false;

    void select() {
        const int index = gtk_combo_box_get_active(presets);
        const bool enabled = index > 0;
        palette.selected = std::max(0, index);
        syncing = true;
        gtk_widget_set_sensitive(GTK_WIDGET(color), enabled);
        gtk_widget_set_sensitive(GTK_WIDGET(button), enabled);
        gtk_widget_set_sensitive(reset, enabled);
        gtk_entry_set_text(color, enabled ? hex(palette.colors[index - 1]).c_str() : "");
        if (enabled) {
            const auto rgb = palette.colors[index - 1];
            const GdkRGBA native{rgb.red / 255., rgb.green / 255., rgb.blue / 255., 1};
            gtk_color_chooser_set_rgba(button, &native);
        }
        syncing = false;
    }
    void typed() {
        Color value;
        if (syncing || !palette.selected || !parse(gtk_entry_get_text(color), value)) return;
        palette.colors[palette.selected - 1] = value;
        const GdkRGBA native{value.red / 255., value.green / 255., value.blue / 255., 1};
        gtk_color_chooser_set_rgba(button, &native);
    }
};

inline std::string configure(GtkWindow* owner) {
    auto* dialog = gtk_dialog_new_with_buttons(tr("皮肤"), owner, GTK_DIALOG_MODAL,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("保存设置"), GTK_RESPONSE_OK, nullptr);
    auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    gtk_box_set_spacing(content, 8);
    auto* chooser = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(chooser, tr("系统主题"));
    for (const auto& preset : studio_skin::presets())
        gtk_combo_box_text_append_text(chooser, linux_i18n::message(linux_i18n::utf8(preset.name)).c_str());
    auto* color = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_width_chars(color, 9);
    gtk_entry_set_max_length(color, 7);
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(color)), tr("强调色（HEX）"));
    auto* pick = gtk_color_button_new();
    auto* reset = gtk_button_new_with_label(tr("恢复此预设"));
    Dialog settings{Settings::load(), GTK_COMBO_BOX(chooser), color, GTK_COLOR_CHOOSER(pick), reset};
    gtk_combo_box_set_active(settings.presets, settings.palette.selected);
    gtk_box_pack_start(content, GTK_WIDGET(chooser), FALSE, FALSE, 0);
    auto* row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
    for (auto* widget : {gtk_label_new(tr("强调色（HEX）")), GTK_WIDGET(color), pick, reset})
        gtk_box_pack_start(row, widget, FALSE, FALSE, 0);
    gtk_box_pack_start(content, GTK_WIDGET(row), FALSE, FALSE, 0);
    auto* note = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_box_pack_start(content, note, FALSE, FALSE, 0);
    g_signal_connect_swapped(chooser, "changed", G_CALLBACK(+[](Dialog* settings) { settings->select(); }), &settings);
    g_signal_connect_swapped(color, "changed", G_CALLBACK(+[](Dialog* settings) { settings->typed(); }), &settings);
    g_signal_connect(pick, "color-set", G_CALLBACK((+[](GtkColorButton*, Dialog* settings) {
        if (!settings->palette.selected || settings->syncing) return;
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(settings->button, &rgba);
        const auto byte = [](double channel) { return int(std::lround(channel * 255)); };
        const Color color{byte(rgba.red), byte(rgba.green), byte(rgba.blue)};
        gtk_entry_set_text(settings->color, hex(color).c_str());
    })), &settings);
    g_signal_connect(reset, "clicked", G_CALLBACK(+[](GtkButton*, Dialog* settings) {
        const int index = settings->palette.selected - 1;
        if (index < 0) return;
        settings->palette.colors[index] = studio_skin::presets()[index].color;
        settings->select();
    }), &settings);
    settings.select();
    gtk_widget_show_all(dialog);
    std::string error;
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        Color accent;
        if (settings.palette.selected && !parse(gtk_entry_get_text(color), accent)) {
            gtk_label_set_text(GTK_LABEL(note), tr("请输入六位 HEX 颜色，例如 #4062BE。"));
            continue;
        }
        if (settings.palette.save(error)) applyStylesheet(settings.palette, error);
        break;
    }
    gtk_widget_destroy(dialog);
    return error;
}
}
