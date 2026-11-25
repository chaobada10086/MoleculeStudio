#pragma once

#include "third_party/tinyxml2/tinyxml2.h"
#include "next/render/scene_api.hpp"
#include "metafile_compression.hpp"
#include <gtk/gtk.h>
#include <cstring>
#include <memory>
#include <sstream>
#include <string_view>

namespace linux_image {
using r62::core::Vec2;
using r62::render::Primitive;
using Bytes = std::string_view;
using Pixels = std::unique_ptr<GdkPixbuf, decltype(&g_object_unref)>;

constexpr char embeddedPrefix[] = "CDXML-EMF:";
constexpr size_t maxPngBytes = 16u * 1024 * 1024;
constexpr size_t maxMetafileBytes = 64u * 1024 * 1024;
constexpr uint32_t graphicsVersion = 0xdbc01002;
constexpr uint32_t metafileSignature = 0x464d4520;
constexpr uint32_t plusSignature = 0x2b464d45;
constexpr uint64_t maxPixels = 16u * 1024 * 1024;

inline bool validDimensions(uint32_t width, uint32_t height) {
    return width && height && width <= 16000 && height <= 16000 && uint64_t(width) * height <= maxPixels;
}

inline bool isImage(const Primitive& primitive) {
    return primitive.kind == r62::render::PrimKind::Rect && primitive.points.size() == 4 &&
        primitive.text.compare(0, sizeof(embeddedPrefix) - 1, embeddedPrefix) == 0;
}

inline uint32_t little(Bytes data, size_t at, unsigned count = 4) {
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= uint32_t(static_cast<unsigned char>(data[at + i])) << (8 * i);
    return value;
}

inline uint32_t floatWord(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline std::string fromHex(Bytes text, size_t limit) {
    if (text.size() > 2 * limit) return {};
    std::string bytes;
    bytes.reserve(text.size() / 2);
    int high = -1;
    for (unsigned char ch : text) {
        if (g_ascii_isspace(ch)) continue;
        const int digit = g_ascii_xdigit_value(ch);
        if (digit < 0) return {};
        if (high < 0) high = digit;
        else {
            bytes += static_cast<char>(16 * high + digit);
            high = -1;
        }
    }
    return high < 0 ? bytes : std::string();
}

inline std::string toHex(Bytes bytes) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(2 * bytes.size());
    for (unsigned char byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

inline bool dimensions(Bytes png, int& width, int& height) {
    if (png.size() < 33 || png.size() > maxPngBytes ||
        png.substr(0, 16) != Bytes("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16)) return false;
    auto big = [&](size_t at) {
        uint32_t n = 0;
        for (size_t i = 0; i < 4; ++i) n = (n << 8) | static_cast<unsigned char>(png[at + i]);
        return n;
    };
    const uint32_t w = big(16), h = big(20);
    if (!validDimensions(w, h)) return false;
    width = w;
    height = h;
    return true;
}

inline Pixels decode(Bytes bytes, const char* type) {
    if (bytes.empty() || bytes.size() > maxPngBytes) return {nullptr, g_object_unref};
    auto* loader = gdk_pixbuf_loader_new_with_type(type, nullptr);
    if (!loader) return {nullptr, g_object_unref};
    bool allowed = true;
    g_signal_connect(loader, "size-prepared", G_CALLBACK(+[](GdkPixbufLoader* loader, gint w, gint h, gpointer data) {
        if (validDimensions(w, h)) return;
        *static_cast<bool*>(data) = false;
        gdk_pixbuf_loader_set_size(loader, 1, 1);
    }), &allowed);
    const bool wrote = gdk_pixbuf_loader_write(loader,
        reinterpret_cast<const guchar*>(bytes.data()), bytes.size(), nullptr);
    const bool closed = gdk_pixbuf_loader_close(loader, nullptr);
    auto* pixbuf = allowed && wrote && closed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    if (pixbuf) g_object_ref(pixbuf);
    g_object_unref(loader);
    return {pixbuf, g_object_unref};
}

inline Pixels decodePng(Bytes png) {
    int width, height;
    if (!dimensions(png, width, height)) return {nullptr, g_object_unref};
    return decode(png, "png");
}

// Windows paste stores one compressed bitmap in an EMF+ image object. Large
// images span multiple comments, each carrying the same total object size.
struct ImageFrame {
    double x = 0, y = 0, width = 0, height = 0;
};

inline std::string pngFromMetafile(Bytes file, ImageFrame* frame = nullptr) {
    if (file.size() < 88 || little(file, 0) != 1 || little(file, 40) != metafileSignature) return {};
    std::string image;
    std::string png;
    size_t total = 0;
    uint32_t objectId = 0;
    bool drawn = false;
    uint32_t dpiX = 0, dpiY = 0;
    for (size_t at = 0; at + 8 <= file.size();) {
        const size_t length = little(file, at + 4);
        if (length < 8 || length % 4 || length > file.size() - at) return {};
        const Bytes record = file.substr(at, length);
        at += length;
        if (little(record, 0) != 70 || length < 16 || little(record, 12) != plusSignature) continue;
        const size_t end = 12 + size_t(little(record, 8));
        if (end < 16 || end > length) return {};
        for (size_t next = 16; next + 12 <= end;) {
            const size_t size = little(record, next + 4), dataSize = little(record, next + 8);
            if (size < 12 || size % 4 || size > end - next || dataSize > size - 12) return {};
            const uint32_t type = little(record, next, 2), flags = little(record, next + 2, 2);
            Bytes data = record.substr(next + 12, dataSize);
            next += size;
            if (type == 0x4001 && data.size() >= 16) {
                dpiX = little(data, 8);
                dpiY = little(data, 12);
            }
            if (type == 0x4001 || type == 0x4002) continue;
            if (type == 0x401b) {
                int w, h;
                if (drawn || flags != (0x4000 | objectId) || data.size() != 40 || !dimensions(png, w, h) ||
                    little(data, 0) != 0xffffffff || little(data, 4) != 2 || little(data, 8) || little(data, 12) ||
                    little(data, 16) != floatWord(w) || little(data, 20) != floatWord(h) || little(data, 24) != 3 ||
                    little(data, 28) || little(data, 32) != uint32_t(w) || little(data, 36) != (uint32_t(h) << 16))
                    return {};
                drawn = true;
                continue;
            }
            if (drawn || !png.empty() || type != 0x4008 || (flags & 0x7f00) != 0x0500) return {};
            if (flags & 0x8000) {
                if (data.size() <= 4) return {};
                const size_t expected = little(data, 0);
                data.remove_prefix(4);
                if (!total) {
                    total = expected;
                    objectId = flags & 255;
                }
                if (total != expected || (flags & 255) != objectId ||
                    total > maxPngBytes + 31 || image.size() > total ||
                    data.size() > total - image.size()) return {};
                image.append(data);
                if (image.size() != total) continue;
                data = image;
            } else if (total) {
                return {};
            } else {
                objectId = flags & 255;
            }
            if (data.size() < 28 || little(data, 4) != 1 || little(data, 24) != 1) return {};
            png.assign(data.substr(28));
        }
    }
    if (drawn && frame && dpiX && dpiY) {
        // EMF specifies the page frame in 0.01 mm, while the supported
        // DrawImagePoints record above places the bitmap at its pixel size.
        auto coordinate = [&](size_t at) { return double(int32_t(little(file, at))); };
        const double left = coordinate(24), top = coordinate(28);
        const double w = (coordinate(32) - left) * dpiX / 2540.;
        const double h = (coordinate(36) - top) * dpiY / 2540.;
        if (w >= 1 && h >= 1)
            *frame = {left * dpiX / 2540., top * dpiY / 2540., std::round(w), std::round(h)};
    }
    return drawn ? png : std::string();
}

struct Metafile {
    std::string bytes;
    bool enhanced = true;
};

inline Pixels load(const Primitive& primitive, Metafile* vector = nullptr, ImageFrame* frame = nullptr) {
    tinyxml2::XMLDocument xml;
    if (xml.Parse(primitive.text.c_str() + sizeof(embeddedPrefix) - 1) != tinyxml2::XML_SUCCESS)
        return {nullptr, g_object_unref};
    const auto* object = xml.RootElement();
    if (!object) return {nullptr, g_object_unref};
    if (const char* png = object->Attribute("PNG")) {
        auto pixels = decodePng(fromHex(png, maxPngBytes));
        if (pixels) return pixels;
    }
    std::string emf;
    if (const char* data = object->Attribute("CompressedEnhancedMetafile")) {
        try {
            const auto expected = object->UnsignedAttribute("UncompressedEnhancedMetafileSize");
            if (expected > 0 && expected <= 32u * 1024 * 1024 &&
                std::strlen(data) <= maxMetafileBytes * 2) {
                const auto decoded = embedded::inflate(embedded::decode(data, true), expected);
                emf.assign(decoded.begin(), decoded.end());
            }
        } catch (const std::exception&) {
        }
    }
    if (emf.empty())
        if (const char* data = object->Attribute("EnhancedMetafile")) emf = fromHex(data, maxMetafileBytes);
    if (emf.size() >= 88 && little(emf, 0) == 1 && little(emf, 40) == metafileSignature) {
        auto pixels = decodePng(pngFromMetafile(emf, frame));
        if (pixels) return pixels;
        if (vector) vector->bytes = std::move(emf);
    } else if (vector) {
        if (const char* wmf = object->Attribute("WindowsMetafile")) {
            vector->bytes = fromHex(wmf, maxMetafileBytes);
            vector->enhanced = false;
        }
    }
    return {nullptr, g_object_unref};
}

inline void put(std::string& data, uint32_t value, unsigned count = 4) {
    for (unsigned i = 0; i < count; ++i) data += static_cast<char>(value >> (8 * i));
}

inline std::string words(std::initializer_list<uint32_t> values) {
    std::string result;
    for (auto value : values) put(result, value);
    return result;
}

inline std::string plusRecord(uint16_t type, uint16_t flags, std::string payload) {
    payload.resize((payload.size() + 3) & ~size_t(3), '\0');
    std::string record;
    put(record, type, 2);
    put(record, flags, 2);
    return record + words({uint32_t(12 + payload.size()), uint32_t(payload.size())}) + payload;
}

inline void appendComment(std::string& file, const std::string& plus) {
    file += words({70, uint32_t(16 + plus.size()), uint32_t(4 + plus.size()), plusSignature});
    file += plus;
}

// Write an EMF+ only image so released Windows builds can display Linux
// pastes using their existing EMF decoder, without a second copy of the PNG.
inline std::string makeMetafile(Bytes png, int width, int height) {
    const uint32_t w = width, h = height;
    std::string image = words({graphicsVersion, 1, 0, 0, 0, 0, 1});
    image.append(png);
    image.resize((image.size() + 3) & ~size_t(3), '\0');
    std::string body;
    appendComment(body, plusRecord(0x4001, 0, words({graphicsVersion, 1, 96, 96})));
    constexpr size_t chunk = 65004;
    const uint32_t records = 4 + uint32_t((image.size() + chunk - 1) / chunk);
    if (image.size() <= chunk) {
        appendComment(body, plusRecord(0x4008, 0x0500, std::move(image)));
    } else {
        for (size_t at = 0; at < image.size(); at += chunk) {
            appendComment(body, plusRecord(0x4008, 0x8500,
                words({uint32_t(image.size())}) + image.substr(at, chunk)));
        }
    }
    auto draw = words({0xffffffff, 2, 0, 0, floatWord(w), floatWord(h), 3});
    for (auto value : {0u, 0u, w, 0u, 0u, h}) put(draw, value, 2);
    appendComment(body, plusRecord(0x401b, 0x4000, std::move(draw)));
    appendComment(body, plusRecord(0x4002, 0, {}));
    body += words({14, 20, 0, 16, 20});
    constexpr uint32_t headerSize = 108;
    const auto frame = [](uint32_t pixels) { return (pixels * 2540 + 48) / 96; };
    return words({1, headerSize, 0, 0, w - 1, h - 1, 0, 0, frame(w), frame(h),
        metafileSignature, 0x10000, uint32_t(headerSize + body.size()), records, 1, 0, 0, 0,
        960, 960, 254, 254, 0, 0, 0, 254000, 254000}) + body;
}

inline std::string clipboardDocument(Bytes bytes, const char* type, double visibleWidth, double visibleHeight) {
    const bool pngType = std::strcmp(type, "png") == 0;
    const auto image = pngType ? decodePng(bytes) : decode(bytes, type);
    if (!image) return {};
    gchar* buffer = nullptr;
    gsize length = 0;
    const bool encoded = pngType || gdk_pixbuf_save_to_buffer(image.get(), &buffer, &length, "png", nullptr, nullptr);
    const std::unique_ptr<gchar, decltype(&g_free)> converted(buffer, g_free);
    if (!encoded || length > maxPngBytes) return {};
    const auto png = pngType ? bytes : Bytes(converted.get(), length);
    const int width = gdk_pixbuf_get_width(image.get()), height = gdk_pixbuf_get_height(image.get());
    const double factor = std::min({.75, visibleWidth * .7 / width, visibleHeight * .7 / height});
    if (!std::isfinite(factor) || factor <= 0) return {};
    const Vec2 half(width * factor / 2, height * factor / 2);
    std::ostringstream xml;
    xml.imbue(std::locale::classic());
    xml.precision(12);
    xml << "<CDXML><page><embeddedobject id=\"1\" MoleculeStudioRaster=\"yes\" BoundingBox=\""
        << -half.x << ' ' << -half.y << ' ' << half.x << ' ' << half.y
        << "\" EnhancedMetafile=\"" << toHex(makeMetafile(png, width, height)) << "\"/></page></CDXML>";
    return xml.str();
}

inline r62::core::Error clipboardBytes(std::string& bytes, const char*& type) {
    struct Format {
        const char* mime;
        const char* type;
    };
    auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    for (const auto& format : {Format{"image/png", "png"}, Format{"image/jpeg", "jpeg"},
                               Format{"image/bmp", "bmp"}, Format{"image/x-ms-bmp", "bmp"}}) {
        const auto target = gdk_atom_intern_static_string(format.mime);
        if (!gtk_clipboard_wait_is_target_available(clipboard, target)) continue;
        auto* data = gtk_clipboard_wait_for_contents(clipboard, target);
        const int size = data ? gtk_selection_data_get_length(data) : -1;
        if (size > 0 && static_cast<size_t>(size) <= maxPngBytes)
            bytes.assign(reinterpret_cast<const char*>(gtk_selection_data_get_data(data)), size);
        if (data) gtk_selection_data_free(data);
        if (bytes.empty()) return {r62::core::ErrorCode::ParseError, "无法读取剪贴板图像，或图像过大"};
        type = format.type;
        return {};
    }
    return {};
}
}
