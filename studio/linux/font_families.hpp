#pragma once

#include <pango/pangocairo.h>
#include <string>

namespace linux_font {
// Fontconfig aliases can outrank the later faces in a Pango family list.
inline bool installed(const char* name) {
    auto* map = pango_cairo_font_map_get_default();
    auto* family = pango_font_map_get_family(map,name);
    return family && !g_ascii_strcasecmp(pango_font_family_get_name(family),name);
}

// Liberation provides Windows-compatible line spacing when substituting outlines.
// Keep the document's requested face first. The Windows outline provider
// falls back to SimSun for missing Han glyphs, including those in Arial runs.
inline std::string families(const std::string& requested, bool lineMetrics = false) {
    std::string names = requested.empty() ? "Arial" : requested;
    if (!g_ascii_strcasecmp(names.c_str(), "Arial"))
        names += ",Liberation Sans";
    else if (!g_ascii_strcasecmp(names.c_str(), "Times New Roman"))
        names = installed("Times New Roman") ? names : (lineMetrics ? "Liberation Serif" : "Nimbus Roman,Liberation Serif");
    else if (!g_ascii_strcasecmp(names.c_str(), "Courier New"))
        names = installed("Courier New") ? names : (lineMetrics ? "Liberation Mono" : "FreeMono,Liberation Mono");
    else if (!g_ascii_strcasecmp(names.c_str(), "Microsoft YaHei") || names == "微软雅黑")
        names += ",Noto Sans CJK SC";
    return names + ",SimSun,Noto Serif CJK SC";
}
}
