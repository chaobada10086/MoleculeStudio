#ifndef R62_NEXT_BIODRAW_PRESET_HPP
#define R62_NEXT_BIODRAW_PRESET_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/preset.hpp
//
// T14 - THE 89 BioDraw BIOLOGICAL SHAPE PRESETS.
//
// -----------------------------------------------------------------------------
// SOURCE OF THE 89 COMMAND STRINGS (two independent readings, both recorded)
// -----------------------------------------------------------------------------
//  [SRC-A] host_integration/drawing_inventory/tools.csv -- 89 rows whose
//          category is "BioDraw 生物图示". Copied VERBATIM, in tools.csv order.
//
//  [SRC-B] The installed GUI XML, parsed read-only in this session:
//            C:/ProgramData/RevvitySignalsSoftware/ChemDrawApplications/
//            ChemDraw/GUI/Common/XML/Toolbars/GenericShape_Toolbar.xml
//          It contains 14 <Toolbar> groups and 98 <Button> nodes carrying a
//          <Graphic sharedGraphic="..."> pair. Exactly 89 of those graphics are
//          DISTINCT; the 9 duplicates are:
//            BDC_GolgiBody, GS_Golgi_2, GS_Golgi_3, GS_Golgi_4,
//            BDC_EndoplasmicReticulum, GS_ER_2, GS_ER_3, GS_ER_4, GS_ER_5
//          each appearing twice: once in GenericShapeGolgiToolBarID /
//          GenericShapeERToolBarID, and again inside GenericShapeOrganelleToolBarID.
//
//          98 - 9 = 89  ==  the tools.csv row count.  This is what "do not
//          double-count the old BioDraw toolbar and the Golgi/ER repeated
//          entries" means concretely, and it is CHECKED at runtime by
//          verifyPresetTable() rather than asserted in prose.
//
// The 'graphic' field below is the XML sharedGraphic name, kept because it is
// the only stable per-preset identifier the vendor file offers. It is an
// IDENTIFIER, not an asset: no ChemDraw bitmap or template file is copied,
// embedded or shipped by this module.
//
// -----------------------------------------------------------------------------
// THE CENTRAL DESIGN DECISION: 89 COMMANDS, NOT 89 ALGORITHMS
// -----------------------------------------------------------------------------
// The task statement is explicit that 275 tools are "tool/style options, not 275
// algorithm sets", and that assumption is carried here. Every one of the 89
// presets is described by four INDEPENDENT axes plus a bounded component list:
//
//   family     14 values  Enzyme GProtein Receptor IonChannel Immunoglobin
//                         NuclearPore NucleicAcid Membrane Misc Organelle
//                         Ribosome Microorganism HelixProtein PlasmidMap
//   glyph      a small closed vocabulary of DRAWABLE PRIMITIVES:
//                         Blob Bilayer Vesicle Capsule Rod Spiralling Tube
//                         Pore Barrell BarrellPore YShape Antibody ...
//              (see GlyphKind; 24 values cover all 89 shapes)
//   variant    0..n  an index into the glyph's parameter table -- this is what
//                    "Enzyme 5 vs Enzyme 6" actually selects: subunit count,
//                    lobe orientation, barrel angle
//   components a list of ComponentSpec: repeated sub-units (membrane lipids,
//              protein subunits, DNA base pairs, ribosome beads, plasmid
//              markers). This is the axis that gives the user real EDITABLE
//              DATA -- see component.hpp.
//
// So chooseGenericShape_Enzyme_6 is NOT a new drawing routine: it is
// {glyph=Blob, variant=6} which resolves to {lobes=3, orientation=...}. The
// renderer has no switch over preset ids; it reads BioShapeParams and draws
// what the parameters describe. Adding a 90th preset is a table row.
//
// -----------------------------------------------------------------------------
// PROVENANCE OF EVERY NUMBER
// -----------------------------------------------------------------------------
//  [SRC-A]/[SRC-B]  the command string and the graphic name -- from the files.
//  [GUIDE]          the family membership -- from DRAWING_FEATURES_2026-10-14.md
//                   "BioDraw 89 项可进一步拆分: 酶 8、G 蛋白 6、受体 11、离子通道
//                   13、免疫球蛋白 5、核孔 2、DNA/RNA 3、膜 4、细胞器 12、核糖体 6、
//                   云形/其他图形 8、微生物 9、螺旋蛋白 1、质粒图谱 1"
//  [PROJ]           subunit counts, lobe radii, spacing fractions, base-pair
//                   counts -- PROJECT-CHOSEN numeric values. They are the
//                   DEFAULT a user then edits. They are NOT claimed to be
//                   ChemDraw's internal constants, which are not published.
// =============================================================================

#include "next/core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace biodraw {

// ---------------------------------------------------------------------------
// Version handshake (same shape as next/core, next/render, next/graphics)
// ---------------------------------------------------------------------------
enum {
    kBiodrawMajor    = 1,
    kBiodrawMinor    = 0,
    kBiodrawRevision = 0,
    kBiodrawVersion  = kBiodrawMajor * 10000 + kBiodrawMinor * 100 + kBiodrawRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestBiodrawCompatibility(int major, int minor) {
    return major == kBiodrawMajor && minor <= kBiodrawMinor;
}

// A preset id that can never collide with a row id (rows are 0..88).
static const uint32_t kNoPreset = 0xFFFFFFFEu;

// ---------------------------------------------------------------------------
// Families: the 14 groups the task statement enumerates, with its own counts
// ---------------------------------------------------------------------------
enum class BioFamily : uint8_t {
    Enzyme = 0,        // 8
    GProtein,          // 6
    Receptor,          // 11
    IonChannel,        // 13
    Immunoglobin,      // 5
    NuclearPore,       // 2
    NucleicAcid,       // 3
    Membrane,          // 4
    Organelle,         // 12
    Ribosome,          // 6
    Misc,              // 8   ("云形/其他图形")
    Microorganism,     // 9
    HelixProtein,      // 1
    PlasmidMap,        // 1
    Count
};

// The exact per-family counts from the task statement / DRAWING_FEATURES.
// verifyPresetTable() checks the built table against this array, so a table edit
// that breaks the 8/6/11/13/5/2/3/4/12/6/8/9/1/1 decomposition fails a test.
extern const int kFamilyExpectedCount[static_cast<int>(BioFamily::Count)];

const char* familyName(BioFamily f);
bool        familyFromName(const std::string& name, BioFamily* out);
// The ObjectKind-agnostic "toolbar order" index of a family (0..13).
int         familyIndex(BioFamily f);

// ---------------------------------------------------------------------------
// GlyphKind: the bounded drawable vocabulary
// ---------------------------------------------------------------------------
// These 24 primitives are the whole drawing vocabulary. A glyph is a pure
// function of (GlyphParams) -> Contour[], implemented in geometry.cpp. The
// mapping from a preset to a glyph+variant lives in the table; it is data.
enum class GlyphKind : uint8_t {
    Blob = 0,        // rounded organic cell body, optional lobes/(nucleus)
    Bilayer,         // two parallel lines of head groups = membrane
    Vesicle,         // closed bilayer ring (micelle, liposome, ellipse membrane)
    Capsule,         // rod with rounded caps (bacterium, mitochondrion outline)
    Rod,             // straight bar (centriole, membrane line)
    Spiralling,      // coiled / wavy filament (helix protein, ER ribbon)
    Tube,            // open-ended tube with a lumen (ER, Golgi stack)
    Pore,            // ring of subunits around a channel (nuclear pore)
    Barrel,          // cylinder of subunits (ion channel barrel, ribosome)
    BarrelPore,      // barrel with a central conduction pore
    YShape,          // Y / antibody
    Antibody,        // two heavy + two light chains (immunoglobin variants)
    Receptor,        // 7-transmembrane serpentine with optional ligand
    Channel,         // membrane-spanning gate with optional selectivity filter
    DoubleHelix,     // DNA / RNA strand pair
    Cloverleaf,      // tRNA secondary structure
    Strand,          // single mRNA ribbon with optional beads
    Ribosome,        // large + small subunit pair
    Star,            // N-point star
    Polygon,         // regular N-gon (circle, triangle, hexagon, octagon, ball)
    Cloud,           // scalloped cloud
    Virus,           // capsid polygon with spikes
    Bacteriophage,   // icosahedral head + tail + fibres
    Plasmid,         // circular map with base-range arcs and markers
    Count
};

const char* glyphName(GlyphKind g);

// ---------------------------------------------------------------------------
// ComponentKind: the REPEATED sub-units -- the editable data axis
// ---------------------------------------------------------------------------
// A preset's components are what a user actually edits: "how many lipids in
// this membrane", "how many base pairs", "which region of the plasmid".
// Each is a ComponentSpec with a count, a spacing and a per-unit size, and
// each is addressable by index so a single sub-unit can be recoloured or moved.
enum class ComponentKind : uint8_t {
    None = 0,
    LipidHead,       // membrane head group
    LipidTail,       // membrane acyl tail pair
    Subunit,         // generic protein subunit lobe
    Transmembrane,   // a membrane-spanning helix segment
    BasePair,        // DNA/RNA base pair rung
    Nucleotide,      // a backbone nucleotide bead
    Bead,            // generic small bead (ribosome, vesicle cargo)
    Spike,           // viral surface spike protein
    Crista,          // mitochondrial inner-membrane fold
    Cisterna,        // Golgi flattened stack layer
    PoreSubunit,     // nuclear-pore / channel subunit
    Flagellum,       // bacterial flagellum
    Marker,          // plasmid marker (restriction site / gene arrow)
    Region           // plasmid named region (base range arc)
};

const char* componentName(ComponentKind k);

struct ComponentSpec {
    ComponentKind kind;
    int    count;          // number of repeated units; 0 = none
    double spacing;        // document units between unit origins
    double unitSize;       // document units; meaning depends on kind
    double phase;          // 0..1 fractional offset along the host path
    bool   evenlySpaced;   // false = spacing is an absolute step, not a fit
    ComponentSpec()
        : kind(ComponentKind::None), count(0), spacing(8.0), unitSize(3.0),
          phase(0.0), evenlySpaced(true) {}
};

// ---------------------------------------------------------------------------
// Preset row
// ---------------------------------------------------------------------------
struct BioPresetSpec {
    uint32_t     presetId;        // == index in the table; STABLE, never reorder
    const char*  sourceCommand;   // [SRC-A] tools.csv command= verbatim
    const char*  graphic;         // [SRC-B] XML sharedGraphic identifier
    const char*  toolId;          // "r62.tool.biodraw.<slug>" (registry preset id)
    const char*  displayName;     // short human name for a gallery label
    BioFamily    family;
    GlyphKind    glyph;
    int          variant;         // index into the glyph's variant table
    const char*  note;            // honest one-line scope note
};

const BioPresetSpec* bioPresets();
size_t               bioPresetCount();

const BioPresetSpec* bioPresetById(uint32_t id);
const BioPresetSpec* bioPresetByCommand(const std::string& sourceCommand);
const BioPresetSpec* bioPresetByGraphic(const std::string& graphic);
const BioPresetSpec* bioPresetByToolId(const std::string& toolId);
// The nth preset (0-based within the family) in table order.
const BioPresetSpec* bioPresetByFamilyIndex(BioFamily family, int index);
size_t               bioPresetCountInFamily(BioFamily family);

// ---------------------------------------------------------------------------
// Table integrity, checked at runtime (not asserted in a comment)
// ---------------------------------------------------------------------------
struct PresetTableAudit {
    int  rowCount;                 // must be 89
    int  distinctCommands;         // must be 89
    int  distinctGraphics;         // must be 89
    int  distinctToolIds;          // must be 89
    int  familyCounts[static_cast<int>(BioFamily::Count)];
    int  familyMismatches;         // # families whose count != kFamilyExpectedCount
    int  duplicateReceipts;        // # rows describing a graphic already present
    bool tableOrderIsIdStable;     // presetId == index for every row
    bool familyBlocksContiguous;   // rows of one family form one contiguous block
    bool ok() const;
    std::string summary() const;
};
PresetTableAudit verifyPresetTable();

// ---------------------------------------------------------------------------
// Default component bundle for a preset (the editable starting point)
// ---------------------------------------------------------------------------
// DECLARED IN params.hpp, together with defaultGlyphParamsFor(), because both
// return types that live there (ComponentSpec / BioGlyphParams) and this header
// must stay includable by a consumer that only wants the 89-row table.
//   std::vector<ComponentSpec> defaultComponents(const BioPresetSpec&);
//   void defaultGlyphParamsFor(const BioPresetSpec&, BioGlyphParams*);

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_PRESET_HPP
