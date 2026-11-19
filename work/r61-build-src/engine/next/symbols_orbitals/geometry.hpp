#ifndef R62_NEXT_SYMBOLS_ORBITALS_GEOMETRY_HPP
#define R62_NEXT_SYMBOLS_ORBITALS_GEOMETRY_HPP
// =============================================================================
// re_round62_viewer/next/symbols_orbitals/geometry.hpp
//
// T12 - PURE GEOMETRY FOR CHEMICAL SYMBOLS, PUSH TRAJECTORIES AND ORBITALS.
//
// NO OpenGL, NO Win32, NO font, NO document. Every function is a pure function of
// its arguments, which is what lets the SAME code serve the renderer (T03
// Primitive emission), the hit tester (T04/T05), the handle editor and the
// headless acceptance tests / SVG evidence.
//
// -----------------------------------------------------------------------------
// THE ONE RULE THIS FILE EXISTS TO ENFORCE
// -----------------------------------------------------------------------------
// PREVIEW AND COMMIT MUST AGREE. `buildSymbolGeometry` / `buildOrbitalGeometry`
// are called with the SAME parameter struct by the preview and by the commit, so
// there is no second code path that could disagree (the defect class recorded in
// SOURCES_AND_LIMITS.md).
//
// -----------------------------------------------------------------------------
// UNITS
// -----------------------------------------------------------------------------
// Document space: x right, y DOWN, unit = CDXML point. Inherited from
// next/core/contracts.hpp. Angles are degrees CCW in MATH orientation, so
// core::direction(90) points visually UP (negative y). Never flip y here.
//
// -----------------------------------------------------------------------------
// ROTATION / DPI INVARIANCE
// -----------------------------------------------------------------------------
// The acceptance criterion asks that ring/sign/dot/lobe geometry stay correct
// under reverse, rotation and DPI change. Two structural properties deliver that:
//
//   1. The glyph SHAPE is generated in a local frame at the origin and then
//      transformed by (rotate, scale, translate) ONCE. So a rotation can only
//      rotate the result; it can never change a ring into an ellipse, because the
//      same uniform scale is applied to x and y.
//   2. No function here takes a DPI or a device pixel. DPI lives only in
//      core::ViewTransform, and the caller converts a device radius to document
//      units before calling hitX(). So a DPI change is a pure scale of the
//      projection and cannot alter document-space geometry.
// =============================================================================

#include "next/symbols_orbitals/preset.hpp"
#include "next/core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace r62 {
namespace symbols {

// ---------------------------------------------------------------------------
// Tessellation policy (same contract as T10 arrows::Tessellation)
// ---------------------------------------------------------------------------
struct Tessellation {
    double tolerance;      // document units; <= 0 selects a safe default
    int    maxSegments;    // hard cap per curve (runaway guard)
    Tessellation() : tolerance(0.0), maxSegments(512) {}
};

// How many segments a full circle of radius r is flattened into, given the
// tolerance. Exposed because the tests assert that a rotated or rescaled ring
// keeps the SAME vertex count logic (i.e. the shape is not re-approximated
// differently after a rotation).
int circleSegments(double radius, const Tessellation& tess);

// ---------------------------------------------------------------------------
// ShapeGeometry: one resolved drawable glyph / lobe / trajectory
// ---------------------------------------------------------------------------
// A mixture of fillable outloops and open strokes. The consumer decides what to
// do with each (T03 emits Polygon for an outline and Polyline for a stroke);
// this struct does not care which backend draws it.
//
// `outlines` are CLOSED fillable contours. `subpathOf` records which lobe each
// outline belongs to (index into the lobe list, -1 for a decoration), so per-lobe
// fill and per-lobe phase colour are addressable without re-parsing geometry.
struct ShapeGeometry {
    std::vector<std::vector<core::Vec2> > outlines;   // closed contourable loops
    std::vector<int>                      outlineLobe;// lobe index per outline
    std::vector<bool>                     outlineFilled;
    // True when the backend must PAINT this outline (flat colour / hatch base),
    // independent of the per-phase 'filled' flag. It exists so the SVG evidence and
    // the GL renderer use ONE predicate: an Open form paints nothing, a Shaded or
    // Solid form paints even the lobe whose phase flag is false. Without it the two
    // backends can disagree (that is exactly how a Solid phase-inverted orbital
    // once drew as an unfilled outline in the SVG but as painted in GL).
    std::vector<bool>                     outlinePaintFill;
    std::vector<bool>                     outlinePositive;
    std::vector<std::vector<core::Vec2> > strokes;    // OPEN polylines
    std::vector<std::vector<core::Vec2> > dots;       // filled dots (a lone pair)
    std::vector<std::vector<core::Vec2> > hatches;    // shading hatch segments
    core::Box   bounds;                               // conservative VISIBLE extent
    double      strokeWidth;                          // document units
    bool        valid;
    ShapeGeometry() : strokeWidth(0.6), valid(false) {}
    size_t segmentCount() const;
};

// ---------------------------------------------------------------------------
// Glyph metrics: the LOCAL frame a symbol occupies
// ---------------------------------------------------------------------------
// Everything is expressed relative to the anchor point, in document units, BEFORE
// the rotation is applied. Exposed so the handle editor and the tests can reason
// about the untransformed shape (which is what makes rotation correctness
// checkable: rotating by 0 must reproduce these numbers exactly).
struct SymbolMetrics {
    double ringRadius;      // outer radius of a charge ring
    double ringStroke;      // the ring stroke width
    double signHalf;        // half-arm length of a plus/minus
    double dotRadius;
    double dotGap;          // centre-to-centre gap of a dot pair
    double barHalf;         // half-length of a bar
    double attachmentR;     // circumradius of a diamond/star
    double beadRadius;
    double siteRadius;      // the anchor-site marker radius (node dot)
    SymbolMetrics()
        : ringRadius(4.4), ringStroke(0.6), signHalf(1.9), dotRadius(0.72),
          dotGap(2.3), barHalf(2.3), attachmentR(2.5), beadRadius(3.4),
          siteRadius(0.0) {}
};

SymbolMetrics symbolMetrics(const SymbolParams& p);

// Build the glyph for a chemical symbol. `tess` controls circle flattening.
core::Error buildSymbolGeometry(const SymbolParams& p, double documentLineWidth,
                                const Tessellation& tess, ShapeGeometry* out);

// ---------------------------------------------------------------------------
// Orbital geometry
// ---------------------------------------------------------------------------
// The orbital SHAPE vocabulary in the local frame, before rotation. Each shape
// contributes a bounded number of closed lobes plus optional decoration strokes
// (the node marker, the dz2 torus rim).
//
// ANCHOR SEMANTICS, straight from the guide:
//   * S and Sigma are CENTERED on the anchor point [GUIDE p241].
//   * SingleLobe/P/HybridSp3/Dxy anchor at their NODE, i.e. the anchor point is
//     the shape origin and the lobes grow away from it [GUIDE p240].
//   * Dz2 also anchors at the node (between the two large lobes).
core::Error buildOrbitalGeometry(const OrbitalParams& p, double documentLineWidth,
                                 const Tessellation& tess, ShapeGeometry* out);

// The lobe outlines alone, in the LOCAL frame (anchor at origin, no rotation,
// no translation). This is the seam the phase/rotation tests use: they assert
// that the transformed geometry equals the local geometry put through the
// documented (rotate, scale, translate) pipeline, bit for bit.
std::vector<std::vector<core::Vec2> > orbitalLocalLobes(const OrbitalParams& p,
                                                        const Tessellation& tess);

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------
// The controls a user sees when a symbol or orbital is selected. [GUIDE p243]
// "You can rotate the radical anion, cation, and lone pair symbols around the
// same end from which they were originally drawn" and [GUIDE p243] "You can
// resize a polymer bead ... by dragging the resize drag handles".
enum class HandleKind : uint8_t {
    None = 0,
    Rotate,       // rotate about the anchor / charge end
    Resize,       // change the glyph size / orbital scale
    LengthHandle, // orbital major axis
    WidthHandle,  // orbital minor axis
    Move          // the anchor itself
};

const char* handleKindName(HandleKind k);

struct Handle {
    HandleKind kind;
    core::Vec2 point;      // document units
    Handle() : kind(HandleKind::None), point(0, 0) {}
};

std::vector<Handle> symbolHandles(const SymbolParams& p, double documentLineWidth,
                                  const Tessellation& tess);
std::vector<Handle> orbitalHandles(const OrbitalParams& p, double documentLineWidth,
                                   const Tessellation& tess);

// The rotation a handle drag should produce, and the scale a resize drag should
// produce. Both are the INVERSE of the handle placement, so the preview equals
// the committed value.
double rotationFromHandleDrag(const core::Vec2& anchor, const core::Vec2& pointer,
                              double baseRotation);
double scaleFromHandleDrag(const core::Vec2& anchor, const core::Vec2& pointer,
                           double baseRadius, double currentScale);

// ---------------------------------------------------------------------------
// Hit testing that AGREES with the drawn geometry
// ---------------------------------------------------------------------------
// The tolerance is a document-space distance already converted from the device
// radius by the caller (core::ViewTransform::deviceRadiusToDocument).
bool hitShape(const ShapeGeometry& geometry, const core::Vec2& point, double tolerance);

double distanceToPolyline(const std::vector<core::Vec2>& path, const core::Vec2& p);
bool   pointInPolygon(const std::vector<core::Vec2>& polygon, const core::Vec2& p);

// ---------------------------------------------------------------------------
// Transform helpers (shared by symbols and orbitals)
// ---------------------------------------------------------------------------
// Rotate about a pivot by `degrees` (CCW math orientation) and scale uniformly
// about the same pivot. One implementation, so a symbol and an orbital cannot
// disagree about what "rotate by 30 degrees" means.
core::Vec2 rotateAbout(const core::Vec2& p, const core::Vec2& pivot, double degrees);
core::Vec2 scaleAbout(const core::Vec2& p, const core::Vec2& pivot, double factor);

// Apply the full (rotate -> scale -> translate) pipeline to a local-frame point.
// `localToDocument` is THE conversion; nothing else may re-implement it.
core::Vec2 localToDocument(const core::Vec2& local, const core::Vec2& anchor,
                           double rotationDeg, double scale);

// ---------------------------------------------------------------------------
// Debug / evidence
// ---------------------------------------------------------------------------
// An SVG dump so a test can attach real geometric evidence without a GPU. Kept in
// the module (not the test) so the demo and the test write the same bytes for the
// same geometry.
std::string svgFromShape(const ShapeGeometry& geometry, const std::string& title);

} // namespace symbols
} // namespace r62

#endif // R62_NEXT_SYMBOLS_ORBITALS_GEOMETRY_HPP
