#ifndef R62_NEXT_SYMBOLS_ORBITALS_PUSH_HPP
#define R62_NEXT_SYMBOLS_ORBITALS_PUSH_HPP
// =============================================================================
// re_round62_viewer/next/symbols_orbitals/push.hpp
//
// T12 - THE ELECTRON / RADICAL PUSH TRAJECTORY, EXPRESSED IN T10 TERMS.
//
// -----------------------------------------------------------------------------
// WHY THIS FILE EXISTS
// -----------------------------------------------------------------------------
// The acceptance criterion says: "electron push trajectories use T10 path/arrow
// components, and the single-electron and electron-pair heads must not be
// confused". Both halves of that sentence are answered here and only here:
//
//   * There is NO curve flattener in this module. pushToArrowParams() turns a
//     PushParams into a T10 arrows::ArrowParams, and everything downstream
//     (flattening, head construction, hit testing, SVG) is T10 code.
//   * The head mapping is TOTAL and testable without a rasterizer:
//         PushHead::FullPair    -> ArrowHeadShape::Solid, sides = Both
//         PushHead::HalfRadical -> ArrowHeadShape::Barb,  sides = Left
//     A half head is a DIFFERENT SHAPE (a one-sided barb), not a smaller version
//     of the full head, so the two cannot be confused at any zoom.
//
// [GUIDE p158] pair transfer = FULL head, one electron = HALF head. The guide is
// the source for the mapping; the numeric sizes are [PROJECT].
//
// -----------------------------------------------------------------------------
// ENDPOINTS
// -----------------------------------------------------------------------------
// [GUIDE p158] "Electron transfers can be drawn between atoms, bonds, and
// orbitals." resolvePushEndpoint() classifies a hit object into that closed set so
// the tool can refuse a meaningless source/target instead of drawing an arrow from
// a text caption.
// =============================================================================

#include "next/symbols_orbitals/preset.hpp"
#include "next/arrows_curves/geometry.hpp"
#include "next/arrows_curves/preset.hpp"
#include "next/core/contracts.hpp"
#include "next/core/document.hpp"

namespace r62 {
namespace symbols {

// Classify an object reference for use as a push endpoint.
// Returns PushEndpoint::None for anything the guide does not list.
PushEndpoint resolvePushEndpoint(const core::Document& d, const core::ObjectRef& ref);

// The centre point of a push endpoint object, in document units. Used as the
// arrow tail/tip so the trajectory visibly starts and ends ON the object.
// Returns false when the ref is not a usable endpoint.
bool pushEndpointAnchor(const core::Document& d, const core::ObjectRef& ref,
                        core::Vec2* out);

// Translate a push into T10 arrow parameters. THE head mapping lives here.
arrows::ArrowParams pushToArrowParams(const PushParams& p, double documentLineWidth);

// The T10 arrow geometry for a push. A thin wrapper so callers cannot accidentally
// build the geometry with different parameters than pushToArrowParams().
core::Error buildPushGeometry(const PushParams& p, double documentLineWidth,
                              const arrows::Tessellation& tess,
                              arrows::ArrowGeometry* out);

// The initial sagitta a drag should produce, given where the pointer is relative
// to the chord. Reuses T10 sagittaFromHandleDrag so the preview and the commit
// cannot diverge (the defect class SOURCES_AND_LIMITS.md records).
double pushSagittaFromDrag(const core::Vec2& begin, const core::Vec2& end,
                           const core::Vec2& pointer);

} // namespace symbols
} // namespace r62

#endif // R62_NEXT_SYMBOLS_ORBITALS_PUSH_HPP
