// =============================================================================
// re_round62_viewer/next/bonds/spec.cpp
//
// T07 - the 21 source commands mapped onto explicit axis bundles.
//
// The list below is the chemical-bond category of
// host_integration/drawing_inventory/tools.csv, in FILE ORDER. Command, toolbar
// and category strings are reproduced exactly so a coverage script can join on
// them without a lookup table of its own.
// =============================================================================

#include "next/bonds/spec.hpp"

#include <cstddef>

namespace r62 {
namespace bonds {

const char* evidenceName(EvidenceSource s) {
    switch (s) {
        case EvidenceSource::Uncited:      return "uncited";
        case EvidenceSource::Toolbar:      return "toolbar";
        case EvidenceSource::Guide:        return "guide";
        case EvidenceSource::ToolbarGuide: return "toolbar+guide";
        case EvidenceSource::Project:      return "project";
    }
    return "?";
}

const char* bondToolKindName(BondToolKind k) {
    switch (k) {
        case BondToolKind::DrawBond:      return "draw-bond";
        case BondToolKind::ClickExisting: return "click-existing";
        case BondToolKind::PlaceOnly:     return "place-only";
    }
    return "?";
}

const char* doubleSideName(DoubleSide s) {
    switch (s) {
        case DoubleSide::Symmetric: return "symmetric";
        case DoubleSide::Left:      return "left";
        case DoubleSide::Right:     return "right";
        case DoubleSide::Centered:  return "centered";
    }
    return "?";
}

const char* bondShapeKindName(BondShapeKind k) {
    switch (k) {
        case BondShapeKind::Stroke:        return "stroke";
        case BondShapeKind::FilledPolygon: return "filled-polygon";
        case BondShapeKind::Polyline:      return "polyline";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Axis legality
// ---------------------------------------------------------------------------
// Mirrors Document::setBondOrder, which refuses Aromatic order unless the
// semantics axis says Aromatic or Query. Duplicating it here means a wrong
// preset is caught at registration, not at the first click.
core::Error validateAxes(const BondAxes& axes) {
    if (!core::validBondOrderValue(axes.order))
        return core::Error(core::ErrorCode::InvalidSemantics,
                           "bond order value out of range");
    if (axes.order == core::BondOrder::Aromatic &&
        axes.semantics != core::BondSemantics::Aromatic &&
        axes.semantics != core::BondSemantics::Query)
        return core::Error(core::ErrorCode::InvalidSemantics,
                           "aromatic order requires aromatic or query semantics");
    if (axes.display == core::BondDisplay::Wedge && axes.stereo == core::BondStereo::Down)
        return core::Error(core::ErrorCode::InvalidSemantics,
                           "a solid wedge cannot be a down stereo bond");
    if (axes.display == core::BondDisplay::Hash && axes.stereo == core::BondStereo::Up)
        return core::Error(core::ErrorCode::InvalidSemantics,
                           "a hashed wedge cannot be an up stereo bond");
    return core::Error::success();
}

namespace {

BondAxes axesOf(core::BondOrder o, core::BondDisplay d, core::BondSemantics s,
                core::BondStereo st, DoubleSide side = DoubleSide::Symmetric) {
    BondAxes a;
    a.order = o; a.display = d; a.semantics = s; a.stereo = st; a.side = side;
    return a;
}

// Row helper: makes a missing field a compile error rather than a silently
// zero-initialised one.
BondPreset row(const char* cmd, const char* toolbar, const char* id,
               const char* name, BondToolKind kind, const BondAxes& axes,
               bool implemented, EvidenceSource ev, const char* note) {
    BondPreset p;
    p.presetId = 0;
    p.id = id;
    p.command = cmd;
    p.toolbar = toolbar;
    p.displayName = name;
    p.kind = kind;
    p.axes = axes;
    p.implemented = implemented;
    p.evidence = ev;
    p.note = note;
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// THE 21 COMMANDS
// ---------------------------------------------------------------------------
//   order     - multiplicity / query multiplicity (core::BondOrder)
//   display   - how it is drawn (core::BondDisplay)
//   semantics - what it MEANS chemically (core::BondSemantics)
//   stereo    - direction (core::BondStereo), independent of display
//   side      - double-bond side placement (our PROJECT policy)
//
// "implemented = true" means geometry, edit command, undo and persistence were
// all exercised by tests/T07. It does NOT mean native ChemDraw parity.
// ---------------------------------------------------------------------------
const std::vector<BondPreset>& bondPresets() {
    static std::vector<BondPreset> table;
    if (!table.empty()) return table;

    const char* MAIN = "MainToolBarID";
    const char* MULT = "MultipleBondsToolBarID";
    const char* DD   = "DottedDashedToolBarID";

    table.push_back(row("chooseBondTool_Solid", MAIN,
        "r62.tool.bond.solid", "Solid Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Normal,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Order1 at LineWidth. Also the click-to-retype tool (guide 2448)."));

    table.push_back(row("chooseMultipleBondsTool_DoubleBond", MULT,
        "r62.tool.bond.double", "Double Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Double, core::BondDisplay::Normal,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Symmetric side by default; side is a separate axis (BONDDOUBLEPOSITION)."));

    table.push_back(row("chooseMultipleBondsTool_BoldDoubleBond", MULT,
        "r62.tool.bond.double.bold", "Bold Double Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Double, core::BondDisplay::Bold,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Order2 drawn at BoldWidth; guide 1117: BoldWidth must exceed LineWidth."));

    table.push_back(row("chooseMultipleBondsTool_AnyBond", MULT,
        "r62.tool.bond.any", "Any Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Any, core::BondDisplay::Normal,
               core::BondSemantics::Query, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Query: one plain line. Multiplicity deliberately left unspecified."));

    table.push_back(row("chooseMultipleBondsTool_DashedDoubleBond", MULT,
        "r62.tool.bond.double.dashed", "Dashed Double Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Double, core::BondDisplay::Dashed,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Order2 with a dashed stroke; the dash pattern itself is a PROJECT choice."));

    table.push_back(row("chooseMultipleBondsTool_DoubleDashedDoubleBond", MULT,
        "r62.tool.bond.double.doubledashed", "Double Dashed Double Bond",
        BondToolKind::DrawBond,
        axesOf(core::BondOrder::Double, core::BondDisplay::Dashed,
               core::BondSemantics::Unsaturated, core::BondStereo::None),
        true, EvidenceSource::Toolbar,
        "Both lines dashed; Unsaturated records that multiplicity is drawn, not asserted."));

    table.push_back(row("chooseMultipleBondsTool_SingleOrDoubleBond", MULT,
        "r62.tool.bond.singleordouble", "Single or Double Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::SingleOrDouble, core::BondDisplay::Normal,
               core::BondSemantics::Query, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Query order 6, never order==4. Two lines express the ambiguity."));

    table.push_back(row("chooseMultipleBondsTool_DoubleEitherBond", MULT,
        "r62.tool.bond.doubleeither", "Double Either Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Double, core::BondDisplay::Normal,
               core::BondSemantics::Normal, core::BondStereo::Either),
        true, EvidenceSource::ToolbarGuide,
        "Crossed double bond: stereo=Either (guide 2456-2457)."));

    table.push_back(row("chooseMultipleBondsTool_DativeBond", MULT,
        "r62.tool.bond.dative", "Dative Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Normal,
               core::BondSemantics::Dative, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Donor=begin, acceptor=end with the arrowhead (guide 2484-2485)."));

    table.push_back(row("chooseMultipleBondsTool_SingleOrAromaticBond", MULT,
        "r62.tool.bond.singleoraromatic", "Single or Aromatic Bond",
        BondToolKind::DrawBond,
        axesOf(core::BondOrder::SingleOrAromatic, core::BondDisplay::Normal,
               core::BondSemantics::Query, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Query order 7; the aromatic meaning stays on the semantics axis."));

    table.push_back(row("chooseMultipleBondsTool_TripleBond", MULT,
        "r62.tool.bond.triple", "Triple Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Triple, core::BondDisplay::Normal,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Axis line plus a symmetric pair at the GUIDE BondSpacing."));

    table.push_back(row("chooseMultipleBondsTool_QuadrupleBond", MULT,
        "r62.tool.bond.quadruple", "Quadruple Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Quadruple, core::BondDisplay::Normal,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Four parallel lines. Order==4 is REAL here and is not aromatic."));

    table.push_back(row("chooseMultipleBondsTool_DoubleOrAromaticBond", MULT,
        "r62.tool.bond.doubleoraromatic", "Double or Aromatic Bond",
        BondToolKind::DrawBond,
        axesOf(core::BondOrder::DoubleOrAromatic, core::BondDisplay::Normal,
               core::BondSemantics::Query, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Query order 8. Never encoded as a numeric order."));

    table.push_back(row("chooseBondTool_DottedHydrogen", DD,
        "r62.tool.bond.hydrogen", "Hydrogen Bond (dotted)", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Dotted,
               core::BondSemantics::Hydrogen, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Must start at an explicit H (guide 2509); retargeting to hydrogen is refused (guide 2520)."));

    table.push_back(row("chooseBondTool_Dashed", DD,
        "r62.tool.bond.dashed", "Dashed Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Dashed,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Order1 with a dashed stroke. HashSpacing is not the dash pattern."));

    table.push_back(row("chooseBondTool_Hashed", MAIN,
        "r62.tool.bond.hash", "Hashed Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Hash,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Uniform CDXML Hash; the stereochemical tapered variant is WedgedHashBegin."));

    table.push_back(row("chooseBondTool_Hashed_Wedged", MAIN,
        "r62.tool.bond.hashedwedge", "Hashed Wedged Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::WedgeHash,
               core::BondSemantics::Normal, core::BondStereo::Down),
        true, EvidenceSource::ToolbarGuide,
        "Hashed wedge variant: distinct display value, same orientation rule."));

    table.push_back(row("chooseBondTool_Bold", MAIN,
        "r62.tool.bond.bold", "Bold Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Bold,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Drawn at the GUIDE BoldWidth (2pt default, guide 1117-1118)."));

    table.push_back(row("chooseBondTool_Wedged", MAIN,
        "r62.tool.bond.wedge", "Wedged Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Wedge,
               core::BondSemantics::Normal, core::BondStereo::Up),
        true, EvidenceSource::ToolbarGuide,
        "Up stereo. Narrow end at begin (guide 2498); wide end = 1.5*BoldWidth."));

    table.push_back(row("chooseBondTool_Hollow_Wedged", MAIN,
        "r62.tool.bond.hollowwedge", "Hollow Wedged Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::HollowWedge,
               core::BondSemantics::Normal, core::BondStereo::Up),
        true, EvidenceSource::ToolbarGuide,
        "Outline-only wedge. Stereo stays on the stereo axis, not in the fill."));

    table.push_back(row("chooseBondTool_Wavy", MAIN,
        "r62.tool.bond.wavy", "Wavy Bond", BondToolKind::DrawBond,
        axesOf(core::BondOrder::Single, core::BondDisplay::Wavy,
               core::BondSemantics::Normal, core::BondStereo::None),
        true, EvidenceSource::ToolbarGuide,
        "Guide 2459-2461: repeated clicks make it double, then double-either."));

    for (size_t i = 0; i < table.size(); ++i)
        table[i].presetId = static_cast<uint32_t>(i);
    return table;
}

const BondPreset* presetForCommand(const std::string& command) {
    const std::vector<BondPreset>& all = bondPresets();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].command == command) return &all[i];
    return 0;
}

const BondPreset* presetForId(const std::string& id) {
    const std::vector<BondPreset>& all = bondPresets();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].id == id) return &all[i];
    return 0;
}

std::vector<std::string> bondCommandNames() {
    const std::vector<BondPreset>& all = bondPresets();
    std::vector<std::string> names;
    names.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i) names.push_back(all[i].command);
    return names;
}

} // namespace bonds
} // namespace r62
