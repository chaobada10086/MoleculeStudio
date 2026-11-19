#ifndef R62_NEXT_BIODRAW_PLASMID_HPP
#define R62_NEXT_BIODRAW_PLASMID_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/plasmid.hpp
//
// T14 - PLASMID MAP DATA: BASE RANGE, MARKERS AND REGIONS.
//
// -----------------------------------------------------------------------------
// THE ACCEPTANCE CRITERION THIS FILE EXISTS FOR
// -----------------------------------------------------------------------------
//   'moving a plasmid marker and editing an interval must not lose data'
//
// Two design decisions make that structurally true rather than a promise:
//
// 1. A feature stores an explicit BASE RANGE [start, end), never an angle. The
//    angle is derived from the range, the base count and the start angle on EVERY
//    read (featureAngleSpan). A drag therefore edits the range, and the drawn arc
//    cannot disagree with the bases it represents -- there is no second copy of
//    the position to fall out of sync.
//
// 2. Moving one feature NEVER renumbers another. Features are independent records
//    in one vector; moveFeature() rewrites exactly one element's range. A test
//    asserts that every other feature's bytes are unchanged by a move.
//
// -----------------------------------------------------------------------------
// WRAP SEMANTICS
// -----------------------------------------------------------------------------
// A plasmid is circular, so a feature may cross base 0. That is expressed as
// end < start (a WRAPPING feature) and it is the ONLY representation accepted: a
// reader that sees end < start knows the arc runs through the origin, and one
// that sees start <= end knows it does not. A feature is never silently rotated
// to avoid a wrap, because that would change which bases it covers.
// =============================================================================

#include "next/biodraw/params.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace biodraw {

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------
enum {
    kMaxPlasmidFeatures = 512,   // a map with more labels than this is unreadable
    kMaxPlasmidBases    = 100000000
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
// The default map is the familiar pUC19-shaped example: 2686 bp with three
// features. It is a PROJECT example, not a claim about any real plasmid.
PlasmidData defaultPlasmidData();

// The total bases covered by all features (counting a wrapping feature by its
// real length). Used by tests and by the map's own summary label.
int plasmidCoveredBases(const PlasmidData& data);

// ---------------------------------------------------------------------------
// Feature editing (each is a pure model operation; the caller wraps it in a
// command so that one user action is one undo step)
// ---------------------------------------------------------------------------
// Normalise a range against the base count: both ends are wrapped into
// [0, baseCount) and an empty range is given a minimum span of 1 base, so a
// feature is never zero-length and therefore never invisible.
bool normalizeFeatureRange(const PlasmidData& data, int* start, int* end);

// Move a feature so that it STARTS at `newStart`, keeping its span. Returns false
// for an unknown index or an invalid new start; in that case nothing changes.
bool moveFeatureStart(PlasmidData* data, size_t index, int newStart);
// Change a feature's span, keeping its start. span is clamped to
// [kMinFeatureSpan, baseCount].
bool setFeatureSpan(PlasmidData* data, size_t index, int span);
// Set an explicit range, normalised. This is the operation the acceptance test
// drives: it must not disturb any OTHER feature.
bool setFeatureRange(PlasmidData* data, size_t index, int start, int end);

// Add a named feature. Refuses (returns false) when the map is full, the name is
// empty, the base count is not positive, or a feature of that name already
// exists -- a duplicate name would make 'edit the AmpR marker' ambiguous.
bool addFeature(PlasmidData* data, const PlasmidFeature& feature);
// Remove by index. Nothing else moves.
bool removeFeature(PlasmidData* data, size_t index);
// The index of a feature by exact name, or npos (returned as false).
bool findFeature(const PlasmidData& data, const std::string& name, size_t* index);

enum { kMinFeatureSpan = 1 };

// ---------------------------------------------------------------------------
// Geometry derived from the data
// ---------------------------------------------------------------------------
// The signed start/span angles of a feature, in the core::direction() convention,
// derived from the range. A wrapping feature yields startAngle > endAngle, which
// is exactly what an arc drawer needs to know to sweep through zero.
struct FeatureAngle {
    double startDeg;
    double spanDeg;    // signed; total degrees covered by the feature
    bool   wraps;
    FeatureAngle() : startDeg(0.0), spanDeg(0.0), wraps(false) {}
};
FeatureAngle featureAngleSpan(const PlasmidData& data, const PlasmidFeature& feature);

// The label anchor angle: the middle of the feature's arc.
double featureLabelAngleDeg(const PlasmidData& data, const PlasmidFeature& feature);

// Validate the whole map. Returns false and fills `reason` for: a base count
// outside [1, kMaxPlasmidBases], a feature with an empty name, a range that
// cannot be normalised, or too many features.
bool validatePlasmidData(const PlasmidData& data, std::string* reason);

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_PLASMID_HPP
