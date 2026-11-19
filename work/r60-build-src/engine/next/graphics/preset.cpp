#include "next/graphics/preset.hpp"

#include <cstring>

namespace r62 {
namespace graphics {
namespace {

// ---------------------------------------------------------------------------
// The 32 geometry rows. Order and `sourceCommand` are copied from
// tools.csv rows 118..149; presetId == row index - 118.
// ---------------------------------------------------------------------------
const ShapePresetSpec kShapes[32] = {
    // Order and spelling copied from tools.csv rows 118..149 (the toolbar
    // order). presetId == row index, and presetId IS persisted, so this
    // table must never be reordered.
    { 0, "chooseDrawingTool_Rectangle_Shadowed", "r62.tool.shape.rectangle.shadowed", "Rectangle (Shadowed)", ShapeForm::Rectangle, ShapeAppearance::Shadowed, LineVariant::Solid, 0, true, "" },
    { 1, "chooseDrawingTool_Oval_Hollow", "r62.tool.shape.oval.hollow", "Oval (Hollow)", ShapeForm::Oval, ShapeAppearance::Hollow, LineVariant::Solid, 0, true, "" },
    { 2, "chooseDrawingTool_RoundRect_Hollow", "r62.tool.shape.roundrect.hollow", "RoundRect (Hollow)", ShapeForm::RoundRectangle, ShapeAppearance::Hollow, LineVariant::Solid, 0, true, "" },
    { 3, "chooseDrawingTool_Rectangle_Hollow", "r62.tool.shape.rectangle.hollow", "Rectangle (Hollow)", ShapeForm::Rectangle, ShapeAppearance::Hollow, LineVariant::Solid, 0, true, "" },
    { 4, "chooseDrawingTool_Circle_Dashed", "r62.tool.shape.circle.dashed", "Circle (Dashed)", ShapeForm::Circle, ShapeAppearance::Dashed, LineVariant::Solid, 0, true, "" },
    { 5, "chooseDrawingTool_Oval_Dashed", "r62.tool.shape.oval.dashed", "Oval (Dashed)", ShapeForm::Oval, ShapeAppearance::Dashed, LineVariant::Solid, 0, true, "" },
    { 6, "chooseDrawingTool_RoundRect_Dashed", "r62.tool.shape.roundrect.dashed", "RoundRect (Dashed)", ShapeForm::RoundRectangle, ShapeAppearance::Dashed, LineVariant::Solid, 0, true, "" },
    { 7, "chooseDrawingTool_Rectangle_Dashed", "r62.tool.shape.rectangle.dashed", "Rectangle (Dashed)", ShapeForm::Rectangle, ShapeAppearance::Dashed, LineVariant::Solid, 0, true, "" },
    { 8, "chooseDrawingTool_Circle_Shaded", "r62.tool.shape.circle.shaded", "Circle (Shaded)", ShapeForm::Circle, ShapeAppearance::Shaded, LineVariant::Solid, 0, true, "" },
    { 9, "chooseDrawingTool_Oval_Shaded", "r62.tool.shape.oval.shaded", "Oval (Shaded)", ShapeForm::Oval, ShapeAppearance::Shaded, LineVariant::Solid, 0, true, "" },
    { 10, "chooseDrawingTool_RoundRect_Shaded", "r62.tool.shape.roundrect.shaded", "RoundRect (Shaded)", ShapeForm::RoundRectangle, ShapeAppearance::Shaded, LineVariant::Solid, 0, true, "" },
    { 11, "chooseDrawingTool_Rectangle_Shaded", "r62.tool.shape.rectangle.shaded", "Rectangle (Shaded)", ShapeForm::Rectangle, ShapeAppearance::Shaded, LineVariant::Solid, 0, true, "" },
    { 12, "chooseDrawingTool_Circle_Solid", "r62.tool.shape.circle.solid", "Circle (Solid)", ShapeForm::Circle, ShapeAppearance::Solid, LineVariant::Solid, 0, true, "" },
    { 13, "chooseDrawingTool_Oval_Solid", "r62.tool.shape.oval.solid", "Oval (Solid)", ShapeForm::Oval, ShapeAppearance::Solid, LineVariant::Solid, 0, true, "" },
    { 14, "chooseDrawingTool_RoundRect_Solid", "r62.tool.shape.roundrect.solid", "RoundRect (Solid)", ShapeForm::RoundRectangle, ShapeAppearance::Solid, LineVariant::Solid, 0, true, "" },
    { 15, "chooseDrawingTool_Rectangle_Solid", "r62.tool.shape.rectangle.solid", "Rectangle (Solid)", ShapeForm::Rectangle, ShapeAppearance::Solid, LineVariant::Solid, 0, true, "" },
    { 16, "chooseDrawingTool_Circle_Shadowed", "r62.tool.shape.circle.shadowed", "Circle (Shadowed)", ShapeForm::Circle, ShapeAppearance::Shadowed, LineVariant::Solid, 0, true, "" },
    { 17, "chooseDrawingTool_Oval_Shadowed", "r62.tool.shape.oval.shadowed", "Oval (Shadowed)", ShapeForm::Oval, ShapeAppearance::Shadowed, LineVariant::Solid, 0, true, "" },
    { 18, "chooseDrawingTool_RoundRect_Shadowed", "r62.tool.shape.roundrect.shadowed", "RoundRect (Shadowed)", ShapeForm::RoundRectangle, ShapeAppearance::Shadowed, LineVariant::Solid, 0, true, "" },
    { 19, "chooseDrawingTool_Circle_Hollow", "r62.tool.shape.circle.hollow", "Circle (Hollow)", ShapeForm::Circle, ShapeAppearance::Hollow, LineVariant::Solid, 0, true, "" },
    { 20, "chooseDrawingTool_Line", "r62.tool.line.solid", "Line (Solid)", ShapeForm::Line, ShapeAppearance::Hollow, LineVariant::Solid, 0, true, "" },
    { 21, "chooseDrawingTool_Line_Dashed", "r62.tool.line.dashed", "Line (Dashed)", ShapeForm::Line, ShapeAppearance::Hollow, LineVariant::Dashed, 0, true, "" },
    { 22, "chooseDrawingTool_Line_Wavy", "r62.tool.line.wavy", "Line (Wavy)", ShapeForm::Line, ShapeAppearance::Hollow, LineVariant::Wavy, 0, true, "" },
    { 23, "chooseDrawingTool_Line_Bold", "r62.tool.line.bold", "Line (Bold)", ShapeForm::Line, ShapeAppearance::Hollow, LineVariant::Bold, 0, true, "" },
    { 24, "chooseDrawingTool_Arc_270", "r62.tool.arc.270.solid", "Arc 270 (Solid)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Solid, 270, true, "" },
    { 25, "chooseDrawingTool_Arc_180", "r62.tool.arc.180.solid", "Arc 180 (Solid)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Solid, 180, true, "" },
    { 26, "chooseDrawingTool_Arc_120", "r62.tool.arc.120.solid", "Arc 120 (Solid)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Solid, 120, true, "" },
    { 27, "chooseDrawingTool_Arc_90", "r62.tool.arc.90.solid", "Arc 90 (Solid)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Solid, 90, true, "" },
    { 28, "chooseDrawingTool_Arc_270_Dashed", "r62.tool.arc.270.dashed", "Arc 270 (Dashed)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Dashed, 270, true, "" },
    { 29, "chooseDrawingTool_Arc_180_Dashed", "r62.tool.arc.180.dashed", "Arc 180 (Dashed)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Dashed, 180, true, "" },
    { 30, "chooseDrawingTool_Arc_120_Dashed", "r62.tool.arc.120.dashed", "Arc 120 (Dashed)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Dashed, 120, true, "" },
    { 31, "chooseDrawingTool_Arc_90_Dashed", "r62.tool.arc.90.dashed", "Arc 90 (Dashed)", ShapeForm::Arc, ShapeAppearance::Hollow, LineVariant::Dashed, 90, true, "" },
};

// Order and spelling copied from tools.csv rows 150..157.
const BracketPresetSpec kBrackets[8] = {
    { 0, "chooseBracketTool_SquareBrackets", "r62.tool.bracket.squarebrackets", "Square pair [ ]", BracketFamily::Square, true, true, "" },
    { 1, "chooseBracketTool_Parenthesis", "r62.tool.bracket.parenthesis", "Parenthesis pair ( )", BracketFamily::Parenthesis, true, true, "" },
    { 2, "chooseBracketTool_CurlyBrackets", "r62.tool.bracket.curlybrackets", "Curly pair { }", BracketFamily::Curly, true, true, "" },
    { 3, "chooseBracketTool_DoubleDagger", "r62.tool.bracket.doubledagger", "Double dagger", BracketFamily::Dagger, false, true, "" },
    { 4, "chooseBracketTool_LeftSquareBracket", "r62.tool.bracket.leftsquarebracket", "Left square bracket [", BracketFamily::Square, false, true, "" },
    { 5, "chooseBracketTool_LeftParenthesis", "r62.tool.bracket.leftparenthesis", "Left parenthesis (", BracketFamily::Parenthesis, false, true, "" },
    { 6, "chooseBracketTool_LeftCurlyBracket", "r62.tool.bracket.leftcurlybracket", "Left curly brace {", BracketFamily::Curly, false, true, "" },
    { 7, "chooseBracketTool_SingleDagger", "r62.tool.bracket.singledagger", "Single dagger", BracketFamily::Dagger, false, true, "" },
};
} // namespace

// ---------------------------------------------------------------------------
// Table access
// ---------------------------------------------------------------------------
const ShapePresetSpec* shapePresets() { return kShapes; }
size_t shapePresetCount() { return 32; }
const BracketPresetSpec* bracketPresets() { return kBrackets; }
size_t bracketPresetCount() { return 8; }

const ShapePresetSpec* shapePresetById(uint32_t id) {
    return id < 32u ? &kShapes[id] : 0;
}

const ShapePresetSpec* shapePresetByCommand(const std::string& c) {
    for (size_t i = 0; i < 32; ++i) if (c == kShapes[i].sourceCommand) return &kShapes[i];
    return 0;
}

const ShapePresetSpec* shapePresetByToolId(const std::string& t) {
    for (size_t i = 0; i < 32; ++i) if (t == kShapes[i].toolId) return &kShapes[i];
    return 0;
}

const BracketPresetSpec* bracketPresetById(uint32_t id) {
    return id < 8u ? &kBrackets[id] : 0;
}

const BracketPresetSpec* bracketPresetByCommand(const std::string& c) {
    for (size_t i = 0; i < 8; ++i) if (c == kBrackets[i].sourceCommand) return &kBrackets[i];
    return 0;
}

const BracketPresetSpec* bracketPresetByToolId(const std::string& t) {
    for (size_t i = 0; i < 8; ++i) if (t == kBrackets[i].toolId) return &kBrackets[i];
    return 0;
}

const ShapePresetSpec* shapePresetFor(ShapeForm form, ShapeAppearance appearance) {
    for (size_t i = 0; i < 32; ++i) {
        const ShapePresetSpec& s = kShapes[i];
        if (s.form == form && s.appearance == appearance &&
            (s.form == ShapeForm::Circle || s.form == ShapeForm::Oval ||
             s.form == ShapeForm::Rectangle || s.form == ShapeForm::RoundRectangle))
            return &s;
    }
    return 0;
}

const ShapePresetSpec* shapePresetForLine(LineVariant line) {
    for (size_t i = 0; i < 32; ++i)
        if (kShapes[i].form == ShapeForm::Line && kShapes[i].line == line) return &kShapes[i];
    return 0;
}

const ShapePresetSpec* shapePresetForArc(int sweepDeg, LineVariant line) {
    for (size_t i = 0; i < 32; ++i)
        if (kShapes[i].form == ShapeForm::Arc && kShapes[i].arcSweepDeg == sweepDeg &&
            kShapes[i].line == line) return &kShapes[i];
    return 0;
}

const BracketPresetSpec* bracketPresetFor(BracketFamily family, bool paired) {
    for (size_t i = 0; i < 8; ++i)
        if (kBrackets[i].family == family && kBrackets[i].paired == paired) return &kBrackets[i];
    return 0;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
const char* shapeFormName(ShapeForm f) {
    switch (f) {
    case ShapeForm::Circle:         return "Circle";
    case ShapeForm::Oval:           return "Oval";
    case ShapeForm::Rectangle:      return "Rectangle";
    case ShapeForm::RoundRectangle: return "RoundRectangle";
    case ShapeForm::Line:           return "Line";
    case ShapeForm::Arc:            return "Arc";
    }
    return "?";
}

const char* appearanceName(ShapeAppearance a) {
    switch (a) {
    case ShapeAppearance::Hollow:   return "Hollow";
    case ShapeAppearance::Dashed:   return "Dashed";
    case ShapeAppearance::Shaded:   return "Shaded";
    case ShapeAppearance::Solid:    return "Solid";
    case ShapeAppearance::Shadowed: return "Shadowed";
    }
    return "?";
}

const char* lineVariantName(LineVariant l) {
    switch (l) {
    case LineVariant::Solid:  return "Solid";
    case LineVariant::Dashed: return "Dashed";
    case LineVariant::Wavy:   return "Wavy";
    case LineVariant::Bold:   return "Bold";
    }
    return "?";
}

const char* bracketFamilyName(BracketFamily f) {
    switch (f) {
    case BracketFamily::Square:      return "Square";
    case BracketFamily::Parenthesis: return "Parenthesis";
    case BracketFamily::Curly:       return "Curly";
    case BracketFamily::Dagger:      return "Dagger";
    }
    return "?";
}

const char* bracketSideName(BracketSide s) {
    switch (s) {
    case BracketSide::Pair:  return "Pair";
    case BracketSide::Left:  return "Left";
    case BracketSide::Right: return "Right";
    }
    return "?";
}

const char* bracketDirectionName(BracketDirection d) {
    switch (d) {
    case BracketDirection::Vertical:   return "Vertical";
    case BracketDirection::Horizontal: return "Horizontal";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Appearance semantics
// ---------------------------------------------------------------------------
bool appearanceFills(ShapeAppearance a) {
    return a == ShapeAppearance::Shaded || a == ShapeAppearance::Solid ||
           a == ShapeAppearance::Shadowed;
}

bool appearanceShadowed(ShapeAppearance a) { return a == ShapeAppearance::Shadowed; }

bool appearanceDashed(ShapeAppearance a) { return a == ShapeAppearance::Dashed; }

// [PROJ] dash pattern proportional to the line width, matching the rule T03
// applies to core::DashStyle::Dashed (proportional dashes).
void appearanceDashPattern(ShapeAppearance a, double lineWidth, double* on, double* off) {
    const double w = lineWidth > 0.0 ? lineWidth : 1.0;
    // A dashed box must read as dashed at normal zoom: short dashes with a gap
    // larger than the dash. The old 4:2 ratio looked almost solid at 0.6pt.
    if (a == ShapeAppearance::Dashed) { *on = 2.2 * w; *off = 3.0 * w; }
    else { *on = 0.0; *off = 0.0; }
}

// The fill colour actually used. Shaded/Shadowed are translucent; Solid is
// opaque. A user-supplied alpha (low byte != 0) is respected and only the
// default is replaced.
uint32_t appearanceFillColor(ShapeAppearance a, uint32_t userFillRGBA) {
    const uint32_t alpha = userFillRGBA & 0xFFu;
    if (alpha != 0u) return userFillRGBA;
    switch (a) {
    case ShapeAppearance::Shaded:   return (userFillRGBA & 0xFFFFFF00u) | 0x80u;
    case ShapeAppearance::Shadowed: return (userFillRGBA & 0xFFFFFF00u) | 0x60u;
    default:                        return userFillRGBA | 0xFFu;
    }
}

bool presetHasInterior(const ShapePresetSpec& spec) {
    if (spec.form == ShapeForm::Line) return false;
    if (spec.form == ShapeForm::Arc) return false;
    return appearanceFills(spec.appearance);
}

bool presetSupportsNonUniformScale(const ShapePresetSpec& spec) {
    // A Circle whose two axes differ is no longer a Circle. Rather than lie,
    // the editor swaps to the Oval preset (see model.hpp swapCircleForOval).
    return spec.form != ShapeForm::Circle;
}

} // namespace graphics
} // namespace r62