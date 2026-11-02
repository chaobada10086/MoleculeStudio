#ifndef R62_NEXT_ARROWS_CURVES_GEOMETRY_HPP
#define R62_NEXT_ARROWS_CURVES_GEOMETRY_HPP
// =============================================================================
// re_round62_viewer/next/arrows_curves/geometry.hpp
//
// T10 - PURE GEOMETRY FOR ARROWS AND FREE CURVES.
//
// NO OpenGL, NO Win32, NO font, NO document. Every function here is a pure
// function of its arguments, which is what lets the same code serve:
//   * the renderer (T03 Primitive emission),
//   * the hit tester (T04/T05),
//   * the control-handle editor,
//   * the headless acceptance tests and the SVG/PNG evidence.
//
// -----------------------------------------------------------------------------
// THE ONE RULE THIS FILE EXISTS TO ENFORCE
// -----------------------------------------------------------------------------
// PREVIEW AND COMMIT MUST AGREE. The classic defect (recorded in
// SOURCES_AND_LIMITS.md as a past bug of the legacy editor) is a gesture that
// previews one endpoint and commits another. The structural answer here is that
// the preview is BUILT FROM THE SAME ArrowGeometry / CurveGeometry value that
// the commit writes. There is no second code path:
//
//     buildArrowGeometry(params)  ->  ArrowGeometry   (used by BOTH)
//
// -----------------------------------------------------------------------------
// UNITS
// -----------------------------------------------------------------------------
// Document space: x right, y DOWN, unit = CDXML point. Inherited from
// next/core/contracts.hpp. Angles are degrees CCW in MATH orientation, so
// core::direction(90) points visually UP (negative y). Never flip y here.
// =============================================================================

#include "next/arrows_curves/preset.hpp"
#include "next/core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace r62 {
namespace arrows {

// ---------------------------------------------------------------------------
// Tessellation policy
// ---------------------------------------------------------------------------
// A tolerance in DOCUMENT units. The caller derives it from the view (one device
// pixel worth of document distance) so the SAME arrow re-projects at any zoom
// without visible faceting. Same contract as render::geom::FlattenOptions.
struct Tessellation {
    double tolerance;      // document units; <= 0 selects a safe default
    int    maxSegments;    // hard cap per arc (runaway guard)
    Tessellation() : tolerance(0.0), maxSegments(512) {}
};

// ---------------------------------------------------------------------------
// HeadGeometry: one resolved arrow head
// ---------------------------------------------------------------------------
// A head is described as a mixture of a fillable outline and open strokes. The
// consumer decides what to do with each (T03 emits Polygon for the outline and
// Polyline for the strokes); this struct does not care.
struct HeadGeometry {
    std::vector<core::Vec2> outline;   // closed polygon; empty when nothing is filled
    std::vector<core::Vec2> strokes;   // segment PAIRS (p0,p1, p2,p3, ...)
    bool                    filled;    // whether `outline` should be filled
    bool                    valid;     // false == nothing to draw (degenerate)
    core::Vec2              tip;       // the point the head points AT
    double                  angleDeg;  // direction of travel at the tip
    HeadGeometry() : filled(false), valid(false), angleDeg(0.0) {}
    size_t strokeSegments() const { return strokes.size() / 2; }
};

// ---------------------------------------------------------------------------
// ArrowGeometry: EVERYTHING needed to draw or hit-test one arrow.
// ---------------------------------------------------------------------------
// `shafts` holds the flattened polylines. A straight arrow has one (or two, for
// an equilibrium, or two for a Double); an arc arrow has its tessellated arc.
// `heads` holds the resolved heads, one entry per drawn head.
struct ArrowGeometry {
    std::vector<std::vector<core::Vec2> > shafts;  // polyline per shaft
    std::vector<HeadGeometry>             heads;
    std::vector<std::vector<core::Vec2> > markers; // no-go cross/hash strokes
    core::Box   bounds;                            // conservative VISIBLE extent
    double      strokeWidth;                       // document units
    double      dashOn, dashOff;                   // document units; 0 == solid
    bool        hollow;                            // stroke-only "block" look
    bool        valid;
    ArrowGeometry() : strokeWidth(0.6), dashOn(0.0), dashOff(0.0),
                      hollow(false), valid(false) {}
    size_t segmentCount() const;
};

// Build the full geometry. This is THE function; preview and commit both call it
// with the same ArrowParams and therefore cannot disagree.
core::Error buildArrowGeometry(const ArrowParams& params,
                               double documentLineWidth,
                               const Tessellation& tess,
                               ArrowGeometry* out);

// The stroke width a preset draws its shaft with, in document units. Exposed so
// the editor can show it and so the test can assert the size axis is real.
double arrowStrokeWidth(const ArrowParams& params, double documentLineWidth);

// Flatten one arc into a polyline. `sagitta` is the mid-arc bulge (positive ==
// bulge to the LEFT of begin->end in document space). Returns the number of
// points appended. Exposed because the arc-handle editor and the tests need it
// without building a whole arrow.
size_t flattenArc(const core::Vec2& begin, const core::Vec2& end, double sagitta,
                  const Tessellation& tess, std::vector<core::Vec2>* out);

// The midpoint of the arc/chord, i.e. where the arc adjustment handle sits.
core::Vec2 arcMidpoint(const core::Vec2& begin, const core::Vec2& end, double sagitta);

// The sagitta a given on-screen drag of the arc handle should produce. This is
// the inverse of arcMidpoint along the chord normal and is what makes the drag
// preview equal to the committed value.
double sagittaFromHandleDrag(const core::Vec2& begin, const core::Vec2& end,
                             const core::Vec2& pointer);

// ---------------------------------------------------------------------------
// Single head builders (used by buildArrowGeometry AND by the curve-with-head
// feature, which is how "an arbitrary spline can be given an arrowhead" works).
// ---------------------------------------------------------------------------
HeadGeometry buildHead(const core::Vec2& tail, const core::Vec2& tip,
                       const HeadParams& params);

// ---------------------------------------------------------------------------
// Handle layout
// ---------------------------------------------------------------------------
// The adjustment handles a user sees when an arrow is hovered with an arrow tool
// or a selection tool. [GUIDE p109] "the adjustment handles appear".
enum class HandleKind : uint8_t {
    None = 0,
    Begin,          // move the tail end
    End,            // move the head end
    Middle,         // drag to create/change the arc  [GUIDE p109]
    ArcRadius,      // change the arc radius without moving the chord
    HeadSize,       // change the head length/width
    SplitA,         // equilibrium: independent length of half A  [GUIDE p112]
    SplitB,         // equilibrium: independent length of half B
    Control         // a free-curve control point
};

struct Handle {
    HandleKind kind;
    core::Vec2 point;       // document units
    int        index;       // control-point index for HandleKind::Control
    int        shaft;       // 0 or 1: which equilibrium half / which shaft
    Handle() : kind(HandleKind::None), index(-1), shaft(0) {}
};

std::vector<Handle> arrowHandles(const ArrowParams& params,
                                 double documentLineWidth,
                                 const Tessellation& tess);

const char* handleKindName(HandleKind k);

// ---------------------------------------------------------------------------
// Reversal
// ---------------------------------------------------------------------------
// [GUIDE p110] "Click an unmodified arrow, or one that has had only its length
// changed, to reverse its direction." The rule that only a length change is
// compatible with a reversal click is expressible, so it is expressed: the
// caller tells us whether the arrow is otherwise unmodified.
bool canReverse(const ArrowParams& params);
ArrowParams reversedArrow(const ArrowParams& params);

// ---------------------------------------------------------------------------
// Free curves (the two Pen tools)
// ---------------------------------------------------------------------------
// A curve is a polyline with per-point kinds, exactly as core::PathRecord stores
// it. The two Pen tools are:
//   choosePenTool_DrawCurve  - record pointer samples into a new curve
//   choosePenTool_EditCurve  - move control points of an existing curve
//
// CurveGeometry is what both the renderer and the editor consume.
struct CurveGeometry {
    std::vector<std::vector<core::Vec2> > contours;  // flattened; [0] is outer
    core::Box   bounds;
    double      strokeWidth;
    double      dashOn, dashOff;
    bool        fillEnabled;
    uint32_t    fillRGBA;
    bool        closed;
    bool        valid;
    CurveGeometry() : strokeWidth(0.6), dashOn(0.0), dashOff(0.0),
                      fillEnabled(false), fillRGBA(0), closed(false), valid(false) {}
};

core::Error buildCurveGeometry(const std::vector<core::Vec2>& points,
                               const std::vector<uint8_t>& pointKind,
                               bool closed, const CurveStyleSpec& style,
                               double documentLineWidth,
                               const Tessellation& tess,
                               CurveGeometry* out);

// ---------------------------------------------------------------------------
// Freehand intake
// ---------------------------------------------------------------------------
// Raw pointer samples are noisy. A freehand curve is decimated with a
// perpendicular-distance rule (the classic Ramer-Douglas-Peucker criterion) so
// that the stored control points are the ones a user would place by hand, not
// one per mouse message. This is a PROJECT choice; the guide only says "click
// and drag your cursor across the drawing window" [GUIDE p117].
struct FreehandOptions {
    double minPointDistance;   // document units; drop samples closer than this
    double simplifyTolerance;  // document units; RDP epsilon
    size_t maxPoints;          // hard cap
    bool   detectClosure;      // snap the last point onto the first
    double closureTolerance;   // document units
    FreehandOptions()
        : minPointDistance(0.35), simplifyTolerance(0.55), maxPoints(4096),
          detectClosure(true), closureTolerance(2.0) {}
};

// Returns the decimated point list and, by reference, whether the result is
// closed.
core::Error simplifyFreehand(const std::vector<core::Vec2>& raw,
                             const FreehandOptions& options,
                             std::vector<core::Vec2>* out, bool* closed);

// ---------------------------------------------------------------------------
// Hit testing that AGREES with the drawn geometry
// ---------------------------------------------------------------------------
// The tolerance is a document-space distance already converted from the device
// radius by the caller (core::ViewTransform::deviceRadiusToDocument).
bool hitArrow(const ArrowGeometry& geometry, const core::Vec2& point, double tolerance);
bool hitCurve(const CurveGeometry& geometry, const core::Vec2& point, double tolerance);

// Distance from a point to a polyline (document units).
double distanceToPolyline(const std::vector<core::Vec2>& path, const core::Vec2& p);
bool   pointInPolygon(const std::vector<core::Vec2>& polygon, const core::Vec2& p);

// ---------------------------------------------------------------------------
// Curve control-point editing
// ---------------------------------------------------------------------------
// [GUIDE p117-118] "Click to select the curve. The curve is selected and
// adjustment handles appear along the curve." and "You can add a segment to an
// existing curve by selecting the curve with EditCurve tool, and clicking in the
// document window". Both operations are pure functions on a point list.
core::Error addCurveSegment(std::vector<core::Vec2>* points,
                            std::vector<uint8_t>* pointKind,
                            const core::Vec2& where, bool* closed);
core::Error moveControlPoint(std::vector<core::Vec2>* points,
                             const std::vector<uint8_t>& pointKind,
                             size_t index, const core::Vec2& to);
core::Error deleteControlPoint(std::vector<core::Vec2>* points,
                               std::vector<uint8_t>* pointKind, size_t index,
                               bool* closed);

// Self-intersection and hole detection, used by the "a self-intersecting, holed
// or closed curve must not crash" acceptance criterion, and to decide whether a
// fill can be produced at all.
struct CurveTopology {
    bool selfIntersects;
    bool closed;
    size_t intersectionCount;
    size_t contourCount;      // >1 == a contour was split by a self-crossing
    bool hasHole;             // a contour is inside another
    CurveTopology() : selfIntersects(false), closed(false),
                      intersectionCount(0), contourCount(0), hasHole(false) {}
};
CurveTopology analyseCurve(const std::vector<core::Vec2>& points, bool closed);

// Split a self-intersecting closed contour into simple contours at its
// crossings, so a fill with holes can be produced. Returns the number of
// contours written. A no-op (one contour) when there is no crossing.
size_t splitSelfIntersections(const std::vector<core::Vec2>& points,
                              std::vector<std::vector<core::Vec2> >* out);

// ---------------------------------------------------------------------------
// Debug / evidence
// ---------------------------------------------------------------------------
// An SVG polyline dump so a test can attach real geometric evidence without a
// GPU. Kept in the module (not the test) so the demo and the test write the same
// bytes for the same geometry.
std::string svgFromArrow(const ArrowGeometry& geometry, const ArrowParams& params,
                         double viewBoxWidth, double viewBoxHeight);
std::string svgFromCurve(const CurveGeometry& geometry);

} // namespace arrows
} // namespace r62

#endif // R62_NEXT_ARROWS_CURVES_GEOMETRY_HPP
