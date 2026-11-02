#ifndef R62_NEXT_ARROWS_CURVES_PRESET_HPP
#define R62_NEXT_ARROWS_CURVES_PRESET_HPP
// =============================================================================
// re_round62_viewer/next/arrows_curves/preset.hpp
//
// T10 - ARROW PRESET TABLE (the 56 commands of tools.csv) AND CURVE PRESET TABLE.
//
// -----------------------------------------------------------------------------
// THE CENTRAL DESIGN DECISION
// -----------------------------------------------------------------------------
// The Arrow toolbar has 56 buttons. They are NOT 56 algorithms. Measured from
// host_integration/drawing_inventory/tools.csv the 56 command names decompose
// into FOUR independent parameter axes plus a handful of structural families:
//
//   * size          Large | Medium | Small        (3)  -> head+shaft scale
//   * line style    Solid | Bold | Dashed | Hollow(4)  -> stroke appearance
//   * head style    Solid | Bold | Dashed | OneSided_Left | OneSided_Right |
//                   Resonance | Hollow | Retrosynthetic (8)
//   * shaft count   1 | 2 (equilibrium) | Double        (3)  -> parallel shafts
//   * arc sweep     90 | 120 | 180 | 270 + Elliptical   (5)  -> arc arrows
//   * arc direction CW | CCW                            (2)
//
// chooseArrowTool_Large_OneSided_Left is therefore NOT a new drawing routine:
// it is {size=Large, head=Barb, sides=Left}. This file is a DATA TABLE that
// resolves each of the 56 source command strings to such a parameter bundle.
// Adding a 57th preset is a table row, not a function.
//
// -----------------------------------------------------------------------------
// WHAT THE PARAMETERS MEAN (head/shape vocabulary)
// -----------------------------------------------------------------------------
// A "shaft" is a line; a "head" is a decoration at an end. Both are expressed
// with shared parameter types so that render and hit test can never disagree.
//
// HeadSides:
//      Both  - a normal symmetric head (ChemDraw "A) solid arrowhead")
//      Left  - the half of the head on the LEFT of the direction of travel
//      Right - the half of the head on the RIGHT
//      None  - no head at all (a plain line, or a no-go cross slash)
//
// HeadShape:
//      Solid    - filled triangle             (guide: "A) Solid arrowhead")
//      Hollow   - unfilled triangle outline   (guide: "B) Hollow head")
//      Angled   - two strokes, no fill        (guide: "C) Angled head")
//      Barb     - filled concave barb         (ChemDraw's 'bold' look)
//      Resonance- the two short parallel strokes ChemDraw draws instead of a
//                 filled head for a resonance arrow
//      Cross    - the no-go X placed at the middle of the shaft
//      Hash     - the no-go double slash placed at the middle of the shaft
//      HalfArrow- the retrosynthetic "half arrow" head
//      None     - plain line end
//
// -----------------------------------------------------------------------------
// WHY PARAMETERS AND NOT AN ENUM PER PRESET
// -----------------------------------------------------------------------------
// The acceptance criterion says "the style presets are data, not 56 duplicated
// draw functions". A table row is data. The renderer reads ArrowParams; it has
// no switch over preset ids anywhere. presetFamily() only decides WHICH OF THE
// THREE BOUNDED RENDER FAMILIES (Straight / Arc / Connector) a preset belongs
// to - that is a bounded algorithm set, exactly as ToolBehavior is in T01.
//
// -----------------------------------------------------------------------------
// PROVENANCE OF EVERY NUMBER
// -----------------------------------------------------------------------------
// The 56 command strings are copied verbatim from tools.csv (vendored from the
// installed ChemDraw GUI XML, see host_integration/drawing_inventory). The
// parameter VALUES (head length factors, dash pattern, arc angles, double-shaft
// separation) are PROJECT choices unless a comment says otherwise. The guide
// (ChemDraw User Guide, PDF pages 109-112, extracted in
// host_integration/drawing_inventory/guide_text.txt) documents the SEMANTICS
// (three head shapes, arc creation, equilibrium coupling, connection points),
// not the millimetre geometry. Values that the guide does state are marked
// [GUIDE pNNN]; everything else is [PROJECT] and may be tuned without touching
// the renderer.
// =============================================================================

#include "next/core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace r62 {
namespace arrows {

// ---------------------------------------------------------------------------
// Version handshake (mirrors next/core and next/render)
// ---------------------------------------------------------------------------
enum {
    kArrowMajor    = 1,
    kArrowMinor    = 0,
    kArrowRevision = 0,
    kArrowVersion  = kArrowMajor * 10000 + kArrowMinor * 100 + kArrowRevision
};

// The core interface this header was compiled against.
inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestArrowCompatibility(int major, int minor) {
    return major == kArrowMajor && minor <= kArrowMinor;
}

// ---------------------------------------------------------------------------
// The "no preset" sentinel.
// ---------------------------------------------------------------------------
// Deliberately NOT core::kPresetNone and deliberately NOT core::kInvalidObjectId:
//   * core::kPresetNone lives in next/core/tool_registry.hpp, which would drag the
//     registry (and with it commands.hpp) into the pure geometry kernel;
//   * core::kInvalidObjectId is the OBJECT-ID sentinel, and using it as a preset
//     id lets 0xFFFFFFFF be ENCODED into a parameter record as preset=4294967295,
//     which then decodes as a half-configured arrow. That was a real defect caught
//     by the malformed-record checks.
// The preset table's own ids are 0..56, so 0xFFFFFFFE can never collide with one.
static const uint32_t kNoPreset = 0xFFFFFFFEu;

// ---------------------------------------------------------------------------
// Parameter vocabulary
// ---------------------------------------------------------------------------

// Overall size class. Drives head length AND shaft stroke width together, which
// is what makes "Large" vs "Small" a single axis rather than two.
enum class ArrowSize : uint8_t {
    Small = 0,
    Medium = 1,
    Large = 2
};

// Stroke appearance of the shaft. `Hollow` is a separate axis because ChemDraw
// applies hollow/filled/shaded to the whole block (guide p113 "Coloring Block
// Arrows"), so it is not the same thing as a dashed line.
enum class ArrowLine : uint8_t {
    Solid = 0,
    Bold,
    Dashed,
    Hollow
};

enum class ArrowHeadShape : uint8_t {
    None = 0,
    Solid,
    Hollow,
    Angled,
    Barb,
    Resonance,
    Cross,
    Hash,
    HalfArrow
};

enum class HeadSides : uint8_t {
    None = 0,
    Both,
    Left,
    Right
};

// How many parallel shafts the preset draws.
enum class ShaftCount : uint8_t {
    One = 1,
    Two = 2,      // equilibrium / double arrow
    Double = 3    // the "180_Double / 120_Double / 90_Double" family
};

// Arc sweep. Straight arrows have sweep 0. [GUIDE p109] "you can drag an arrow
// from its middle to create an arc of any length" and the palette offers fixed
// 90/120/180/270 arc presets; those four values are the palette's, so they are
// source-documented, not invented.
enum class ArcSweep : uint8_t {
    Straight = 0,
    Deg90,
    Deg120,
    Deg180,
    Deg270,
    // A sweep the USER created by dragging the middle handle [GUIDE p109: "drag an
    // arrow from its middle to create an arc of any length"]. Such an arc has a
    // sagitta but no palette name, so it needs its own value. Without it, curving a
    // straight arrow produced an Arc-family record with sweep==Straight, which the
    // validator then refused - i.e. the documented gesture could not be saved.
    Free
};

enum class ArcDirection : uint8_t {
    None = 0,
    Clockwise,        // CW  - the toolbar spelling
    CounterClockwise  // CCW - the toolbar spelling
};

// The bounded set of RENDER FAMILIES. This is the only "algorithm switch" in the
// module, and it has three members rather than 56.
enum class ArrowFamily : uint8_t {
    Straight = 0,   // a straight shaft (possibly two parallel ones) + heads
    Arc = 1,        // the shaft IS an arc; heads tangent at its ends
    Connector = 2   // a plain line used as a stored link
};

// A single arrow head's parameters. Split from the shaft so "changing the shape
// of the arrowhead changes both heads identically" [GUIDE p112] is one
// assignment.
struct HeadParams {
    ArrowHeadShape shape;
    HeadSides      sides;
    double         length;    // document units, along the shaft direction
    double         width;     // document units, full width across the shaft
    bool           atEnd;     // head at the END point (the arrowhead)
    bool           atBegin;   // head at the BEGIN point (reversed / double-headed)
    HeadParams()
        : shape(ArrowHeadShape::Solid), sides(HeadSides::Both),
          length(3.0), width(2.2), atEnd(true), atBegin(false) {}
};

// ---------------------------------------------------------------------------
// ArrowParams: the resolved parameter bundle for ONE arrow object.
// ---------------------------------------------------------------------------
// This struct is what the renderer, the hit tester and the editor all consume.
// It is a VALUE type and is stored WHOLE on the document object, so render and
// commit can never derive different geometry from the same preset.
struct ArrowParams {
    uint32_t      presetId;     // index into the table below (stable)
    ArrowFamily   family;
    ArrowSize     sizeClass;
    ArrowLine     line;
    ShaftCount    shafts;
    HeadParams    head;         // the head parameter block (shared by both ends)
    ArcSweep      sweep;
    ArcDirection  direction;

    // ---- geometry ----
    core::Vec2    begin;        // document units, the "start" end
    core::Vec2    end;          // document units, the head end
    double        arcSagitta;   // document units; >0 bulges to the LEFT of
                                // begin->end (screen space, y down). Derived from
                                // sweep+direction when the preset supplies an
                                // arc; editable by the user.

    // ---- equilibrium / double shaft ----
    // Two parallel shafts separated by this many document units. When
    // shafts==One this is ignored. [GUIDE p112] changing the length of an
    // equilibrium arrow changes BOTH arrows proportionately; that is enforced by
    // keeping the ratio between separation and total length constant on resize.
    double        separation;
    // The two independent halves of an equilibrium arrow. Each is a fraction of
    // the full length in [0.15, 0.85]; at 0.5 they are equal. Alt-dragging one
    // half (guide p112) changes only its own value.
    double        splitRatio;
    // The head-end half may not be extended past its original length; the guide
    // says so explicitly, so it is stored and checked.
    double        originalSplitRatio;

    double        strokeWidth = 0; // explicit base width; zero keeps preset default
    double        scale;        // user scale multiplier, 1.0 == preset default

    ArrowParams()
        : presetId(0), family(ArrowFamily::Straight), sizeClass(ArrowSize::Large),
          line(ArrowLine::Solid), shafts(ShaftCount::One), sweep(ArcSweep::Straight),
          direction(ArcDirection::None), begin(0, 0), end(24, 0),
          arcSagitta(0.0), separation(3.0), splitRatio(0.5),
          originalSplitRatio(0.5), scale(1.0) {}
};

// ---------------------------------------------------------------------------
// PresetSpec: one row of the table.
// ---------------------------------------------------------------------------
// `sourceCommand` is the EXACT string from tools.csv. `implemented` is honest:
// false means the row is enumerable and selectable but the renderer reports
// NotImplemented rather than drawing something wrong.
struct PresetSpec {
    uint32_t       presetId;
    const char*    sourceCommand;   // tools.csv command, never NULL
    const char*    toolId;          // stable namespaced id for the registry
    const char*    displayName;
    ArrowFamily    family;
    ArrowSize      sizeClass;
    ArrowLine      line;
    ArrowHeadShape headShape;
    HeadSides      headSides;
    ShaftCount     shafts;
    ArcSweep       sweep;
    ArcDirection   direction;
    bool           reversed;        // preset draws head at the BEGIN point
    bool           implemented;
    const char*    note;
};

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------
// 56 rows, in the order the toolbar XML lists them (tools.csv preserves that
// order, and the order is what a "third arrow from the left in the top row"
// instruction in the guide's tutorial refers to). Never reorder: presetId is
// persisted.
const PresetSpec* arrowPresets();          // 56 entries
size_t            arrowPresetCount();      // == 56
const PresetSpec* presetById(uint32_t id);
const PresetSpec* presetByCommand(const std::string& sourceCommand);
const PresetSpec* presetByToolId(const std::string& toolId);

// Resolve a preset + a user gesture into a fully specified parameter bundle.
// `begin`/`end` are in document units. `documentLineWidth` supplies the base
// stroke width so a preset and the document agree on "1x".
ArrowParams resolvePreset(uint32_t presetId, const core::Vec2& begin,
                          const core::Vec2& end, double documentLineWidth);

// Same, but starting from an existing ArrowParams (a style change keeps the
// geometry, which is what makes "style change is one transaction" trivial).
ArrowParams applyPresetStyle(const ArrowParams& current, uint32_t presetId,
                             double documentLineWidth);

// ---------------------------------------------------------------------------
// Curve style presets - the Curves_Toolbar.xml 8 commands.
// ---------------------------------------------------------------------------
// Same principle: 8 commands, one path model, four booleans plus a fill mode.
struct CurveStyleSpec {
    uint32_t    presetId;
    const char* sourceCommand;
    const char* toolId;
    const char* displayName;
    bool        closed;
    bool        dashed;
    bool        bold;
    bool        doubled;
    bool        faded;
    bool        shaded;      // drop shadow
    bool        filled;
    bool        implemented;
    const char* note;
};

const CurveStyleSpec* curveStyles();      // 8 entries
size_t                curveStyleCount();  // == 8
const CurveStyleSpec* curveStyleById(uint32_t id);
const CurveStyleSpec* curveStyleByCommand(const std::string& sourceCommand);

// Human readable names, also used by the coverage report.
const char* sizeName(ArrowSize s);
const char* lineName(ArrowLine l);
const char* headShapeName(ArrowHeadShape s);
const char* headSidesName(HeadSides s);
const char* shaftCountName(ShaftCount c);
const char* sweepName(ArcSweep s);
const char* directionName(ArcDirection d);
const char* familyName(ArrowFamily f);

// Validation used before an object is written to the document. Returns an error
// with a specific message rather than silently clamping, so a test can assert on
// the refusal.
core::Error validateArrowParams(const ArrowParams& params);
core::Error validateCurveStyle(const CurveStyleSpec& style);

} // namespace arrows
} // namespace r62

#endif // R62_NEXT_ARROWS_CURVES_PRESET_HPP
