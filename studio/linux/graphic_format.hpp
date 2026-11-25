#pragma once

#include "engine/graphic_style.hpp"
#include "interop.hpp"
#include "i18n.hpp"
#include <array>
#include <optional>

namespace linux_graphic_format {
using namespace r62;
using namespace r62::core;
using integration::Session;
using linux_i18n::tr;

inline std::optional<uint32_t> color(GtkWindow* parent, const char* title, uint32_t initial, bool canClear = false) {
    auto* dialog = gtk_color_chooser_dialog_new(tr(title), parent);
    auto* chooser = GTK_COLOR_CHOOSER(dialog);
    gtk_color_chooser_set_use_alpha(chooser, FALSE);
    auto unpack = [](uint32_t value) {
        return GdkRGBA{render::colorRed(value) / 255., render::colorGreen(value) / 255., render::colorBlue(value) / 255., 1.};
    };
    std::array<GdkRGBA, 8> palette{unpack(0x000000ff), unpack(0xdc1e1eff), unpack(0x0046dcff), unpack(0x008c46ff),
        unpack(0xb400b4ff), unpack(0xe68200ff), unpack(0x6e6e6eff), unpack(0xffffffff)};
    gtk_color_chooser_add_palette(chooser, GTK_ORIENTATION_HORIZONTAL, 8, palette.size(), palette.data());
    const auto selected = unpack(initial);
    gtk_color_chooser_set_rgba(chooser, &selected);
    const int clearResponse = 1;
    if (canClear) gtk_dialog_add_button(GTK_DIALOG(dialog), tr("无填充"), clearResponse);
    const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
    std::optional<uint32_t> result;
    if (response == GTK_RESPONSE_OK) {
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(chooser, &rgba);
        const auto byte = [](double channel) { return static_cast<uint32_t>(std::lround(channel * 255.)); };
        result = (byte(rgba.red) << 24) | (byte(rgba.green) << 16) | (byte(rgba.blue) << 8) | 255u;
    } else if (response == clearResponse) result = 0;
    gtk_widget_destroy(dialog);
    return result;
}

inline std::optional<double> width(GtkWindow* parent, double current, double fallback) {
    auto* dialog = gtk_dialog_new_with_buttons(tr("线宽"), parent, GTK_DIALOG_MODAL, tr("取消"), GTK_RESPONSE_CANCEL,
        tr("确定"), GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    auto* body = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(body), 12);
    auto* automatic = gtk_check_button_new_with_label(tr("自动（按样式）"));
    auto* spin = gtk_spin_button_new_with_range(.1, 12., .05);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), current > 0. ? current : fallback);
    atk_object_set_name(gtk_widget_get_accessible(spin), tr("线宽（磅）"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(automatic), current <= 0.);
    gtk_widget_set_sensitive(spin, current > 0.);
    g_signal_connect(automatic, "toggled", G_CALLBACK(+[](GtkToggleButton* automatic, GtkWidget* spin) {
        gtk_widget_set_sensitive(spin, !gtk_toggle_button_get_active(automatic));
    }), spin);
    gtk_box_pack_start(GTK_BOX(body), automatic, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(body), spin, FALSE, FALSE, 6);
    gtk_widget_show_all(dialog);
    std::optional<double> result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
        result = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(automatic)) ? 0. :
            gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
    }
    gtk_widget_destroy(dialog);
    return result;
}

inline Error apply(Session& session, std::optional<double> width, std::optional<uint32_t> color) {
    if (!width && !color) return {};
    std::vector<ObjectRef> refs;
    for (auto ref : studioio::expanded(session.doc, session.selected))
        if ((width && studio::graphic_style::supportsWidth(ref.kind)) ||
            (color && studio::graphic_style::supported(ref.kind))) refs.push_back(ref);
    if (!refs.empty()) {
        auto result = session.submit(Command("线宽与颜色", kInvalidateGeometry | kInvalidateAppearance,
            [refs, width, color](CommandContext& context) {
                for (auto ref : refs) {
                    const auto error = studio::graphic_style::apply(context.document, ref, width.value_or(0.),
                        color.value_or(0), width.has_value(), color.has_value());
                    if (!error.ok()) return CommandResult::fail(error.code, error.message);
                }
                return CommandResult::success();
            }), "线宽与颜色");
        if (!result.ok()) return result;
    }
    if (width) session.graphicsWidth = *width;
    if (color) session.graphicsColor = *color;
    return {};
}

inline Error fill(Session& session, std::optional<uint32_t> color) {
    if (!color) return {};
    const auto refs = studioio::expanded(session.doc, session.selected);
    if (!refs.empty()) {
        const auto result = session.submit(Command("图案填充", kInvalidateGeometry | kInvalidateAppearance,
            [refs, color](CommandContext& context) {
                for (auto ref : refs) {
                    const auto error = studio::graphic_style::fill(context.document, ref, *color);
                    if (!error.ok()) return CommandResult::fail(error.code, error.message);
                }
                return CommandResult::success();
            }), "图案填充");
        if (!result.ok()) return result;
    }
    session.graphicsFillSet = true;
    session.graphicsFill = *color;
    return {};
}
}
