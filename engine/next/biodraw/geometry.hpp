#ifndef R62_NEXT_BIODRAW_GEOMETRY_HPP
#define R62_NEXT_BIODRAW_GEOMETRY_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/geometry.hpp
//
// T14 - GLYPH GEOMETRY, BOUNDS AND HIT TESTING.
//
// -----------------------------------------------------------------------------
// THE 24 GLYPHS ARE THE WHOLE DRAWING VOCABULARY
// -----------------------------------------------------------------------------
// Every one of the 89 presets resolves to (GlyphKind, variant, BioGlyphParams,
// components). buildGlyphGeometry() is the SINGLE place shapes are produced, and
// it has no knowledge of preset ids. That is the structural guarantee that the
// 89 entries are a parameter table and not 89 code paths.
//
// -----------------------------------------------------------------------------
// CONTOURS: what a consumer receives
// -----------------------------------------------------------------------------
// A BioContour is a run of points with a per-point kind, matching the convention
// T11 already publishes (render::PointKind): 0 = corner, 1 = the PREVIOUS point
// is a quadratic control point. T03's curve flattener understands kind == 1, so
// a BioDraw figure is drawn by the shared renderer with no private GL calls.
//
// A contour also carries its SUB-ELEMENT KEY, which is what makes per-unit
// colouring possible: the render bridge colours each contour by its key.
//
// -----------------------------------------------------------------------------
// HIT TESTING
// -----------------------------------------------------------------------------
// hitTestBioShape() walks the SAME contour list the renderer draws, inflating by
// the resolved stroke width. There is no second, hand-written hit geometry, so
// render and hit test cannot disagree about where the figure is. A figure whose
// interior is filled is hit inside; a figure that is only an outline (membrane,
// plasmid ring, cloud) is hit only near the outline.
// =============================================================================

#include "next/biodraw/components.hpp"
#include "next/biodraw/params.hpp"

#include <vector>

namespace r62 {
namespace biodraw {

// ---------------------------------------------------------------------------
// Contours
// ---------------------------------------------------------------------------
enum : uint8_t {
    kPointCorner = 0,   // a straight segment vertex (or contour start)
    kPointQuad   = 1    // the PREVIOUS point is a quadratic Bezier control point
};

struct BioContour {
    std::vector<core::Vec2> points;
    std::vector<uint8_t>    kind;    // kPointCorner / kPointQuad, one per point
    bool        closed;              // last point joins the first
    bool        filled;              // this run may be filled
    bool        isHole;              // filled AND subtracted from the previous fill
    uint32_t    key;                 // sub-element key (colour identity)
    BioContour() : closed(false), filled(false), isHole(false), key(0) {}
};

// One drawable sub-element of a figure. Grouping contours by element is what
// lets a caller colour, list or (later) ungroup individual sub-units.
struct BioSubElement {
    uint32_t    key;
    ComponentKind component;          // None for a body band
    uint32_t    componentIndex;
    uint32_t    unitIndex;
    std::vector<BioContour> contours;
    BioSubElement() : key(0), component(ComponentKind::None), componentIndex(0), unitIndex(0) {}
};

// The full geometry of one figure, ready to render or hit test.
struct BioGeometry {
    BioShapeParams           params;   // the NORMALISED parameters used
    std::vector<BioSubElement> elements; // draw order
    core::Box    inkBounds;            // geometry only, no stroke inflation
    core::Box    hitBounds;            // includes stroke width and handles
    double       strokeWidth;          // resolved document units (> 0)
    uint64_t     unitCount;            // total repeated units drawn
    BioGeometry() : strokeWidth(0.6), unitCount(0) {}
    bool empty() const { return elements.empty(); }
};

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------
// Resolve the stroke width: params.style.width if > 0, else documentLineWidth,
// else a project default. Mirrors what T11 does so the two modules agree.
double resolveStrokeWidth(const BioShapeParams& params, double documentLineWidth);

// Build the geometry of a figure. `documentLineWidth` is used only when the
// parameters do not carry a width. The parameters are NORMALISED inside, so a
// caller that passes an out-of-range count gets the same geometry the model
// would persist.
BioGeometry buildGlyphGeometry(const BioShapeParams& params, double documentLineWidth);

// Convenience: the host paths a glyph lays its components along. Exposed because
// the tools module needs them to place DRAG HANDLES on the same lines the units
// are on -- a handle that is not on the path would move a lipid without moving
// where the user grabbed.
std::vector<HostPath> glyphHostPaths(const BioShapeParams& params);

// ---------------------------------------------------------------------------
// Hit testing and bounds
// ---------------------------------------------------------------------------
struct BioHitResult {
    bool       hit;
    uint32_t   key;          // the sub-element that was hit (0 when hit == false)
    double     distance;     // document units to the nearest ink
    size_t     elementIndex; // index into BioGeometry::elements
    BioHitResult() : hit(false), key(0), distance(0.0), elementIndex(0) {}
};

// `tolerance` is a DOCUMENT-space radius. The caller converts its device-pixel
// tolerance through core::ViewTransform::deviceRadiusToDocument() first, exactly
// as the rest of the project does; this function never sees pixels.
BioHitResult hitTestBioShape(const BioGeometry& geometry, const core::Vec2& point,
                             double tolerance);

// Bounds of a figure without building contours for it -- used by the selection
// layer, which needs a box for every object and must not pay for full geometry.
// Intentionally CONSERVATIVE: it may exceed the ink box, never fall short.
core::Box bioShapeBounds(const BioShapeParams& params, double documentLineWidth);

// ---------------------------------------------------------------------------
// Non-destructive transform
// ---------------------------------------------------------------------------
// Scaling a figure edits its PARAMETERS (axis vectors, unit sizes), so the
// result is a different figure with the same content, not a resampled picture.
// A non-uniform stretch is expressible because the frame has two axis vectors.
//
//   * the repeated UNIT COUNT does not change under a scale (that is what
//     'stretch without destroying the structure' means);
//   * a unit's own size scales by the geometric mean of the two factors, so a
//     stretched membrane has slightly oval heads rather than absurd ones;
//   * `distort` additionally shears the frame, which is what the task calls
//     '畸变' and is deliberately a SEPARATE flag from scaling.
core::Error scaleBioShape(BioShapeParams* params, const core::Vec2& pivot,
                          double factorU, double factorV, bool* changed);
core::Error rotateBioShape(BioShapeParams* params, const core::Vec2& pivot,
                           double degrees);
// Shear factor: 0 keeps a rectangle, identical to the T11 notion of a shear.
core::Error distortBioShape(BioShapeParams* params, double shearU, double shearV);

// ---------------------------------------------------------------------------
// Sub-element extraction (what 'ungroup' means for a BioDraw figure)
// ---------------------------------------------------------------------------
// A figure is ONE document object. 'Ungroup' must materialise its sub-elements
// as real, independently selectable objects -- and an undo must put them back.
// These two functions are the model-side half of that; the command assembly is
// in model.hpp (explodeBioShapeCommand / collapseBioShapeCommand).
struct ExplodedPart {
    uint32_t   key;
    GlyphKind  glyph;        // a simple glyph that draws the part alone
    BioShapeParams params;   // a fully independent figure for the part
    ExplodedPart() : key(0), glyph(GlyphKind::Blob) {}
};
std::vector<ExplodedPart> explodeParts(const BioGeometry& geometry, double documentLineWidth);

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_GEOMETRY_HPP
