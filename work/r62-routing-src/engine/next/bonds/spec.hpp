#ifndef R62_NEXT_BONDS_SPEC_HPP
#define R62_NEXT_BONDS_SPEC_HPP
// =============================================================================
// re_round62_viewer/next/bonds/spec.hpp
//
// T07 - BOND TOOL AND STYLE SPECIFICATION (the "21 commands are parameters,
//       not 21 algorithms" layer).
//
// DEPENDS ON (read only, never redefined here):
//   next/core/contracts.hpp    Vec2, BondOrder, BondDisplay, BondSemantics,
//                              BondStereo, LineStyle, ObjectRef
//   next/core/commands.hpp     Command, CommandContext, InvalidationFlag
//   next/core/document.hpp     Document (read via the command context)
//
// THIS HEADER CONTAINS NO Win32, NO OpenGL AND NO SECOND DOCUMENT MODEL.
//
// -----------------------------------------------------------------------------
// WHY THIS FILE EXISTS
// -----------------------------------------------------------------------------
// ChemDraw's Multiple Bonds / Dotted-Dashed / Main toolbars contain 21 commands
// in the chemical-bond category (host_integration/drawing_inventory/tools.csv,
// verified by DRAWING_FEATURES_2026-10-14.md line 14). Those 21 commands are NOT
// 21 algorithms. They are 21 parameter vectors over FOUR INDEPENDENT AXES that
// the T01 core model already declares:
//
//   BondOrder      how many lines / which query multiplicity
//   BondDisplay    how the bond is DRAWN (wedge, hash, bold, dashed, wavy, ...)
//   BondSemantics  what the bond MEANS chemically (aromatic, dative, hydrogen,
//                  query, unsaturated)
//   BondStereo     stereo direction (Up / Down / Either) independent of display
//
// The task statement forbids collapsing them ("the bond order, the display
// style and the chemical type stay separate") and core/INTERFACES.md section 6
// forbids order==4 meaning aromatic. This file therefore maps each of the 21
// source commands onto an explicit parameter vector, never onto a bare integer.
//
// -----------------------------------------------------------------------------
// VERIFIED GEOMETRY CONSTANTS
// -----------------------------------------------------------------------------
// Source: the bundled ChemDraw User Guide. Page numbers are PDF reader pages;
// the raw extracted text is host_integration/drawing_inventory/guide_text.txt.
//
//   * BondSpacing default = 18 (% of length)   [guide_text.txt:1633]
//     "BondSpacing - Set the distance between the lines in double or triple
//      bonds"                                   [guide_text.txt:1113]
//   * HashSpacing default = 2.5 pt              [guide_text.txt:1631, 1128]
//   * BoldWidth default   = 2 pt                [guide_text.txt:1628]
//     "The end of a wedge is 1.5 times the bold width" [guide_text.txt:1118]
//     -> wide-end half width = 1.5 * BoldWidth / 2 = 1.5 pt at the defaults
//   * LineWidth default   = 0.6 pt              [guide_text.txt:1629]
//   * "Drag from the narrow end of the wedged bond to the wide end" [2498]
//     -> the BEGIN atom is the narrow end. This is a Canvas attribute today
//        (CDXML WedgeHash / "narrow end" convention); we keep begin == narrow.
//   * Dative: "Drag from the positive to the negative end (arrowhead)" [2484],
//     "Dative bonds are drawn with a fixed orientation" [2485], and
//     "Dashed (coordination) bonds are functionally equivalent to dative
//      bonds" [2493].
//   * Double-either = crossed double bond, "unspecified double bond
//     configuration, or a mixture of configurations" [2456-2457].
//   * Hydrogen bond must start from an explicit hydrogen [2509]; and
//     "You cannot change any existing bond to hydrogen bond" [2520].
//   * Double bond side: BONDDOUBLEPOSITION exists as a command [1402]; the
//     side hotkeys are l / c / r [2115-2118]. The OFFSET NUMBERS are our own
//     choice - see DoubleSide.
//
// Anything this file calls PROJECT is a deliberate choice with no documentary
// proof; anything it calls GUIDE is taken from the sources above.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/core/contracts.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace bonds {

// ---------------------------------------------------------------------------
// Version / compatibility handshake
// ---------------------------------------------------------------------------
// Independent of core::kInterfaceVersion and render::kRenderVersion. A caller
// checks all three. This mirrors the rule the other modules already use.
enum {
    kBondsMajor    = 1,
    kBondsMinor    = 0,
    kBondsRevision = 0,
    kBondsVersion  = kBondsMajor * 10000 + kBondsMinor * 100 + kBondsRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestBondsCompatibility(int major, int minor) {
    return major == kBondsMajor && minor <= kBondsMinor;
}

// ---------------------------------------------------------------------------
// Where a fact came from. A coverage report that cannot cite a source is a
// defect, not a footnote.
// ---------------------------------------------------------------------------
enum class EvidenceSource : uint8_t {
    Uncited = 0,   // must never appear in a shipped preset
    Toolbar,       // drawing_inventory/tools.csv row (the command exists)
    Guide,         // bundled ChemDraw User Guide text
    ToolbarGuide,  // both
    Project        // our own choice, clearly labelled
};
const char* evidenceName(EvidenceSource s);

// ---------------------------------------------------------------------------
// Which interaction algorithm a preset uses. The algorithm set is small and
// bounded; "21 commands" is data.
// ---------------------------------------------------------------------------
enum class BondToolKind : uint8_t {
    DrawBond = 0,     // press/drag/release creates or retypes a bond
    ClickExisting,    // click an existing bond to advance its type (guide 2448)
    PlaceOnly         // tool exists but interaction is owned elsewhere
};
const char* bondToolKindName(BondToolKind k);

// ---------------------------------------------------------------------------
// Double bond side placement.
// ---------------------------------------------------------------------------
// GUIDE: BONDDOUBLEPOSITION exists (guide 1402) and l / c / r pick left /
// centre / right (2115-2118). PROJECT: the offsets. ChemDraw default is a
// symmetric double bond at 18% of length; left/right move the SECOND line only,
// which is what the "subsequent clicks reposition the two lines" behaviour
// (guide 2448-2449) looks like.
enum class DoubleSide : uint8_t {
    Symmetric = 0,   // both lines offset from the axis (the default)
    Left = 1,        // second line only, on the -normal side
    Right = 2,       // second line only, on the +normal side
    Centered = 3     // one line on the axis, the other offset
};
const char* doubleSideName(DoubleSide s);

// ---------------------------------------------------------------------------
// Bond axes bundle: what a preset resolves to and what geometry consumes.
// A value type, comparable, so a test can assert an exact bundle.
// ---------------------------------------------------------------------------
struct BondAxes {
    core::BondOrder     order;
    core::BondDisplay   display;
    core::BondSemantics semantics;
    core::BondStereo    stereo;
    DoubleSide          side;       // only meaningful for a double bond

    BondAxes()
        : order(core::BondOrder::Single), display(core::BondDisplay::Normal),
          semantics(core::BondSemantics::Normal), stereo(core::BondStereo::None),
          side(DoubleSide::Symmetric) {}

    bool operator==(const BondAxes& o) const {
        return order == o.order && display == o.display &&
               semantics == o.semantics && stereo == o.stereo && side == o.side;
    }
    bool operator!=(const BondAxes& o) const { return !(*this == o); }

    bool sideMatters() const {
        return order == core::BondOrder::Double ||
               order == core::BondOrder::DoubleOrAromatic;
    }
};

// Is this axis bundle a legal document state? Mirrors (does not replace) the
// rule in Document::setBondOrder: Aromatic order needs Aromatic or Query
// semantics. Returns core::Error so callers use one error vocabulary.
core::Error validateAxes(const BondAxes& axes);

// ---------------------------------------------------------------------------
// One mapped command. `command` is the exact tools.csv command name.
// ---------------------------------------------------------------------------
struct BondPreset {
    uint32_t        presetId;      // dense index assigned at registration
    std::string     id;            // namespaced, e.g. "r62.tool.bond.solid"
    std::string     command;       // tools.csv command, exact spelling
    std::string     toolbar;       // tools.csv toolbar column
    std::string     displayName;   // for menus
    BondToolKind    kind;
    BondAxes        axes;
    bool            implemented;   // false == registered, reports honestly
    EvidenceSource  evidence;
    std::string     note;          // what is deliberately not claimed

    BondPreset() : presetId(0), kind(BondToolKind::DrawBond), implemented(false),
                   evidence(EvidenceSource::Uncited) {}
};

// The 21 presets, in tools.csv order. Static data; no per-call allocation.
const std::vector<BondPreset>& bondPresets();

// Lookup by tools.csv command name ("chooseBondTool_Wedged"). 0 when absent.
const BondPreset* presetForCommand(const std::string& command);
// Lookup by namespaced tool id ("r62.tool.bond.wedged").
const BondPreset* presetForId(const std::string& id);
// The 21 source command names in file order (used by the coverage report).
std::vector<std::string> bondCommandNames();

// ---------------------------------------------------------------------------
// Geometry parameters. GUIDE values are defaults; a caller may override any of
// them and the generator must honour the override.
// ---------------------------------------------------------------------------
struct BondGeometryOptions {
    double spacingPercent;         // GUIDE 18 (% of length)
    double lineWidth;              // GUIDE 0.6 pt
    double boldWidth;              // GUIDE 2.0 pt
    double wedgeEndScale;          // GUIDE 1.5 (wedge end = 1.5 * boldWidth)
    double hashSpacing;            // GUIDE 2.5 pt
    double multipleEndGapPercent;  // PROJECT 5.0 - inner-line shortening
    double wavyAmplitude;          // PROJECT 1.2
    double wavyWavelength;         // PROJECT 0.0 == derive from amplitude
    double dashOn, dashOff;        // PROJECT 3.0 / 2.0
    double dotOn, dotOff;          // PROJECT 0.8 / 1.6
    double dativeHeadPercent;      // PROJECT 22.0 (% of length)
    double dativeHeadAngle;        // PROJECT 22.0 (half-angle, degrees)
    double minDecoratedLength;     // PROJECT 3.0 - below this, shrink
    double labelClearance;         // PROJECT 2.0 - inner-line label clearance

    BondGeometryOptions()
        : spacingPercent(18.0), lineWidth(0.6), boldWidth(2.0), wedgeEndScale(1.5),
          hashSpacing(2.5), multipleEndGapPercent(5.0), wavyAmplitude(1.2),
          wavyWavelength(0.0), dashOn(3.0), dashOff(2.0), dotOn(0.8), dotOff(1.6),
          dativeHeadPercent(22.0), dativeHeadAngle(22.0), minDecoratedLength(3.0),
          labelClearance(2.0) {}
};

// ---------------------------------------------------------------------------
// Render data: what a renderer should draw for one bond.
// ---------------------------------------------------------------------------
// Deliberately NOT render::Primitive: the business module must emit geometry in
// a headless test with no render module linked, and the T03 adapter converts
// this into Primitive records. It is also what lets a test assert the exact
// polygon of a wedge without a GPU.
enum class BondShapeKind : uint8_t {
    Stroke = 0,     // an open polyline to be stroked
    FilledPolygon,  // a closed polygon to be filled (wedge)
    Polyline        // several open polylines (hashes, crossed double bond)
};
const char* bondShapeKindName(BondShapeKind k);

struct BondShape {
    BondShapeKind           kind;
    std::vector<core::Vec2> points;      // Stroke / Polyline points
    std::vector<core::Vec2> outline;     // FilledPolygon contour
    std::vector<core::Vec2> fillHoles;   // FilledPolygon optional hole
    bool                    fill;
    double                  strokeWidth; // 0 == document default
    double                  dashOn, dashOff;   // 0 == solid
    double                  wavyAmplitude;
    double                  wavyWavelength;
    BondShape()
        : kind(BondShapeKind::Stroke), fill(false), strokeWidth(0.0), dashOn(0.0),
          dashOff(0.0), wavyAmplitude(0.0), wavyWavelength(0.0) {}
};

// Everything a renderer needs for one bond. axisBegin/axisEnd are the RESOLVED
// endpoints in begin->end order, so a caller never re-derives them.
struct BondRenderData {
    std::vector<BondShape> shapes;
    core::Vec2             axisBegin;
    core::Vec2             axisEnd;
    bool                   degenerate;
    bool                   clippedByLabel;
    std::string            note;
    BondRenderData() : degenerate(false), clippedByLabel(false) {}
};

} // namespace bonds
} // namespace r62

#endif // R62_NEXT_BONDS_SPEC_HPP
