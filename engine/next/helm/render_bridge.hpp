#ifndef R62_NEXT_HELM_RENDER_BRIDGE_HPP
#define R62_NEXT_HELM_RENDER_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/helm/render_bridge.hpp
//
// T15 - SEQUENCE -> T03 SCENE DESCRIPTION.
//
// THE BOUNDARY
// T03 owns the shared render kernel: it turns Primitive records into batched GPU
// draw calls and it produces the world bounding boxes that hit testing uses. T15
// owns the CHEMISTRY and the SEQUENCE LAYOUT and therefore owns the decision of
// WHERE a residue is drawn and WHAT label it carries. So this file does exactly
// one thing: it emits render::Primitive records and lets T03 draw them.
//
// This module therefore contains NO OpenGL call, no window, no GL context and no
// glyph rasterisation. Text is emitted as a Primitive of kind Text with a font size
// in DOCUMENT units; T03 measures and draws it.
//
// WHAT IS EMITTED
// -----------------------------------------------------------------------------
//  * one marker primitives per residue, shaped from the cartoon shape table when
//    the polymer is in Cartoon display (guide PDF page 172), or a small handle
//    when it is in Structure display;
//  * one Text primitive per residue carrying the residue label;
//  * one Segment per covalent connection, and a Dashed segment per hydrogen bond;
//  * nothing at all for a residue whose chemistry is unknown, beyond its label and
//    a neutral marker: no bond is invented and no shape is claimed.
//
// LAYERS
// -----------------------------------------------------------------------------
// Residues go on render::layers::kContent. Connections go on kContent as well, but
// ranked BELOW the residues by the z field so a bond never covers a label. Nothing
// is emitted into an overlay layer: selection marks and hotspots are T04/T03's job
// and this module must not be able to erase them.
// =============================================================================

#include "next/helm/sequence.hpp"
#include "next/render/scene_api.hpp"

#include <string>

namespace r62 {
namespace helm {

struct RenderOptions {
    // Document units for a residue marker. Not a pixel value: the view scales it.
    double markerRadius;
    // Document units for a residue label. 0 means use the document default.
    double labelSize;
    // Stroke width in document units for a connection. 0 means the view default.
    double connectionWidth;
    // Colour for a covalent connection and for a hydrogen bond.
    uint32_t covalentColor;
    uint32_t hydrogenColor;
    // When true, a residue with no resolved chemistry is drawn in the "unknown"
    // colour so a reader can SEE that it was preserved rather than interpreted.
    uint32_t unknownColor;
    uint32_t blobColor;
    // When true a residue's natural analog drives its cartoon colour, which is the
    // guide's rule (PDF page 172-174). When false every residue uses one colour.
    bool naturalAnalogColors;
    RenderOptions()
        : markerRadius(5.0), labelSize(0.0), connectionWidth(0.0),
          covalentColor(0x333333FFu), hydrogenColor(0x808080FFu),
          unknownColor(0x999999FFu), blobColor(0x6699FFFFu),
          naturalAnalogColors(true) {}
};

// The label string a residue draws. Kept here rather than in the renderer because
// the CHOICE of what a sequence shows is a sequence decision: a known monomer shows
// its symbol, and an unknown one shows the symbol it was preserved with.
std::string labelForMonomerToken(const MonomerInstance& instance);

// The colour the guide's table assigns to a natural analog (PDF pages 172-174).
// The table names vendor palette colours; this returns the RGB that palette entry
// corresponds to, and 0 when the analog is not in the table. Returning 0 rather
// than a default keeps "the guide did not state a colour" distinguishable from
// "the guide stated black".
uint32_t naturalAnalogColor(const std::string& naturalAnalog);

// Emit the whole sequence into `scene`. Returns the number of primitives added.
// A scene is a VALUE; this function appends to it and never clears it, so a caller
// can combine several producers.
size_t emitSequence(const SequenceModel& model, const RenderOptions& options,
                    render::Scene* scene);

// Emit ONLY one polymer. Used for a local edit, where re-emitting the untouched
// polymers would be wasted work. Returns the number of primitives added.
size_t emitPolymer(const SequenceModel& model, size_t polymerIndex,
                   const RenderOptions& options, render::Scene* scene);

// Emit the layers a caller should have registered, in the order this module uses.
// Kept here so an integrator does not have to guess names and z values.
struct LayerRegistration {
    const char* name;
    int32_t     z;
};
const LayerRegistration* sequenceLayers(size_t* count);

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_RENDER_BRIDGE_HPP
