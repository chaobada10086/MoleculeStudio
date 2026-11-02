#ifndef R62_NEXT_GRAPHICS_PRESET_HPP
#define R62_NEXT_GRAPHICS_PRESET_HPP
// =============================================================================
// re_round62_viewer/next/graphics/preset.hpp
//
// T11 - GEOMETRY SHAPE PRESET TABLE (32 commands) AND BRACKET PRESET TABLE (8).
//
// -----------------------------------------------------------------------------
// SOURCE OF THE 40 COMMAND STRINGS
// -----------------------------------------------------------------------------
// tools.csv (host_integration/drawing_inventory/tools.csv, generated from the
// installed ChemDraw GUI XML) lists:
//
//   rows 118..149  几何图形         32 commands  Drawing_Toolbar.xml
//   rows 150..157  括号与匕首标记    8 commands  Brackets_Toolbar.xml
//
// The strings in the tables below are copied VERBATIM from those rows, in the
// order tools.csv lists them (which is the toolbar order).
//
// -----------------------------------------------------------------------------
// THE CENTRAL DESIGN DECISION: 40 COMMANDS, NOT 40 ALGORITHMS
// -----------------------------------------------------------------------------
// The 32 geometry commands decompose along THREE independent axes plus one
// bounded structural axis:
//
//   form        Circle | Oval | Rectangle | RoundRectangle | Line | Arc     (6)
//   appearance  Hollow | Dashed | Shaded | Solid | Shadowed                (5)
//   line        Solid | Dashed | Wavy | Bold                               (4)
//   arc sweep   90 | 120 | 180 | 270                                       (4)
//
// 4 forms x 5 appearances = 20 (circle/oval/rectangle/roundrect)
// 4 line variants          =  4
// 4 sweeps x 2 styles      =  8
//                            ---
//                             32
//
// chooseDrawingTool_Oval_Shadowed is therefore NOT a new drawing routine: it is
// {form=Oval, appearance=Shadowed}. The renderer has no switch over preset ids;
// it reads ShapeParams and draws what the parameters describe.
//
// The 8 bracket commands decompose the same way:
//
//   family  Square | Parenthesis | Curly | Dagger                        (4)
//   paired  true | false                                                 (2)
//   -> 3 pairs + 3 singles + single dagger + double dagger              (8)
//
// -----------------------------------------------------------------------------
// PROVENANCE OF EVERY NUMBER
// -----------------------------------------------------------------------------
//  [SRC]  the exact command string from tools.csv.
//  [PROJ] a project-chosen numeric value (corner radius fraction, arc
//         segmentation, dagger bar length) that may be tuned without touching
//         the renderer, the model or any test that asserts SEMANTICS.
// =============================================================================

#include "next/core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace r62 {
namespace graphics {

// ---------------------------------------------------------------------------
// Version handshake (mirrors next/core, next/render, next/arrows_curves)
// ---------------------------------------------------------------------------
enum {
    kGraphicsMajor    = 1,
    kGraphicsMinor    = 0,
    kGraphicsRevision = 0,
    kGraphicsVersion  = kGraphicsMajor * 10000 + kGraphicsMinor * 100 + kGraphicsRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestGraphicsCompatibility(int major, int minor) {
    return major == kGraphicsMajor && minor <= kGraphicsMinor;
}

// A preset id that can never collide with a row id (rows are 0..39).
static const uint32_t kNoPreset = 0xFFFFFFFEu;

// ---------------------------------------------------------------------------
// Parameter vocabulary
// ---------------------------------------------------------------------------
enum class ShapeForm : uint8_t {
    Circle = 0,
    Oval,
    Rectangle,
    RoundRectangle,
    Line,
    Arc
};

enum class ShapeAppearance : uint8_t {
    Hollow = 0,
    Dashed,
    Shaded,
    Solid,
    Shadowed
};

enum class LineVariant : uint8_t {
    Solid = 0,
    Dashed,
    Wavy,
    Bold
};

enum class ArcSweep : int {
    Deg90 = 90,
    Deg120 = 120,
    Deg180 = 180,
    Deg270 = 270
};

enum class BracketFamily : uint8_t {
    Square = 0,
    Parenthesis,
    Curly,
    Dagger
};

enum class BracketSide : uint8_t {
    Pair = 0,
    Left,
    Right
};

enum class BracketDirection : uint8_t {
    Vertical = 0,
    Horizontal
};

struct BracketLabelAnchor {
    double u;
    double v;
    BracketLabelAnchor() : u(1.0), v(-1.0) {}
    BracketLabelAnchor(double u_, double v_) : u(u_), v(v_) {}
};

// ---------------------------------------------------------------------------
// Preset rows
// ---------------------------------------------------------------------------
struct ShapePresetSpec {
    uint32_t         presetId;
    const char*      sourceCommand;
    const char*      toolId;
    const char*      displayName;
    ShapeForm        form;
    ShapeAppearance  appearance;
    LineVariant      line;
    int              arcSweepDeg;
    bool             implemented;
    const char*      note;
};

struct BracketPresetSpec {
    uint32_t        presetId;
    const char*     sourceCommand;
    const char*     toolId;
    const char*     displayName;
    BracketFamily   family;
    bool            paired;
    bool            implemented;
    const char*     note;
};

const ShapePresetSpec*   shapePresets();
size_t                   shapePresetCount();
const BracketPresetSpec* bracketPresets();
size_t                   bracketPresetCount();

const ShapePresetSpec*   shapePresetById(uint32_t id);
const ShapePresetSpec*   shapePresetByCommand(const std::string& sourceCommand);
const ShapePresetSpec*   shapePresetByToolId(const std::string& toolId);
const BracketPresetSpec* bracketPresetById(uint32_t id);
const BracketPresetSpec* bracketPresetByCommand(const std::string& sourceCommand);
const BracketPresetSpec* bracketPresetByToolId(const std::string& toolId);

const ShapePresetSpec* shapePresetFor(ShapeForm form, ShapeAppearance appearance);
const ShapePresetSpec* shapePresetForLine(LineVariant line);
const ShapePresetSpec* shapePresetForArc(int sweepDeg, LineVariant line);
const BracketPresetSpec* bracketPresetFor(BracketFamily family, bool paired);

const char* shapeFormName(ShapeForm f);
const char* appearanceName(ShapeAppearance a);
const char* lineVariantName(LineVariant l);
const char* bracketFamilyName(BracketFamily f);
const char* bracketSideName(BracketSide s);
const char* bracketDirectionName(BracketDirection d);

bool appearanceFills(ShapeAppearance appearance);
bool appearanceShadowed(ShapeAppearance appearance);
bool appearanceDashed(ShapeAppearance appearance);
void appearanceDashPattern(ShapeAppearance appearance, double lineWidth,
                           double* on, double* off);
uint32_t appearanceFillColor(ShapeAppearance appearance, uint32_t userFillRGBA);

bool presetHasInterior(const ShapePresetSpec& spec);
bool presetSupportsNonUniformScale(const ShapePresetSpec& spec);

} // namespace graphics
} // namespace r62

#endif // R62_NEXT_GRAPHICS_PRESET_HPP
