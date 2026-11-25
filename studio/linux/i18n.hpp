#pragma once

#include "config.hpp"
#include "ui_language.hpp"
#include "ui_strings.hpp"
#include <gtk/gtk.h>

namespace linux_i18n {
inline std::wstring wide(const std::string& text) {
    glong length = 0;
    auto* decoded = g_utf8_to_ucs4(text.c_str(), text.size(), nullptr, &length, nullptr);
    if (!decoded) return {};
    std::wstring result(decoded, decoded + length);
    g_free(decoded);
    return result;
}

inline std::string utf8(const std::wstring& text) {
    const std::vector<gunichar> points(text.begin(), text.end());
    if (points.empty()) return {};
    auto* encoded = g_ucs4_to_utf8(points.data(), points.size(), nullptr, nullptr, nullptr);
    std::string result = encoded ? encoded : "";
    g_free(encoded);
    return result;
}

inline int& current() {
    static int value = [] {
        const auto saved = linux_config::Preferences().text("Interface", "Language");
        for (int index = 0; index < studio_i18n::languageCount; ++index)
            if (saved == utf8(studio_i18n::code(index))) return index;
        return 0;
    }();
    return value;
}

inline void applyDirection() {
    gtk_widget_set_default_direction(current() == 11 ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
}

inline bool save(int language) {
    if (language < 0 || language >= studio_i18n::languageCount) return false;
    if (!linux_config::Preferences().text("Interface", "Language", utf8(studio_i18n::code(language)))) return false;
    current() = language;
    applyDirection();
    return true;
}

inline std::string message(const std::string& source) {
    if (current() == 0 || source.empty()) return source;
    if (auto* gtk = nativeTranslation(source, current())) return gtk;
    static const std::string ellipsis = "…";
    if (source.size() > ellipsis.size() && source.compare(source.size() - ellipsis.size(),
            ellipsis.size(), ellipsis) == 0) {
        const auto stem = source.substr(0, source.size() - ellipsis.size());
        if (auto* gtk = nativeTranslation(stem, current())) return std::string(gtk) + ellipsis;
    }
    const auto decoded = wide(source);
    if (decoded.empty()) return source;
    return utf8(studio_i18n::tr(decoded, current()));
}

// GTK copies strings supplied to its constructors. Keep translated literals
// stable for calls that supply more than one label through varargs.
inline const char* tr(const char* source) {
    if (current() == 0 || !*source) return source;
    static std::map<std::pair<int, std::string>, std::string> labels;
    auto key = std::make_pair(current(), std::string(source));
    auto found = labels.find(key);
    if (found == labels.end()) found = labels.emplace(std::move(key), message(source)).first;
    return found->second.c_str();
}

inline void renderLabel(GtkWidget* widget, const char* source) {
    const auto* translated = tr(source);
    if (GTK_IS_MENU_ITEM(widget)) gtk_menu_item_set_label(GTK_MENU_ITEM(widget), translated);
    else if (GTK_IS_BUTTON(widget)) gtk_button_set_label(GTK_BUTTON(widget), translated);
    else if (GTK_IS_LABEL(widget)) gtk_label_set_text(GTK_LABEL(widget), translated);
}

inline GtkWidget* bindLabel(GtkWidget* widget, const char* source) {
    g_object_set_data_full(G_OBJECT(widget), "moleculestudio-label", g_strdup(source), g_free);
    renderLabel(widget, source);
    return widget;
}

inline void renderIconLabel(GtkWidget* widget, const char* source) {
    gtk_widget_set_tooltip_text(widget, tr(source));
    atk_object_set_name(gtk_widget_get_accessible(widget), tr(source));
}

inline void bindIconLabel(GtkWidget* widget, const char* source) {
    g_object_set_data_full(G_OBJECT(widget), "moleculestudio-icon-label", g_strdup(source), g_free);
    renderIconLabel(widget, source);
}

inline void refreshLabels(GtkWidget* widget) {
    if (auto* name = static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "moleculestudio-icon-label")))
        renderIconLabel(widget, name);
    auto* source = static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "moleculestudio-label"));
    if (source) renderLabel(widget, source);
    if (GTK_IS_MENU_ITEM(widget))
        if (auto* submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget))) refreshLabels(submenu);
    if (GTK_IS_CONTAINER(widget))
        gtk_container_foreach(GTK_CONTAINER(widget),
            +[](GtkWidget* child, gpointer) { refreshLabels(child); }, nullptr);
}
}
