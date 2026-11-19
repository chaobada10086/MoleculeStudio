#ifndef R62_NEXT_BIODRAW_COMPONENTS_HPP
#define R62_NEXT_BIODRAW_COMPONENTS_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/components.hpp
//
// T14 - PARAMETRIC COMPONENT EXPANSION.
//
// -----------------------------------------------------------------------------
// THE REQUIREMENT THIS FILE ANSWERS
// -----------------------------------------------------------------------------
// The task statement is explicit:
//
//   'Membranes / proteins / DNA must have EDITABLE unit repetition, spacing and
//    sub-unit counts ... complex objects must scale, distort, group/ungroup and
//    colour sub-elements WITHOUT being just a whole-image bitmap operation.'
//
// So every repeated structure in a BioDraw figure is EXPANDED here into an
// explicit list of placed units, each carrying:
//
//   * its stable sub-element key (subElementKeyForUnit) -- so it can be
//     individually coloured or replaced;
//   * its document-space position and size -- so a scale or a stretch is a
//     recomputation, not a resample of a picture;
//   * its own local frame -- so a stretched membrane's lipid heads stay round
//     while the membrane stretches.
//
// -----------------------------------------------------------------------------
// WHY THE EXPANSION IS A PURE FUNCTION OF THE PARAMETERS
// -----------------------------------------------------------------------------
// expandUnits() reads ONLY the BioShapeParams and the metrics passed in. It never
// reads the document, never allocates a cache keyed on object identity, and
// never looks at the previous expansion. That is what makes 'N changes ->
// geometry changes' true by construction: there is no path by which a stale
// expansion can survive a parameter edit.
//
// -----------------------------------------------------------------------------
// SPACING SEMANTICS (the boundary the acceptance criteria ask about)
// -----------------------------------------------------------------------------
// evenlySpaced == true : N units spread to FIT the host extent. spacing is a
//                        MINIMUM pitch; if the host cannot fit N units at that
//                        pitch, the units overlap rather than being dropped or
//                        silently thinned -- the count the user asked for is the
//                        count that is drawn. count == 1 centres the unit.
// evenlySpaced == false: units are laid out at exactly `spacing` starting from
//                        the host start, so changing the count does NOT move the
//                        earlier units. This is the mode that makes a repeated
//                        object's count observable without moving everything.
// =============================================================================

#include "next/biodraw/params.hpp"

#include <vector>

namespace r62 {
namespace biodraw {

// ---------------------------------------------------------------------------
// Host path a component is repeated along
// ---------------------------------------------------------------------------
// A component repeats along a straight run, an arc, or a closed ellipse. Which
// one is a property of the glyph, so the glyph builder chooses; the component
// expander only needs the resulting parameterisation.
enum class PathKind : uint8_t {
    Straight = 0,   // membrane line, mRNA strand, rod
    Arc,            // membrane arc, spiralling ribbon
    Ellipse,        // vesicle, micelle, plasmid circle
    Ring            // nuclear pore subunit ring (closed, radial orientation)
};

struct HostPath {
    PathKind kind;
    core::Vec2 start;     // Straight/Arc: path start;  Ellipse/Ring: ignored
    core::Vec2 end;       // Straight/Arc: path end
    core::Vec2 centre;    // Arc/Ellipse/Ring: centre
    double     radius;    // Arc/Ellipse/Ring: radius on the u axis
    double     radiusV;   // Ellipse: radius on the v axis
    double     startDeg;  // Arc/Ellipse: start angle, core::direction() convention
    double     sweepDeg;  // Arc: signed sweep; Ellipse/Ring: 360
    HostPath() : kind(PathKind::Straight), radius(0.0), radiusV(0.0),
                 startDeg(0.0), sweepDeg(0.0) {}

    // Total path length in document units. For a full ellipse this is the
    // Ramanujan perimeter approximation, which is accurate to <0.02% and is
    // enough for SPACING; it is not used for any geometry the user measures.
    double length() const;
    // Point at parameter t in [0,1] along the path.
    core::Vec2 pointAt(double t) const;
    // Unit tangent at t (direction of increasing t).
    core::Vec2 tangentAt(double t) const;
};

// ---------------------------------------------------------------------------
// One placed unit
// ---------------------------------------------------------------------------
struct PlacedUnit {
    uint32_t   key;        // stable sub-element key (subElementKeyForUnit)
    uint32_t   componentIndex;
    uint32_t   unitIndex;
    ComponentKind kind;
    core::Vec2 centre;     // document space
    core::Vec2 axisU;      // local half-extent +x (already rotated to the path)
    core::Vec2 axisV;      // local half-extent +y
    double     t;          // parameter along the host path, 0..1
    bool       outlineOnly;// some units are drawn as a ring, not a filled dot
    PlacedUnit()
        : key(0), componentIndex(0), unitIndex(0), kind(ComponentKind::None),
          t(0.0), outlineOnly(false) {}
};

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------
// Pure function. `params` supplies the counts/spacing/sizes; `path` says where.
// The returned list is in draw order (component 0 first, then component 1, ...)
// and its size is exactly the sum of the component counts, so a test can assert
// 'the number of drawn units equals the requested count' directly.
std::vector<PlacedUnit> expandUnits(const BioShapeParams& params,
                                    size_t componentIndex,
                                    const HostPath& path);

// All components at once. `paths` is parallel to params.components; a component
// with no path (paths.size() <= index, or a degenerate path) is SKIPPED and
// counted in *skipped, never silently placed at the origin.
std::vector<PlacedUnit> expandAllUnits(const BioShapeParams& params,
                                       const std::vector<HostPath>& paths,
                                       uint64_t* skipped);

// Total number of drawable units across all components -- what a test compares
// against the parameters.
uint64_t totalUnitCount(const BioShapeParams& params);

// ---------------------------------------------------------------------------
// Boundary rules (each is a refusal or a documented clamp, never a surprise)
// ---------------------------------------------------------------------------
// The task requires 'repeated object count changes with the parameter and the
// boundary is correct'. These are the boundaries, and they are enforced here.
enum { kMaxComponentCount = 4096 };   // matches the 12-bit unit index in the key
// Document units; below this a unit is smaller than a printed dot. A double, not
// an enum member, because it is a length and not a count.
static const double kMinComponentUnitSize = 0.05;

// Validate and return a normalised copy. Rules:
//  * count < 0            -> 0 (a component with no units draws nothing)
//  * count > kMax         -> kMax, and *clamped is set true
//  * spacing <= 0         -> 0.1 document units, and *clamped is set true
//  * unitSize < minimum   -> minimum, and *clamped is set true
//  * phase outside [0,1)  -> wrapped into [0,1), and *clamped is set true
// A caller that checked *clamped can tell the user; a caller that did not still
// gets a drawable component.
ComponentSpec normalizeComponent(const ComponentSpec& spec, bool* clamped);
BioShapeParams normalizeComponents(const BioShapeParams& params, bool* clamped);

// The count actually drawn for a component: 0 when the component kinds is None,
// otherwise the normalised count. Separate from the stored count so that
// 'stored 5000, drawn 4096' is visible rather than hidden.
int drawnUnitCount(const ComponentSpec& spec);

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_COMPONENTS_HPP
