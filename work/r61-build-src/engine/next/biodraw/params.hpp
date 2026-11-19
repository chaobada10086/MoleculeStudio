#ifndef R62_NEXT_BIODRAW_PARAMS_HPP
#define R62_NEXT_BIODRAW_PARAMS_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/params.hpp
//
// T14 - THE PERSISTENT PARAMETER MODEL OF ONE BioDraw OBJECT.
//
// -----------------------------------------------------------------------------
// WHAT IS AND IS NOT STORED IN THE T01 AnnotationRecord
// -----------------------------------------------------------------------------
// core::AnnotationRecord (next/core/document.hpp) freezes exactly:
//
//     id, kind, anchors[4], geometry[4], presetId, scale, rotation,
//     style, fillRGBA, sourceId, alive
//
// That is the RIGHT amount for a generic annotation and the WRONG amount for a
// BioDraw figure, because a membrane is 'N lipids of size s at spacing p', an
// antibody is '2 heavy + 2 light chains with k disulfide bridges', and a plasmid
// is 'a 4361 bp circle with 3 markers and 2 named regions'. None of that is
// expressible in four points.
//
// So, exactly as T11 did for its shapes, the AUTHORITATIVE copy is a versioned
// record in the OBJECT EXTENSION BAG and the AnnotationRecord is kept as a
// MIRROR. The mirror is what a consumer that knows nothing about T14 sees:
//
//     geometry[0] = centre
//     geometry[2] = centre + axisU     (half-extent along the figure +x)
//     geometry[3] = centre + axisV     (half-extent along the figure +y)
//     geometry[1] = (min.x, min.y) of the conservative hit bounds
//
// readBioShape() prefers the bag and FALLS BACK to the mirror, so a figure that
// entered the document from another module is never invisible and never
// un-editable.
//
// -----------------------------------------------------------------------------
// WHY THE BAG IS THE RIGHT HOME (T01's own rule, not a workaround)
// -----------------------------------------------------------------------------
// next/core INTERFACES.md section 5 declares the AnnotationRecord slab family as
// 'structure complete, no editor tool yet' and section 9 declares the extension
// bag with reserved prefixes. T14 is the module that supplies the missing editor
// behaviour for the BioShape slab. It does NOT add a field to a frozen struct and
// does NOT create a rival document model.
//
// -----------------------------------------------------------------------------
// SEMANTICS: A BioDraw FIGURE IS A SCHEMATIC, NOT A CHEMICAL STRUCTURE
// -----------------------------------------------------------------------------
// The task statement requires this be said out loud, so it is enforced rather
// than documented: a BioDraw object NEVER contributes an Atom, a Bond, a node or
// an edge to the chemical graph. insertBioShape() writes ONLY to the bioShapes()
// annotation slab. There is no code path from this module to addAtom/addBond.
// Consequently 'the DNA preset' is an ILLUSTRATION of a double helix and this
// module makes NO claim of RNA/DNA chemical editing -- that is the separate HELM
// monomer-sequence domain (T15), which is a different model with different data.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/biodraw/preset.hpp"

#include <string>
#include <utility>
#include <vector>

namespace r62 {
namespace biodraw {

// ---------------------------------------------------------------------------
// Glyph parameters: one numeric block, shared by every glyph
// ---------------------------------------------------------------------------
// A single flat block rather than a union or one struct per glyph, for the same
// reason AnnotationRecord is a single slab: the vocabulary is bounded and every
// field has a documented meaning for a subset of glyphs. An unused field is
// simply ignored by that glyph's builder, and the persistence format stays one
// record.
//
// This is NOT 'one struct pretending to be 89 shapes'. Which fields matter is a
// property of the glyph and is stated per glyph in geometry.cpp.
struct BioGlyphParams {
    // --- body / outline -----------------------------------------------------
    double lobes;          // Blob: number of lobes around the body
    double lobeDepth;      // Blob: 0..1 how far a lobe bulges from the base radius
    double cornerFraction; // Capsule/Rod/Polygon: rounding as fraction of half-min-extent
    double nodes;          // Polygon/Star/Virus: vertex count (star: point count)
    double innerRatio;     // Star/Polygon: inner radius / outer radius
    double sweepDeg;       // Tube/Spiralling/Plasmid: arc sweep in degrees
    double turns;          // Spiralling/DoubleHelix: number of full turns
    double amplitude;      // Spiralling/Bilayer waviness as fraction of half-height
    double barrelAngleDeg; // Barrel/Capsule: cap-face tilt
    // --- membrane -----------------------------------------------------------
    double leafletGap;     // Bilayer/Vesicle: gap between the two leaflets, doc units
    double headRadius;     // Bilayer/Vesicle: head-group circle radius, doc units
    double tailLength;     // Bilayer/Vesicle: acyl tail length, doc units
    // --- protein ------------------------------------------------------------
    double subunitRadius;  // generic subunit blob radius, doc units
    double tmSegments;     // Receptor/Channel: transmembrane helix count
    double loops;          // Receptor/Channel: inter-segment loop count
    double ligandPresent;  // 0/1 (double so the block stays numeric and flat)
    double disulfides;     // Antibody: disulfide bridge count
    double chainLength;    // Antibody: chain arm length, doc units
    // --- nucleic acid -------------------------------------------------------
    double basePairs;      // DoubleHelix/Strand: rung count
    double helixRadius;    // DoubleHelix: backbone radius, doc units
    double risePerTurn;    // DoubleHelix: axial rise per full turn, doc units
    // --- misc ---------------------------------------------------------------
    double spikes;         // Virus/Bacteriophage: surface spike count
    double capsidFaces;    // Virus/Bacteriophage: capsid facet count
    double beadCount;      // Ribosome/Vesicle cargo bead count
    double nucleusOffset;  // Blob/Organelle: nucleus centre offset in frame units

    BioGlyphParams()
        : lobes(0.0), lobeDepth(0.15), cornerFraction(0.22), nodes(0.0),
          innerRatio(0.45), sweepDeg(0.0), turns(0.0), amplitude(0.0),
          barrelAngleDeg(0.0), leafletGap(3.0), headRadius(1.2), tailLength(4.0),
          subunitRadius(2.6), tmSegments(0.0), loops(0.0), ligandPresent(0.0),
          disulfides(0.0), chainLength(12.0), basePairs(0.0), helixRadius(3.2),
          risePerTurn(10.0), spikes(0.0), capsidFaces(0.0), beadCount(0.0),
          nucleusOffset(0.0) {}
};

// ---------------------------------------------------------------------------
// Plasmid map data: base range, markers, regions  (its own editable record)
// ---------------------------------------------------------------------------
// A plasmid map is the one BioDraw preset whose content is genuinely DATA and
// not decoration: a circular sequence of baseCount base pairs, carrying named
// FEATURES at base ranges. The acceptance criterion is explicit -- 'moving a
// marker and editing an interval must not lose data' -- so the model is built so
// that cannot happen:
//
//  * every marker and region carries an explicit base range [start, end), not a
//    screen angle. The angle is DERIVED from the range and baseCount on every
//    read, so a drag edits the RANGE and the angle cannot drift out of sync
//    with the bases it represents.
//  * start/end are normalised by plasmid.hpp so both lie in [0, baseCount] and
//    the only wrap accepted is the documented one.
//  * each marker owns a colour and a label, and each SURVIVES a move of another
//    marker: they are independent records, not a bitmap.
struct PlasmidFeature {
    std::string name;      // label, e.g. 'AmpR', 'ori', 'EcoRI'
    int    start;          // inclusive base index, 0-based
    int    end;            // exclusive base index; end may be < start for a wrap
    uint32_t colorRGBA;    // fill for the arc/arrow
    int    kind;           // 0 = named region, 1 = marker/site (an arrow)
    double track;          // radial track index (0 = inner, 1, 2 ... = outward)
    PlasmidFeature() : start(0), end(0), colorRGBA(0x3C78C8FFu), kind(0), track(0.0) {}
    int span() const { return end >= start ? (end - start) : 0; }
    bool wraps() const { return end < start; }
};

struct PlasmidData {
    int    baseCount;      // total bp in the circle; > 0
    double startAngleDeg;  // where base 0 sits, core::direction() convention
    std::vector<PlasmidFeature> features;
    std::string title;     // e.g. 'pUC19'
    bool   clockwise;      // direction of increasing base index
    PlasmidData() : baseCount(0), startAngleDeg(0.0), clockwise(true) {}
    bool valid() const { return baseCount > 0; }
};

// ---------------------------------------------------------------------------
// The whole content of one BioDraw object
// ---------------------------------------------------------------------------
// Read this as 'what the user can edit'. Everything in it is persisted, and
// every mutation goes through model.hpp so one user action is one undo step.
struct BioShapeParams {
    uint32_t        presetId;    // kNoPreset for a hand-built figure
    BioFamily       family;
    GlyphKind       glyph;
    int             variant;
    BioGlyphParams  glyphParams; // numbers
    std::vector<ComponentSpec> components;  // repeated, individually addressable

    // Placement: the same axis-vector frame T11 uses, so a NON-UNIFORM stretch
    // and a rotation are both expressible without a separate transform type.
    core::Vec2      centre;
    core::Vec2      axisU;       // half-extent along figure +x
    core::Vec2      axisV;       // half-extent along figure +y
    double          rotationDeg; // additional rotation, core::direction() convention
    double          scale;       // user scale multiplier; 1.0 == preset default

    core::LineStyle style;
    uint32_t        fillRGBA;    // default body fill
    // Per-sub-element colour OVERRIDES. The key is the stable sub-element key
    // from subElementKeyForUnit()/subElementKeyForBand(); a missing key means
    // 'use the body fill'. This is what makes sub-element colouring a real model
    // operation instead of a whole-image repaint.
    std::vector<std::pair<uint32_t, uint32_t> > subElementColors;

    // PlasmidMap only; empty for every other family.
    PlasmidData     plasmid;

    BioShapeParams()
        : presetId(kNoPreset), family(BioFamily::Misc), glyph(GlyphKind::Blob),
          variant(0), centre(0.0, 0.0), axisU(18.0, 0.0), axisV(0.0, 12.0),
          rotationDeg(0.0), scale(1.0), fillRGBA(0xC8DCC8FFu) {}

    bool isPlasmid() const { return glyph == GlyphKind::Plasmid; }
};

// ---------------------------------------------------------------------------
// Extension-bag keys and codecs
// ---------------------------------------------------------------------------
// A 'k=v;k=v;...' text record, the same choice next/graphics and
// next/arrows_curves made: it survives a text diff, has no struct layout in the
// document, an unknown key is ignored on read, and the trailing '.1' lets the
// encoding change without a format bump.
//
// The COMPONENT list and the PLASMID feature list each get their own sub-record,
// because one flat key would grow with the object's data and make a diff
// unreadable.
extern const char* const kBioParamsKey;      // 'bio.params.1'
extern const char* const kBioComponentsKey;  // 'bio.components.1'
extern const char* const kBioPlasmidKey;     // 'bio.plasmid.1'
extern const char* const kBioColorsKey;      // 'bio.colors.1'

std::string  encodeBioParams(const BioShapeParams& p);
// On a malformed record *ok is false and *out is left at its default, so a
// corrupt document can never produce a half-configured figure.
BioShapeParams decodeBioParams(const std::string& text, bool* ok);

std::string  encodeComponents(const std::vector<ComponentSpec>& c);
std::vector<ComponentSpec> decodeComponents(const std::string& text, bool* ok);

std::string  encodePlasmid(const PlasmidData& d);
PlasmidData  decodePlasmid(const std::string& text, bool* ok);

std::string  encodeColors(const std::vector<std::pair<uint32_t, uint32_t> >& c);
std::vector<std::pair<uint32_t, uint32_t> > decodeColors(const std::string& text, bool* ok);

// ---------------------------------------------------------------------------
// Frame helpers (figure-space <-> document-space)
// ---------------------------------------------------------------------------
// Point (u,v) in the unit square (-1..1) mapped into document space, INCLUDING
// the rotation. u is along the figure's own +x, v along its own +y.
core::Vec2 framePoint(const BioShapeParams& p, double u, double v);
// Axis vectors after rotation (what geometry and hit tests must use).
void       frameAxes(const BioShapeParams& p, core::Vec2* outU, core::Vec2* outV);
core::Box  frameAabb(const BioShapeParams& p);

// ---------------------------------------------------------------------------
// Sub-element identity
// ---------------------------------------------------------------------------
// A BioDraw object is ONE document object containing N drawable sub-elements,
// and the task requires those sub-elements to be individually addressable:
// 'complex objects ... sub-element colouring cannot be just a whole-image bitmap
// operation'. A sub-element is named by a stable 32-bit key derived from
// (component index, unit index) or a fixed band id -- NOT by an array position,
// so inserting one lipid does not recolour every lipid after it.
//
//   key = 0xCC000000 | (componentIndex << 12) | unitIndex   (component unit)
//   key = 0xBB000000 | bandId                               (body/outline band)
enum : uint32_t {
    kBandOutline   = 0,   // the figure outline/body stroke
    kBandBody      = 1,   // the body fill
    kBandNucleus   = 2,   // nucleus / lumen / central disc
    kBandLabel     = 3,   // attached text band
    kBandBackbone  = 4,   // helix backbones
    kBandRungs     = 5,   // helix base-pair rungs
    kBandTrack0    = 6    // first plasmid radial track (track t -> kBandTrack0 + t)
};
uint32_t subElementKeyForUnit(uint32_t componentIndex, uint32_t unitIndex);
uint32_t subElementKeyForBand(uint32_t bandId);
bool     parseSubElementKey(uint32_t key, bool* isUnit, uint32_t* a, uint32_t* b);
// Colour actually used for a sub-element: the override if present, else the
// figure body fill. Never returns a disabled colour.
uint32_t resolveSubElementColor(const BioShapeParams& p, uint32_t key);
// Set/clear an override. Returns true if the value changed.
bool     setSubElementColor(BioShapeParams* p, uint32_t key, uint32_t rgba);
bool     clearSubElementColor(BioShapeParams* p, uint32_t key);

// ---------------------------------------------------------------------------
// Preset defaults
// ---------------------------------------------------------------------------
// The [PROJ] default components (unit counts, spacing, sub-unit sizes) and the
// default numeric glyph block for a preset. These are what a user starts from and
// then edits, which is why they are a function that RETURNS A VALUE rather than a
// table baked into the preset row: a caller placing a preset may change anything
// before committing, and nothing about the preset is frozen at placement time.
//
// Declared here (not in preset.hpp) because their type is BioGlyphParams, and
// preset.hpp must stay includable by consumers that only want the 89-row table.
std::vector<ComponentSpec> defaultComponents(const BioPresetSpec& spec);
void                       defaultGlyphParamsFor(const BioPresetSpec& spec,
                                                 BioGlyphParams* out);

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_PARAMS_HPP
