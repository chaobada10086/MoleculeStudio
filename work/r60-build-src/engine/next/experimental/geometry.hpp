#ifndef R62_NEXT_EXPERIMENTAL_GEOMETRY_HPP
#define R62_NEXT_EXPERIMENTAL_GEOMETRY_HPP
// =============================================================================
// re_round62_viewer/next/experimental/geometry.hpp
//
// T16 - PURE GEOMETRY KERNEL FOR A TLC PLATE / ELECTROPHORESIS GEL.
//
// This file links NO renderer and NO document. It includes only
// next/experimental/figure.hpp (which includes next/core/contracts.hpp for Vec2
// and Box). Every geometry and hit-test claim in this task is therefore testable
// headlessly, exactly as next/graphics/geometry.hpp is.
//
// -----------------------------------------------------------------------------
// THE FRAME: ABSOLUTE PLACEMENT LIVES IN THE ANNOTATION RECORD MIRROR
// -----------------------------------------------------------------------------
//     p(u, v) = centre + u * axisU + v * axisV          (u, v in [-1, 1])
//
// axisU points from the board centre to the RIGHT edge (half-extent vector);
// axisV points from the centre to the BOTTOM edge. Document space has y
// downward, so v = -1 is the top (solvent-front side) and v = +1 the bottom
// (origin side).
//
// This is deliberately the SAME representation next/graphics uses, and for the
// same reason: T05 transforms an annotation by applying an affine to its four
// geometry points, so keeping the frame as two independent axis vectors makes a
// rotation, a flip and a non-uniform scale exact, and keeps every RELATIVE
// coordinate in the payload untouched. The relative/absolute split is what makes
// "relative coordinates and display stay consistent" a structural property
// instead of a promise.
// =============================================================================

#include "next/experimental/figure.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace experimental {

// ---------------------------------------------------------------------------
// PlateFrame
// ---------------------------------------------------------------------------
struct PlateFrame {
    core::Vec2 centre;
    core::Vec2 axisU;   // half-extent from centre to the u = +1 edge
    core::Vec2 axisV;   // half-extent from centre to the v = +1 edge
    PlateFrame();
    PlateFrame(const core::Vec2& c, const core::Vec2& u, const core::Vec2& v);

    core::Vec2 pointAt(double u, double v) const;
    // Corners clockwise from the top-left (u=-1, v=-1).
    void corners(core::Vec2 out[4]) const;
    core::Box aabb() const;
    double halfWidth() const;
    double halfHeight() const;
    bool degenerate() const;
    // Inverse map. Returns false when the frame is degenerate.
    bool relativeOf(const core::Vec2& point, double* u, double* v) const;
};

PlateFrame frameFromBox(const core::Box& box);
// A drag from begin to current. A reversed drag keeps its handedness (the axis
// signs go negative) so the drawn board equals the forward drag board.
PlateFrame frameFromDrag(const core::Vec2& begin, const core::Vec2& current);
// The frame with the same centre/axes but uniform scale factor.
PlateFrame frameScaled(const PlateFrame& frame, double factor);
// Rotate the whole frame about a pivot. Angles are degrees, CCW in math
// orientation (core::direction convention), so +90 turns the board
// counter-clockwise on screen. Relative coordinates are untouched by definition.
PlateFrame rotateFrame(const PlateFrame& frame, const core::Vec2& pivot, double degrees);

// A document-space tolerance expressed in relative board units. Uses the smaller
// half-extent, so the band is never narrower than the caller asked for.
double relativeTolerance(const PlateFrame& frame, double documentTolerance);

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
struct BoardGeometry {
    PlateFigure figure;
    PlateFrame  frame;
    core::Box   boardBox;   // the board boundary
    core::Box   bounds;     // everything this object draws (board + stroke)
    bool        rfDefined;
    std::string rfWhy;
    BoardGeometry() : rfDefined(false) {}
};

core::Error buildBoardGeometry(const PlateFigure& figure, const PlateFrame& frame,
                               BoardGeometry* out);

void boardCornerPolygon(const PlateFrame& frame, core::Vec2 out[4]);
// Closed polygon approximating the relative ellipse centred at (cu, cv).
std::vector<core::Vec2> ellipsePolygon(const PlateFrame& frame, double cu, double cv,
                                       double ru, double rv, int segments);
// The rectangle a band occupies.
std::vector<core::Vec2> bandPolygon(const PlateFrame& frame, const Lane& lane, const Band& band);
// The rectangle of a gel well.
std::vector<core::Vec2> wellPolygon(const PlateFrame& frame, const Lane& lane, double topV,
                                    double depth);

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------
// Whole-object hit: inside the board boundary (inflated by documentTolerance) or
// within tolerance of it. This is what T04/T05 use to pick the figure itself.
bool boardHitTest(const PlateFigure& figure, const PlateFrame& frame,
                  const core::Vec2& point, double documentTolerance);

enum class ElementKind : uint8_t {
    None = 0,
    Body,        // interior, not on any element -> whole-figure drag
    Lane,
    Spot,
    Band,
    OriginLine,
    FrontLine
};
const char* elementKindName(ElementKind kind);

struct ElementRef {
    ElementKind kind;
    uint32_t    id;     // element id; 0 for Body / lines
    double      u, v;   // relative coordinates of the pick
    ElementRef() : kind(ElementKind::None), id(0), u(0), v(0) {}
    bool valid() const { return kind != ElementKind::None; }
};

// The element under a point. Spots are tested before bands, bands before lane
// bodies, lane bodies before the guide lines, and Body is the fallback inside the
// board. Returns kind == None outside the board.
ElementRef pickElement(const PlateFigure& figure, const PlateFrame& frame,
                       const core::Vec2& point, double documentTolerance);

core::Vec2 elementDocumentCentre(const PlateFigure& figure, const PlateFrame& frame,
                                 const ElementRef& element);

// ---------------------------------------------------------------------------
// Board resize handles
// ---------------------------------------------------------------------------
enum class BoardHandle : uint8_t {
    None = 0, Left, Right, Top, Bottom,
    TopLeft, TopRight, BottomLeft, BottomRight
};
const char* boardHandleName(BoardHandle handle);
void        boardHandleRelative(BoardHandle handle, double* u, double* v);
BoardHandle pickBoardHandle(const PlateFrame& frame, const core::Vec2& point,
                            double documentTolerance);
// Resize keeping the opposite edge fixed. start is the frame when the drag began;
// the returned frame is the resolved value the caller must BOTH preview and
// commit (T05 preview == commit rule).
PlateFrame  resizeFrame(const PlateFrame& start, BoardHandle handle, const core::Vec2& pointer);

// Small shared helpers
bool pointInPolygon(const std::vector<core::Vec2>& polygon, const core::Vec2& point);
double distancePointSegment(const core::Vec2& p, const core::Vec2& a, const core::Vec2& b);
double clampDouble(double value, double lo, double hi);

} // namespace experimental
} // namespace r62

#endif // R62_NEXT_EXPERIMENTAL_GEOMETRY_HPP
