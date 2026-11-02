#ifndef R62_NEXT_BONDS_GEOMETRY_HPP
#define R62_NEXT_BONDS_GEOMETRY_HPP
// =============================================================================
// re_round62_viewer/next/bonds/geometry.hpp
//
// T07 - bond geometry generation. Pure functions over core::Vec2.
//
// NO OpenGL, NO Win32, NO render module link. The output (BondRenderData) is
// converted to render::Primitive by bonds/adapters.hpp. This split is what lets
// the exact wedge polygon, hash count and multiple-bond spacing be asserted in a
// headless test with no GPU.
//
// GEOMETRY CONTRACT (inherited from core, never redefined)
//   * document space: x right, y DOWN, unit = CDXML point
//   * direction(d) is core::direction - the only angle->vector conversion
//   * there is NO DPI here; DPI belongs to ViewTransform in the caller
// =============================================================================

#include "next/bonds/spec.hpp"

namespace r62 {
namespace bonds {

// ---------------------------------------------------------------------------
// What the caller knows about the two atoms, so geometry can avoid labels.
// ---------------------------------------------------------------------------
// The generator never reads a Document: it takes exactly the facts it needs.
// That keeps it a pure function and keeps T07 free of a hidden model copy.
struct EndpointContext {
    bool   drawsLabel;    // the atom renders a visible element/label glyph
    double labelRadius;   // document units of that label, 0 when none
    EndpointContext() : drawsLabel(false), labelRadius(0.0) {}
};

// ---------------------------------------------------------------------------
// Axis resolution
// ---------------------------------------------------------------------------
// The single place a bond axis is turned into (begin, end, dir, normal, len).
// Everything else consumes this, so no code path can invent a second pair of
// endpoints - the "preview and commit use one resolved value" rule from T04.
struct BondAxis {
    core::Vec2 begin;
    core::Vec2 end;
    core::Vec2 dir;     // unit, begin -> end
    core::Vec2 normal;  // unit, perpendicular (-dir.y, dir.x)
    double     length;
    bool       degenerate;   // length below the usable threshold
    bool       reversed;     // the caller asked for a reversed draw order
    BondAxis() : length(0.0), degenerate(false), reversed(false) {}
};

// Build the axis from two document-space points. minLength is the threshold
// below which the result is degenerate (the caller passes the option value).
BondAxis makeAxis(const core::Vec2& begin, const core::Vec2& end, double minLength);

// Same, but with the endpoints swapped first. Used by reverse bond direction,
// which must keep the SAME geometry and swap only the stored begin/end order.
BondAxis makeAxisReversed(const core::Vec2& begin, const core::Vec2& end,
                          double minLength);

// ---------------------------------------------------------------------------
// The generator
// ---------------------------------------------------------------------------
// axes selects order/display/semantics/stereo/side. ctxBegin/ctxEnd describe the
// atom labels so a multiple bond can be shortened for clearance. The returned
// data ALWAYS contains at least the axis stroke for a non-degenerate bond, so a
// renderer never has to special-case "nothing to draw".
BondRenderData buildBondGeometry(const BondAxis& axis, const BondAxes& axes,
                                 const BondGeometryOptions& options,
                                 const EndpointContext& ctxBegin,
                                 const EndpointContext& ctxEnd);

// Convenience: axis + geometry in one call.
BondRenderData buildBondGeometry(const core::Vec2& begin, const core::Vec2& end,
                                 const BondAxes& axes,
                                 const BondGeometryOptions& options);

// ---------------------------------------------------------------------------
// Small shared helpers, exposed because the tests assert on them directly.
// ---------------------------------------------------------------------------
// Distance between the lines of a multiple bond, per the GUIDE BondSpacing rule
// (a percentage of the bond length). Degenerate input yields 0.
double multipleBondSpacing(double length, const BondGeometryOptions& options);

// Number of distinct visible line/polygon strokes the geometry produced. The
// performance report quotes this as the visible geometry count.
size_t visibleShapeCount(const BondRenderData& data);

// Total number of points across every shape (a cheap tessellation proxy).
size_t totalPointCount(const BondRenderData& data);

// ---------------------------------------------------------------------------
// Label interaction
// ---------------------------------------------------------------------------
// Given an axis point that starts at an atom with a label of labelRadius, return
// the point pushed away from the atom along the bond direction by the clearance.
// This is the ONE place label avoidance is computed.
core::Vec2 clearLabel(const core::Vec2& atom, const core::Vec2& toward,
                      double labelRadius, double clearance);

} // namespace bonds
} // namespace r62

#endif // R62_NEXT_BONDS_GEOMETRY_HPP
