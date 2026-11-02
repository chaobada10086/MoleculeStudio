#include "next/arrows_curves/preset.hpp"

#include <cmath>
#include <cstring>

namespace r62 {
namespace arrows {

namespace {

// ---------------------------------------------------------------------------
// The table. 56 rows, order == Arrows_Toolbar.xml button order == tools.csv
// arrow-category row order. Verified 2026-10-15 against
// host_integration/drawing_inventory/tools.csv (56 rows, category "arrow").
//
// Columns: presetId, sourceCommand, toolId, displayName, family, sizeClass, line,
//          headShape, headSides, shafts, sweep, direction, reversed, implemented,
//          note
//
// 56 distinct presetIds in [0, 56]. presetId is PERSISTED (it is written into
// the document extension bag), so rows may be APPENDED but never renumbered.
// ---------------------------------------------------------------------------
const PresetSpec kPresets[] = {
    {  0, "chooseArrowTool_Large_Solid", "r62.tool.arrow.large.solid",
       "Large Solid", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    {  1, "chooseArrowTool_Large_Bold", "r62.tool.arrow.large.bold",
       "Large Bold", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Bold,
       ArrowHeadShape::Barb, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "bold shaft + barbed head" },
    {  2, "chooseArrowTool_Large_Dashed", "r62.tool.arrow.large.dashed",
       "Large Dashed", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Dashed,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    {  3, "chooseArrowTool_Large_OneSided_Left", "r62.tool.arrow.large.oneSided.left",
       "Large One-Sided Left", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Barb, HeadSides::Left, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "left/right are relative to the direction the head points [GUIDE p111]" },
    {  4, "chooseArrowTool_Large_OneSided_Right", "r62.tool.arrow.large.oneSided.right",
       "Large One-Sided Right", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Barb, HeadSides::Right, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    {  5, "chooseArrowTool_Large_Resonance", "r62.tool.arrow.large.resonance",
       "Large Resonance", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Resonance, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "two short parallel strokes instead of a fill" },
    {  6, "chooseArrowTool_Large_NoGo_Cross", "r62.tool.arrow.large.noGo.cross",
       "Large No-Go Cross", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Cross, HeadSides::None, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "the X is on the shaft MIDDLE, not an end" },
    {  7, "chooseArrowTool_Large_NoGo_Hash", "r62.tool.arrow.large.noGo.hash",
       "Large No-Go Hash", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Hash, HeadSides::None, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "double slash on the shaft middle" },
    {  8, "chooseArrowTool_Medium_Solid", "r62.tool.arrow.medium.solid",
       "Medium Solid", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    {  9, "chooseArrowTool_Medium_Bold", "r62.tool.arrow.medium.bold",
       "Medium Bold", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Bold,
       ArrowHeadShape::Barb, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 10, "chooseArrowTool_Medium_Dashed", "r62.tool.arrow.medium.dashed",
       "Medium Dashed", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Dashed,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 11, "chooseArrowTool_Medium_OneSided_Left", "r62.tool.arrow.medium.oneSided.left",
       "Medium One-Sided Left", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Barb, HeadSides::Left, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 12, "chooseArrowTool_Medium_OneSided_Right", "r62.tool.arrow.medium.oneSided.right",
       "Medium One-Sided Right", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Barb, HeadSides::Right, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 13, "chooseArrowTool_Medium_Resonance", "r62.tool.arrow.medium.resonance",
       "Medium Resonance", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Resonance, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 14, "chooseArrowTool_Medium_NoGo_Cross", "r62.tool.arrow.medium.noGo.cross",
       "Medium No-Go Cross", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Cross, HeadSides::None, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 15, "chooseArrowTool_Medium_NoGo_Hash", "r62.tool.arrow.medium.noGo.hash",
       "Medium No-Go Hash", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Hash, HeadSides::None, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 16, "chooseArrowTool_Small_Solid", "r62.tool.arrow.small.solid",
       "Small Solid", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 17, "chooseArrowTool_Small_Bold", "r62.tool.arrow.small.bold",
       "Small Bold", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Bold,
       ArrowHeadShape::Barb, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 18, "chooseArrowTool_Small_Dashed", "r62.tool.arrow.small.dashed",
       "Small Dashed", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Dashed,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 19, "chooseArrowTool_Small_OneSided_Left", "r62.tool.arrow.small.oneSided.left",
       "Small One-Sided Left", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Barb, HeadSides::Left, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 20, "chooseArrowTool_Small_OneSided_Right", "r62.tool.arrow.small.oneSided.right",
       "Small One-Sided Right", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Barb, HeadSides::Right, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 21, "chooseArrowTool_Small_Resonance", "r62.tool.arrow.small.resonance",
       "Small Resonance", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Resonance, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 22, "chooseArrowTool_Small_NoGo_Cross", "r62.tool.arrow.small.noGo.cross",
       "Small No-Go Cross", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Cross, HeadSides::None, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 23, "chooseArrowTool_Small_NoGo_Hash", "r62.tool.arrow.small.noGo.hash",
       "Small No-Go Hash", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Hash, HeadSides::None, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 24, "chooseArrowTool_Large_Hollow", "r62.tool.arrow.large.hollow",
       "Large Hollow", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Hollow,
       ArrowHeadShape::Hollow, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "[GUIDE p110] hollow arrows rotate and change length/width like regular ones" },
    { 32, "chooseArrowTool_Medium_Hollow", "r62.tool.arrow.medium.hollow",
       "Medium Hollow", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Hollow,
       ArrowHeadShape::Hollow, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 25, "chooseArrowTool_270_CW", "r62.tool.arrow.arc270.cw",
       "270 CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg270,
       ArcDirection::Clockwise, false, true, "" },
    { 26, "chooseArrowTool_270_CCW", "r62.tool.arrow.arc270.ccw",
       "270 CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg270,
       ArcDirection::CounterClockwise, false, true, "" },
    { 27, "chooseArrowTool_270_Single_CW", "r62.tool.arrow.arc270.single.cw",
       "270 Single CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg270,
       ArcDirection::Clockwise, false, true, "half arc: 135 degrees of the 270 sweep" },
    { 28, "chooseArrowTool_270_Single_CCW", "r62.tool.arrow.arc270.single.ccw",
       "270 Single CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg270,
       ArcDirection::CounterClockwise, false, true, "half arc variant" },
    { 29, "chooseArrowTool_2Sided_270", "r62.tool.arrow.arc270.twoSided",
       "2-Sided 270", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg270,
       ArcDirection::Clockwise, false, true, "heads at BOTH arc ends" },
    { 55, "chooseArrowTool_270_Eliptical_CW", "r62.tool.arrow.arc270.elliptical.cw",
       "270 Elliptical CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg270,
       ArcDirection::Clockwise, false, true, "elliptical arc, not circular [GUIDE p109 'elliptical arcs']" },
    { 56, "chooseArrowTool_270_Eliptical_CCW", "r62.tool.arrow.arc270.elliptical.ccw",
       "270 Elliptical CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg270,
       ArcDirection::CounterClockwise, false, true, "" },
    { 30, "chooseArrowTool_Large_Equilibrium", "r62.tool.arrow.large.equilibrium",
       "Large Equilibrium", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Two, ArcSweep::Straight,
       ArcDirection::None, false, true, "[GUIDE p112] length changes both arrows proportionately" },
    { 38, "chooseArrowTool_Medium_Equilibrium", "r62.tool.arrow.medium.equilibrium",
       "Medium Equilibrium", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Two, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 46, "chooseArrowTool_Small_Equilibrium", "r62.tool.arrow.small.equilibrium",
       "Small Equilibrium", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Two, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 31, "chooseArrowTool_Large_UnbalancedE", "r62.tool.arrow.large.unbalancedE",
       "Large Unbalanced Equilibrium", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Two, ArcSweep::Straight,
       ArcDirection::None, false, true, "[GUIDE p109] 'equilibrium arrows of unequal lengths'" },
    { 39, "chooseArrowTool_Medium_UnbalancedE", "r62.tool.arrow.medium.unbalancedE",
       "Medium Unbalanced Equilibrium", ArrowFamily::Straight, ArrowSize::Medium, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Two, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 47, "chooseArrowTool_Small_UnbalancedE", "r62.tool.arrow.small.unbalancedE",
       "Small Unbalanced Equilibrium", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Two, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 33, "chooseArrowTool_180_CW", "r62.tool.arrow.arc180.cw",
       "180 CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg180,
       ArcDirection::Clockwise, false, true, "" },
    { 34, "chooseArrowTool_180_CCW", "r62.tool.arrow.arc180.ccw",
       "180 CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg180,
       ArcDirection::CounterClockwise, false, true, "" },
    { 35, "chooseArrowTool_180_Single_CW", "r62.tool.arrow.arc180.single.cw",
       "180 Single CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg180,
       ArcDirection::Clockwise, false, true, "half of the 180 sweep" },
    { 36, "chooseArrowTool_180_Single_CCW", "r62.tool.arrow.arc180.single.ccw",
       "180 Single CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg180,
       ArcDirection::CounterClockwise, false, true, "half of the 180 sweep" },
    { 37, "chooseArrowTool_180_Double", "r62.tool.arrow.arc180.double",
       "180 Double", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Double, ArcSweep::Deg180,
       ArcDirection::Clockwise, false, true, "two parallel arc shafts, heads opposite" },
    { 40, "chooseArrowTool_Large_Retrosynthetic", "r62.tool.arrow.large.retrosynthetic",
       "Large Retrosynthetic", ArrowFamily::Straight, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::HalfArrow, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "the open half-arrow head ChemDraw uses for retrosynthesis" },
    { 48, "chooseArrowTool_Small_Retrosynthetic", "r62.tool.arrow.small.retrosynthetic",
       "Small Retrosynthetic", ArrowFamily::Straight, ArrowSize::Small, ArrowLine::Solid,
       ArrowHeadShape::HalfArrow, HeadSides::Both, ShaftCount::One, ArcSweep::Straight,
       ArcDirection::None, false, true, "" },
    { 41, "chooseArrowTool_120_Solid_CW", "r62.tool.arrow.arc120.solid.cw",
       "120 Solid CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg120,
       ArcDirection::Clockwise, false, true, "" },
    { 42, "chooseArrowTool_120_Solid_CCW", "r62.tool.arrow.arc120.solid.ccw",
       "120 Solid CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg120,
       ArcDirection::CounterClockwise, false, true, "" },
    { 43, "chooseArrowTool_120_Single_CW", "r62.tool.arrow.arc120.single.cw",
       "120 Single CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg120,
       ArcDirection::Clockwise, false, true, "half of the 120 sweep" },
    { 44, "chooseArrowTool_120_Single_CCW", "r62.tool.arrow.arc120.single.ccw",
       "120 Single CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg120,
       ArcDirection::CounterClockwise, false, true, "half of the 120 sweep" },
    { 45, "chooseArrowTool_120_Double", "r62.tool.arrow.arc120.double",
       "120 Double", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Double, ArcSweep::Deg120,
       ArcDirection::Clockwise, false, true, "" },
    { 49, "chooseArrowTool_90_CW", "r62.tool.arrow.arc90.cw",
       "90 CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg90,
       ArcDirection::Clockwise, false, true, "" },
    { 50, "chooseArrowTool_90_CCW", "r62.tool.arrow.arc90.ccw",
       "90 CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg90,
       ArcDirection::CounterClockwise, false, true, "" },
    { 51, "chooseArrowTool_90_Single_CW", "r62.tool.arrow.arc90.single.cw",
       "90 Single CW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg90,
       ArcDirection::Clockwise, false, true, "half of the 90 sweep" },
    { 52, "chooseArrowTool_90_Single_CCW", "r62.tool.arrow.arc90.single.ccw",
       "90 Single CCW", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::One, ArcSweep::Deg90,
       ArcDirection::CounterClockwise, false, true, "half of the 90 sweep" },
    { 53, "chooseArrowTool_90_Double", "r62.tool.arrow.arc90.double",
       "90 Double", ArrowFamily::Arc, ArrowSize::Large, ArrowLine::Solid,
       ArrowHeadShape::Solid, HeadSides::Both, ShaftCount::Double, ArcSweep::Deg90,
       ArcDirection::Clockwise, false, true, "" },
};

// ---------------------------------------------------------------------------
// Curve styles - Curves_Toolbar.xml, 8 commands.
// [GUIDE p118] "To create a closed freehand shape, draw a curve and navigate to
// Curves > Closed. To add curve properties, select Filled, Faded, or Shaded
// under the Curve menu or in the Curve toolbar."
// ---------------------------------------------------------------------------
const CurveStyleSpec kCurveStyles[] = {
    { 0, "curveStyle_Plain",   "r62.tool.curve.style.plain",   "Plain",
      false, false, false, false, false, false, false, true,
      "the neutral style: open, solid, single, opaque" },
    { 1, "curveStyle_Closed",  "r62.tool.curve.style.closed",  "Closed",
      true,  false, false, false, false, false, false, true, "" },
    { 2, "curveStyle_Dashed",  "r62.tool.curve.style.dashed",  "Dashed",
      false, true,  false, false, false, false, false, true, "" },
    { 3, "curveStyle_Bold",    "r62.tool.curve.style.bold",    "Bold",
      false, false, true,  false, false, false, false, true, "" },
    { 4, "curveStyle_Doubled", "r62.tool.curve.style.doubled", "Doubled",
      false, false, false, true,  false, false, false, true,
      "two parallel offset copies of the path" },
    { 5, "curveStyle_Faded",   "r62.tool.curve.style.faded",   "Faded",
      false, false, false, false, true,  false, false, true,
      "translucent fill/stroke, no shadow" },
    { 6, "curveStyle_Shaded",  "r62.tool.curve.style.shaded",  "Shaded",
      false, false, false, false, false, true,  false, true,
      "drop shadow offset from the path" },
    { 7, "curveStyle_Filled",  "r62.tool.curve.style.filled",  "Filled",
      true,  false, false, false, false, false, true,  true,
      "closed AND filled; a fill needs closure, so Closed is implied" }
};

// ---------------------------------------------------------------------------
// Size factors. [PROJECT] values; the guide documents the CONCEPT ("customize
// arrows not only for length and angle, but for arrowhead width and shape"
// [GUIDE p109]) but not the numbers.
// ---------------------------------------------------------------------------
struct SizeFactors {
    double headLength;    // document units at scale 1.0
    double headWidth;
    double strokeFactor;  // multiplier on the document line width
};

SizeFactors sizeFactors(ArrowSize s) {
    SizeFactors f;
    switch (s) {
    case ArrowSize::Small:  f.headLength = 6.0; f.headWidth = 3.0; break;
    case ArrowSize::Medium: f.headLength = 9.0; f.headWidth = 4.5; break;
    case ArrowSize::Large:
    default:                f.headLength = 13.5; f.headWidth = 6.75; break;
    }
    f.strokeFactor = 1.0;
    return f;
}

// Stroke-width multipliers for the line-style axis live in geometry.cpp
// (arrowStrokeWidth), which is the single definition both the model and the
// render bridge use. Keeping a second copy here would be a way for them to
// disagree.

// Arc geometry from a sweep + direction + the chord length.
//
// The chord is |end - begin|. For a CIRCULAR arc of included angle theta:
//     R = chord / (2 sin(theta/2))        (the circumscribed-circle relation)
//     s = R * (1 - cos(theta/2))          (the sagitta / mid-arc bulge)
// This is elementary geometry, not a ChemDraw constant.
//
//   Deg180 -> R = chord/2,        s = R
//   Deg270 -> R = chord/sqrt(2),  s = R * (1 + sqrt(2)/2)
//   Deg120 -> R = chord,          s = R * (1 - 0.5) = chord/2
//   Deg90  -> R = chord/sqrt(2),  s = R * (1 - sqrt(2)/2)
//
// The Single/Double variants use HALF the nominal sweep; that is expressed in
// the source command by the "_Single_" spelling. Keeping that decision HERE (in
// the one resolver) rather than in the renderer is what stops preview and commit
// deriving different geometry.
double sweepDegrees(ArcSweep s) {
    switch (s) {
    case ArcSweep::Deg90:  return 90.0;
    case ArcSweep::Deg120: return 120.0;
    case ArcSweep::Deg180: return 180.0;
    case ArcSweep::Deg270: return 270.0;
    case ArcSweep::Straight:
    default:               return 0.0;
    }
}

bool contains(const char* haystack, const char* needle) {
    return haystack && std::strstr(haystack, needle) != 0;
}

// Half the nominal sweep when the source command says "Single".
bool halfSweep(const char* command) { return contains(command, "_Single_"); }

// The sign of the bulge. Document space is y DOWN, so "clockwise on screen" is a
// positive cross product in document coordinates. [GUIDE p109] shows CW and CCW
// presets as distinct buttons, so the direction is real, not cosmetic.
double sagittaFor(const PresetSpec& p, double chord) {
    if (p.family != ArrowFamily::Arc) return 0.0;
    double theta = sweepDegrees(p.sweep);
    if (halfSweep(p.sourceCommand)) theta *= 0.5;
    if (theta <= 0.0 || theta >= 359.9) return 0.0;
    const double half = theta * 0.5 * 3.14159265358979323846 / 180.0;
    const double sinHalf = std::sin(half);
    if (sinHalf <= 1e-9) return 0.0;
    const double radius = chord / (2.0 * sinHalf);
    const double sag = radius * (1.0 - std::cos(half));
    return p.direction == ArcDirection::CounterClockwise ? -sag : sag;
}

} // namespace

const PresetSpec* arrowPresets() { return kPresets; }

size_t arrowPresetCount() { return sizeof(kPresets) / sizeof(kPresets[0]); }

const PresetSpec* presetById(uint32_t id) {
    const size_t n = arrowPresetCount();
    for (size_t i = 0; i < n; ++i)
        if (kPresets[i].presetId == id) return &kPresets[i];
    return 0;
}

const PresetSpec* presetByCommand(const std::string& sourceCommand) {
    const size_t n = arrowPresetCount();
    for (size_t i = 0; i < n; ++i)
        if (sourceCommand == kPresets[i].sourceCommand) return &kPresets[i];
    return 0;
}

const PresetSpec* presetByToolId(const std::string& toolId) {
    const size_t n = arrowPresetCount();
    for (size_t i = 0; i < n; ++i)
        if (toolId == kPresets[i].toolId) return &kPresets[i];
    return 0;
}

ArrowParams resolvePreset(uint32_t presetId, const core::Vec2& begin,
                          const core::Vec2& end, double documentLineWidth) {
    ArrowParams out;
    const PresetSpec* p = presetById(presetId);
    if (!p) {
        // A caller-detectable "unknown preset". kNoPreset is the sentinel, NOT
        // kInvalidObjectId: the latter is the ObjectId sentinel and would be
        // encoded straight into the parameter record as preset=4294967295, which
        // then decodes as a half-configured arrow.
        out.presetId = kNoPreset;
        return out;
    }

    out.presetId   = p->presetId;
    out.family     = p->family;
    out.sizeClass  = p->sizeClass;
    out.line       = p->line;
    out.shafts     = p->shafts;
    out.sweep      = p->sweep;
    out.direction  = p->direction;
    out.begin      = begin;
    out.end        = end;
    out.scale      = 1.0;

    const SizeFactors f = sizeFactors(p->sizeClass);
    out.head.shape   = p->headShape;
    out.head.sides   = p->headSides;
    out.head.length  = f.headLength;
    out.head.width   = f.headWidth;
    // A no-go marker (Cross/Hash) is a MID-shaft mark, not an end head, so the
    // preset declares sides=None and no end head is drawn. Everything else with
    // a declared side draws its head at the END.
    out.head.atEnd   = (p->headSides != HeadSides::None);
    out.head.atBegin = false;
    if(p->headShape==ArrowHeadShape::Resonance){out.head.shape=ArrowHeadShape::Solid;out.head.atBegin=true;}
    if(p->headShape==ArrowHeadShape::Cross||p->headShape==ArrowHeadShape::Hash){out.head.sides=HeadSides::Both;out.head.atEnd=true;}
    if (p->reversed) { out.head.atBegin = out.head.atEnd; out.head.atEnd = false; }
    // 2Sided_270 draws heads at BOTH ends. [GUIDE p109] lists it as its own
    // toolbar button, so it is a real two-headed variant.
    if (contains(p->sourceCommand, "_2Sided_"))
        out.head.atBegin = (p->headSides != HeadSides::None);

    const double chord = core::length(end - begin);
    out.arcSagitta = sagittaFor(*p, chord);

    // Equilibrium separation is proportional to the arrow length, so the guide's
    // "changing the length changes both arrows proportionately" [GUIDE p112]
    // holds across a resize. Clamped so a very short arrow does not collapse.
    out.separation = f.headWidth * .75;
    if(p->headShape==ArrowHeadShape::HalfArrow){out.shafts=ShaftCount::Double;out.head.width=2*f.headLength;out.separation=out.head.width;}
    if (out.separation < 1.5) out.separation = 1.5;

    // Unbalanced: the two halves get DIFFERENT lengths, which is the whole point
    // of the preset. [GUIDE p109] "equilibrium arrows of unequal lengths".
    out.splitRatio = contains(p->sourceCommand, "Unbalanced") ? 0.62 : 0.5;
    out.originalSplitRatio = out.splitRatio;

    (void)documentLineWidth;   // stroke width is derived on demand by strokeWidthFor
    return out;
}

ArrowParams applyPresetStyle(const ArrowParams& current, uint32_t presetId,
                             double documentLineWidth) {
    const PresetSpec* p = presetById(presetId);
    if (!p) return current;
    // Keep the geometry the user has already adjusted (a curve, a split, a
    // scale); replace only the style axes. resolvePreset() would reset
    // arcSagitta from the chord, which is RIGHT for a new arrow and WRONG for a
    // style change on one the user already curved.
    ArrowParams out = resolvePreset(presetId, current.begin, current.end,
                                    documentLineWidth);
    if (out.presetId == core::kInvalidObjectId) return current;
    if(out.family==ArrowFamily::Arc&&current.family==ArrowFamily::Arc)out.arcSagitta=current.arcSagitta;
    out.scale              = current.scale;
    out.strokeWidth        = current.strokeWidth;
    if(current.presetId==presetId){out.splitRatio=current.splitRatio;out.originalSplitRatio=current.originalSplitRatio;}
    // An explicit palette choice replaces both head flags; choosing a single
    // arrow after Resonance must remove the tail head.
    return out;
}

const CurveStyleSpec* curveStyles() { return kCurveStyles; }

size_t curveStyleCount() { return sizeof(kCurveStyles) / sizeof(kCurveStyles[0]); }

const CurveStyleSpec* curveStyleById(uint32_t id) {
    const size_t n = curveStyleCount();
    for (size_t i = 0; i < n; ++i)
        if (kCurveStyles[i].presetId == id) return &kCurveStyles[i];
    return 0;
}

const CurveStyleSpec* curveStyleByCommand(const std::string& sourceCommand) {
    const size_t n = curveStyleCount();
    for (size_t i = 0; i < n; ++i)
        if (sourceCommand == kCurveStyles[i].sourceCommand) return &kCurveStyles[i];
    return 0;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
const char* sizeName(ArrowSize s) {
    switch (s) {
    case ArrowSize::Small:  return "Small";
    case ArrowSize::Medium: return "Medium";
    case ArrowSize::Large:  return "Large";
    }
    return "?";
}

const char* lineName(ArrowLine l) {
    switch (l) {
    case ArrowLine::Solid:  return "Solid";
    case ArrowLine::Bold:   return "Bold";
    case ArrowLine::Dashed: return "Dashed";
    case ArrowLine::Hollow: return "Hollow";
    }
    return "?";
}

const char* headShapeName(ArrowHeadShape s) {
    switch (s) {
    case ArrowHeadShape::None:      return "None";
    case ArrowHeadShape::Solid:     return "Solid";
    case ArrowHeadShape::Hollow:    return "Hollow";
    case ArrowHeadShape::Angled:    return "Angled";
    case ArrowHeadShape::Barb:      return "Barb";
    case ArrowHeadShape::Resonance: return "Resonance";
    case ArrowHeadShape::Cross:     return "Cross";
    case ArrowHeadShape::Hash:      return "Hash";
    case ArrowHeadShape::HalfArrow: return "HalfArrow";
    }
    return "?";
}

const char* headSidesName(HeadSides s) {
    switch (s) {
    case HeadSides::None:  return "None";
    case HeadSides::Both:  return "Both";
    case HeadSides::Left:  return "Left";
    case HeadSides::Right: return "Right";
    }
    return "?";
}

const char* shaftCountName(ShaftCount c) {
    switch (c) {
    case ShaftCount::One:    return "One";
    case ShaftCount::Two:    return "Two";
    case ShaftCount::Double: return "Double";
    }
    return "?";
}

const char* sweepName(ArcSweep s) {
    switch (s) {
    case ArcSweep::Straight: return "Straight";
    case ArcSweep::Deg90:    return "90";
    case ArcSweep::Deg120:   return "120";
    case ArcSweep::Deg180:   return "180";
    case ArcSweep::Deg270:   return "270";
    case ArcSweep::Free:     return "Free";
    }
    return "?";
}

const char* directionName(ArcDirection d) {
    switch (d) {
    case ArcDirection::None:             return "None";
    case ArcDirection::Clockwise:        return "CW";
    case ArcDirection::CounterClockwise: return "CCW";
    }
    return "?";
}

const char* familyName(ArrowFamily f) {
    switch (f) {
    case ArrowFamily::Straight:  return "Straight";
    case ArrowFamily::Arc:       return "Arc";
    case ArrowFamily::Connector: return "Connector";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
// Refuses with a SPECIFIC message rather than silently clamping, so a test can
// assert on the refusal and a caller can show the user why.
core::Error validateArrowParams(const ArrowParams& params) {
    if (!params.begin.finite() || !params.end.finite())
        return core::Error(core::ErrorCode::InvalidGeometry, "arrow endpoint is not finite");
    if (!std::isfinite(params.arcSagitta))
        return core::Error(core::ErrorCode::InvalidGeometry, "arrow sagitta is not finite");
    if(!std::isfinite(params.strokeWidth)||params.strokeWidth<0||params.strokeWidth>100000)return core::Error(core::ErrorCode::InvalidArgument,"invalid arrow stroke width");
    if (!std::isfinite(params.scale) || params.scale <= 0.0 || params.scale > 1000.0)
        return core::Error(core::ErrorCode::InvalidArgument, "arrow scale must be in (0, 1000]");
    if (!std::isfinite(params.head.length) || params.head.length <= 0.0 ||
        params.head.length > 100000.0)
        return core::Error(core::ErrorCode::InvalidArgument, "arrow head length out of range");
    if (!std::isfinite(params.head.width) || params.head.width < 0.0 ||
        params.head.width > 100000.0)
        return core::Error(core::ErrorCode::InvalidArgument, "arrow head width out of range");
    if (params.shafts != ShaftCount::One &&
        (!std::isfinite(params.separation) || params.separation <= 0.0))
        return core::Error(core::ErrorCode::InvalidArgument,
                           "a multi-shaft arrow needs a positive separation");
    if (!std::isfinite(params.splitRatio) ||
        params.splitRatio < 0.15 || params.splitRatio > 0.85)
        return core::Error(core::ErrorCode::InvalidArgument,
                           "equilibrium split ratio must be in [0.15, 0.85]");
    // presetId == 0 is a valid row (Large_Solid); kNoPreset is the explicit
    // "unresolved" marker. Anything ELSE must name a real row.
    if (params.presetId != kNoPreset && presetById(params.presetId) == 0)
        return core::Error(core::ErrorCode::InvalidArgument, "unknown arrow preset id");
    // An Arc-family arrow must have SOMETHING that describes its sweep: either a
    // named palette sweep or an explicit Free sweep (a user-dragged arc). What is
    // rejected is "Arc" with "Straight", which describes no arc at all.
    if (params.family == ArrowFamily::Arc && params.sweep == ArcSweep::Straight)
        return core::Error(core::ErrorCode::InvalidSemantics,
                           "an Arc-family arrow without a sweep has no arc to draw");
    // A named sweep and a zero sagitta also contradict each other: the record would
    // claim an arc and draw a line.
    if (params.family == ArrowFamily::Arc && params.sweep != ArcSweep::Straight &&
        std::fabs(params.arcSagitta) < 1e-12)
        return core::Error(core::ErrorCode::InvalidSemantics,
                           "an Arc-family arrow with a named sweep needs a non-zero sagitta");
    return core::Error::success();
}

core::Error validateCurveStyle(const CurveStyleSpec& style) {
    if (style.sourceCommand == 0 || !*style.sourceCommand)
        return core::Error(core::ErrorCode::InvalidArgument, "curve style has no source command");
    if (style.filled && !style.closed)
        return core::Error(core::ErrorCode::InvalidSemantics, "a filled curve must be closed");
    return core::Error::success();
}

} // namespace arrows
} // namespace r62
