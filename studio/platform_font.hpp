#pragma once

#include "next/text/glyph_source.hpp"
#include <memory>

#ifdef __linux__
namespace r62 { namespace text { GlyphProvider* createLinuxGlyphProvider(); } }
#endif

namespace studio {
inline std::unique_ptr<r62::text::GlyphProvider> createGlyphProvider() {
#ifdef __linux__
    return std::unique_ptr<r62::text::GlyphProvider>(r62::text::createLinuxGlyphProvider());
#else
    return std::unique_ptr<r62::text::GlyphProvider>(r62::text::createGdiGlyphProvider("Arial", "Microsoft YaHei"));
#endif
}
}
