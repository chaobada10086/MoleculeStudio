#pragma once
#include "next/arrows_curves/tools.hpp"

namespace r62 {
namespace arrows {

size_t pickSelectedArrowHandle(const std::vector<Handle>& handles, core::Vec2 pointer,
                               double deviceRadius, const core::ViewTransform& view);
bool mechanismGlyphTakesPriority(const core::Document& document, core::Vec2 pointer,
                                 const core::ViewTransform& view, double deviceRadius,
                                 uint32_t presetId, uint32_t modifiers, const Handle* handle);

} // namespace arrows
} // namespace r62
