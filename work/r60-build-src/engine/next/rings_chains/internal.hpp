#ifndef R62_NEXT_RINGS_CHAINS_INTERNAL_HPP
#define R62_NEXT_RINGS_CHAINS_INTERNAL_HPP
// =============================================================================
// re_round62_viewer/next/rings_chains/internal.hpp
//
// T08 - helpers shared between this module's .cpp files. NOT a public header:
// nothing outside next/rings_chains/ may include it, and it declares no type
// that a caller needs. Keeping it separate is what lets the public header stay a
// single self-contained contract.
// =============================================================================

#include "next/rings_chains/rings_chains.hpp"

namespace r62 {
namespace rings_chains {
namespace detail {

// Preset -> id lookups used by the placement code.
const char* ringToolId(RingPresetId id);
const char* ringCommandName(RingPresetId id);
const char* chainToolId(ChainPresetId id);
const char* chainCommandName(ChainPresetId id);

// Geometry helpers (presets.cpp).
core::Vec2 placeLocal(const core::Vec2& local, double rotationDeg, const core::Vec2& origin);
core::Vec2 rotateLocal(const core::Vec2& local, double rotationDeg);
double directionTo(const core::Vec2& origin, const core::Vec2& target);
core::BondSemantics ringBondSemantics(RingPresetId preset);

// Normalise an angle to [0, 360).
double normalizeDeg(double d);

// Smallest absolute difference between two angles, in [0, 180].
double angleGapDeg(double a, double b);

// True when two documents are the same instance AND the revision is unchanged,
// i.e. a placement resolved earlier is still safe to commit. A placement from a
// different document, or from before an undo, is refused - this is the T04
// "stale hotspot after open/undo" rule applied to placement.
bool placementIsFresh(const core::Document& document,
                      core::DocumentId placementDocumentId,
                      uint64_t placementRevision);

// The document bond length, falling back to the core default when unset.
double resolvedBondLength(const core::Document& document, double requested);

} // namespace detail
} // namespace rings_chains
} // namespace r62

#endif // R62_NEXT_RINGS_CHAINS_INTERNAL_HPP