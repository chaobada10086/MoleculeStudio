#pragma once

#include "raster_image.hpp"
#include <glib/gstdio.h>
#include <librsvg/rsvg.h>
#include <sys/resource.h>
#include <cmath>

namespace linux_image {
using Svg = std::unique_ptr<RsvgHandle, decltype(&g_object_unref)>;
constexpr size_t maxSvgBytes = 32u * 1024 * 1024;

struct SvgImage {
    Svg handle{nullptr, g_object_unref};
    double width = 0, height = 0;
    size_t bytes = 0;
};

inline void limitConverter(gpointer) {
    const rlimit fileLimit{maxSvgBytes, maxSvgBytes};
    const rlimit memoryLimit{1024u * 1024 * 1024, 1024u * 1024 * 1024};
    const rlimit cpuLimit{7, 7};
    const rlimit coreLimit{0, 0};
    setrlimit(RLIMIT_FSIZE, &fileLimit);
    setrlimit(RLIMIT_AS, &memoryLimit);
    setrlimit(RLIMIT_CPU, &cpuLimit);
    setrlimit(RLIMIT_CORE, &coreLimit);
}

inline SvgImage vectorImage(const Metafile& file) {
    SvgImage result;
    if (file.bytes.empty() || file.bytes.size() > maxMetafileBytes) return result;
    const char* program = file.enhanced ? "/usr/bin/emf2svg-conv" : "/usr/bin/wmf2svg";
    if (!g_file_test(program, G_FILE_TEST_IS_EXECUTABLE)) return result;

    gchar* temporary = g_dir_make_tmp("moleculestudio-metafile-XXXXXX", nullptr);
    if (!temporary) return result;
    std::string directory(temporary);
    g_free(temporary);
    const auto input = directory + (file.enhanced ? "/drawing.emf" : "/drawing.wmf");
    const auto output = directory + "/drawing.svg";
    if (g_file_set_contents(input.c_str(), file.bytes.data(), file.bytes.size(), nullptr)) {
        std::vector<std::string> arguments{"/usr/bin/timeout", "-s", "KILL", "8", program};
        if (file.enhanced)
            arguments.insert(arguments.end(), {"-p", "-i", input, "-o", output});
        else arguments.insert(arguments.end(), {"--inline", "-o", output, input});
        std::vector<gchar*> argv;
        for (auto& argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        const auto flags = GSpawnFlags(G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL);
        int status = -1;
        GStatBuf size{};
        if (g_spawn_sync(directory.c_str(), argv.data(), nullptr, flags, limitConverter, nullptr,
                nullptr, nullptr, &status, nullptr) && g_spawn_check_wait_status(status, nullptr) &&
            g_stat(output.c_str(), &size) == 0 && size.st_size > 0 && size_t(size.st_size) <= maxSvgBytes) {
            gchar* svg = nullptr;
            gsize bytes = 0;
            if (g_file_get_contents(output.c_str(), &svg, &bytes, nullptr) && bytes <= maxSvgBytes) {
                std::string markup(svg, bytes);
                // libwmf emits a pre-namespace SVG 1.0 root. librsvg requires the SVG namespace.
                if (!file.enhanced) {
                    const auto start = markup.find("<svg");
                    const auto end = markup.find('>', start);
                    if (end != std::string::npos && markup.find("xmlns=", start) > end)
                        markup.insert(start + 4, " xmlns=\"http://www.w3.org/2000/svg\"");
                }
                result.handle.reset(rsvg_handle_new_from_data(
                    reinterpret_cast<const guint8*>(markup.data()), markup.size(), nullptr));
                result.bytes = bytes;
            }
            g_free(svg);
        }
    }
    g_remove(input.c_str());
    g_remove(output.c_str());
    g_rmdir(directory.c_str());

    if (result.handle && !rsvg_handle_get_intrinsic_size_in_pixels(result.handle.get(), &result.width, &result.height)) {
        gboolean hasViewbox = FALSE;
        RsvgRectangle viewbox{};
        rsvg_handle_get_intrinsic_dimensions(result.handle.get(), nullptr, nullptr, nullptr, nullptr, &hasViewbox, &viewbox);
        if (hasViewbox) {
            result.width = viewbox.width;
            result.height = viewbox.height;
        }
    }
    if (!std::isfinite(result.width) || !std::isfinite(result.height) || result.width <= 0 ||
        result.height <= 0 || result.width > 1000000 || result.height > 1000000) result.handle.reset();
    return result;
}
}
