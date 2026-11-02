#ifndef R62_NEXT_SYMBOLS_ORBITALS_RENDER_BRIDGE_HPP
#define R62_NEXT_SYMBOLS_ORBITALS_RENDER_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/symbols_orbitals/render_bridge.hpp
//
// T12 - THE BRIDGE FROM SYMBOL/ORBITAL/PUSH GEOMETRY TO T03 render::Primitive.
//
// This is the ONLY header in next/symbols_orbitals/ that includes next/render.
// The geometry kernel stays render-independent, so it is testable with no GPU.
//
// -----------------------------------------------------------------------------
// T10 REUSE FOR TRAJECTORIES (no second curve flattener)
// -----------------------------------------------------------------------------
// A push trajectory is NOT flattened here. `appendPushPrimitives` converts the
// PushParams into a T10 ArrowParams (a straight or slightly curved arrow with the
// documented head) and calls arrows::appendArrow(). That is the mechanical meaning
// of the acceptance criterion "electron push trajectories use T10 path/arrow
// components": one flattener, one head builder, one hit tester, in T10.
//
// The FULL-head vs HALF-head distinction [GUIDE p158] maps onto T10 head axis:
//   PushHead::FullPair    -> ArrowHeadShape::Solid,  head.sides = Both
//   PushHead::HalfRadical -> ArrowHeadShape::Barb,   head.sides = Left
// A half head is therefore a genuinely different drawn shape, not a smaller one,
// which is what makes the two pushes visually distinguishable in the evidence.
//
// -----------------------------------------------------------------------------
// VERSION SIGNATURE (the cache key)
// -----------------------------------------------------------------------------
// Primitive::version must change when the object content changes and stay the same
// when it does not. These signatures hash exactly the inputs the geometry depends
// on and NOT the view transform, because zoom is not an object change.
// =============================================================================

#include "next/symbols_orbitals/geometry.hpp"
#include "next/symbols_orbitals/model.hpp"
#include "next/symbols_orbitals/preset.hpp"
#include "next/render/geometry.hpp"
#include "next/render/scene_api.hpp"

#include <cstdint>
#include <vector>

namespace r62 {
namespace symbols {

inline bool requestSymbolsRenderCompatibility(int major, int minor) {
    return render::requestRenderCompatibility(major, minor);
}

// ---------------------------------------------------------------------------
// Signatures
// ---------------------------------------------------------------------------
uint64_t symbolSignatureOf(const SymbolParams& p, double documentLineWidth);
uint64_t orbitalSignatureOf(const OrbitalParams& p, double documentLineWidth);
uint64_t pushSignatureOf(const PushParams& p, double documentLineWidth);

// ---------------------------------------------------------------------------
// Primitive emission
// ---------------------------------------------------------------------------
// One Primitive per drawn element: an outline, a stroke, a dot, a hatch segment.
// A charge ring is ~3 primitives (ring, plus arm, plus stem), NOT one per triangle.
// `layer` is the content layer the caller owns; `baseZ` orders the pieces.
core::Error appendSymbolPrimitives(const ShapeGeometry& geometry,
                                   const SymbolParams& params,
                                   const core::ObjectRef& source,
                                   uint64_t version, uint32_t layer, int32_t baseZ,
                                   std::vector<render::Primitive>* out);

core::Error appendOrbitalPrimitives(const ShapeGeometry& geometry,
                                    const OrbitalParams& params,
                                    const core::ObjectRef& source,
                                    uint64_t version, uint32_t layer, int32_t baseZ,
                                    std::vector<render::Primitive>* out);

// A push goes THROUGH T10. `out` receives T10 arrow primitives carrying the push
// object ref as their source, so hover/delete behave exactly as for an arrow.
core::Error appendPushPrimitives(const PushParams& params, double documentLineWidth,
                                 const core::ObjectRef& source, uint32_t layer,
                                 std::vector<render::Primitive>* out);

// Convenience: build the geometry AND emit.
core::Error appendSymbol(const SymbolParams& p, double documentLineWidth,
                         const Tessellation& tess, const core::ObjectRef& source,
                         uint32_t layer, std::vector<render::Primitive>* out);
core::Error appendOrbital(const OrbitalParams& p, double documentLineWidth,
                          const Tessellation& tess, const core::ObjectRef& source,
                          uint32_t layer, std::vector<render::Primitive>* out);

// ---------------------------------------------------------------------------
// Style translation
// ---------------------------------------------------------------------------
render::Style symbolStyle(const ShapeGeometry& geometry, const SymbolParams& p);
render::Style orbitalStyle(const ShapeGeometry& geometry, const OrbitalParams& p,
                           int lobeIndex, bool filled, bool positive);

// The arrow parameters a push resolves to live in push.hpp, which owns the T10
// mapping. Re-declared nowhere: include next/symbols_orbitals/push.hpp for
// pushToArrowParams() / buildPushGeometry(). That keeps ONE definition of the
// full-head vs half-head rule.

// ---------------------------------------------------------------------------
// Overlay: handles for the currently edited object
// ---------------------------------------------------------------------------
void appendSymbolHandles(const std::vector<Handle>& handles,
                         const core::ObjectRef& source,
                         const core::ViewTransform& view, uint32_t layer,
                         std::vector<render::Primitive>* out);

} // namespace symbols
} // namespace r62

#endif // R62_NEXT_SYMBOLS_ORBITALS_RENDER_BRIDGE_HPP
