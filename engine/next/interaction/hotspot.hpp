#ifndef R62_NEXT_INTERACTION_HOTSPOT_HPP
#define R62_NEXT_INTERACTION_HOTSPOT_HPP
// =============================================================================
// re_round62_viewer/next/interaction/hotspot.hpp
//
// T04 - HOTSPOT DETECTION AND SNAPRESULT.
//
// Two things live here that look similar and are deliberately kept apart:
//
//   Hotspot      - "which existing object is the pointer *on* right now?". Used
//                  for highlight rendering and for choosing the hotkey context.
//                  It is a HOVER concept with hysteresis.
//
//   SnapResult   - "where would a newly drawn bond END?". A superset: it can be
//                  a join to an existing atom, or a constrained free endpoint.
//                  The preview and the commit MUST consume the SAME value; that
//                  is why the type is returned by value and stored, never
//                  re-derived between preview and release.
//
// -----------------------------------------------------------------------------
// LOCAL QUERIES ONLY. FULL SCANS ARE FORBIDDEN.
// -----------------------------------------------------------------------------
// The acceptance criterion is explicit: at 50,000 atoms no mousemove may scan
// the whole document. Every entry point here takes a CANDIDATE SINK (an
// injected interface) and a radius. The platform supplies candidates from
// whatever spatial index it owns; the tests supply them from a spatial::Index
// BVH and assert that a full scan and a local query agree, and separately
// assert the local query touches a bounded number of objects.
//
// A CandidateSet also carries its own statistics so "how many objects did this
// hover touch?" is a measured number in the test output, not an assertion.
// -----------------------------------------------------------------------------

#include "input_event.hpp"
#include "keymap.hpp"

#include <cstddef>
#include <limits>
#include <vector>

namespace r62 {
namespace interaction {

// Re-exported for callers that only include this header.
static const core::ObjectId kNoObject = core::kInvalidObjectId;

// ---------------------------------------------------------------------------
// HotspotKind
// ---------------------------------------------------------------------------
enum class HotspotKind : uint8_t { None = 0, Atom, Bond };

const char* hotspotKindName(HotspotKind kind);

// ---------------------------------------------------------------------------
// Named platform-object kind for a hotspot candidate.
// ---------------------------------------------------------------------------
// A candidate is a point-like thing the pointer can land on. ChemDraw has more
// than atoms and bonds (control handles, label hotspots, endpoints). The enum
// makes the distinction explicit so "treat every candidate as an atom" - which
// would silently join a bond to a selection handle - cannot happen by default.
enum class CandidateKind : uint8_t {
    Atom = 0,        // connectable atom; a bond may join here
    Bond,            // a bond body; a hotspot, never a join target
    Endpoint,        // an existing bond's endpoint handle
    ControlHandle,   // a path/curve control point
    LabelHotspot     // an atom label's clickable box (smaller than the atom)
};

const char* candidateKindName(CandidateKind kind);

// ---------------------------------------------------------------------------
// SnapCandidate
// ---------------------------------------------------------------------------
// The exact shape `interaction_profile.hpp` already consumed, extended with the
// kind and the owning object so the router can (a) exclude the start object and
// (b) refuse a join to a handle that merely *looks* like an atom.
struct SnapCandidate {
    core::ObjectRef ref;        // stable identity of the underlying object
    core::Vec2      point;      // DOCUMENT coordinates of the snap point
    CandidateKind   kind;
    bool            eligible;   // false == visible but not a legal join target
    bool            tolerant;   // label hotspot / handle: uses the smaller radius

    SnapCandidate()
        : kind(CandidateKind::Atom), eligible(true), tolerant(false) {}
    SnapCandidate(const core::ObjectRef& r, const core::Vec2& pt,
                  CandidateKind k = CandidateKind::Atom, bool ok = true,
                  bool tol = false)
        : ref(r), point(pt), kind(k), eligible(ok), tolerant(tol) {}
};

// Statistics for one candidate acquisition. Recorded so the 50,000-atom claim
// ("no full scan") is evidenced rather than asserted.
struct QueryStats {
    size_t nodesVisited;     // BVH nodes the index reported visiting
    size_t candidatesTested; // candidates the resolver examined
    size_t candidateBytes;   // rough working-set size, for reporting
    QueryStats() : nodesVisited(0), candidatesTested(0), candidateBytes(0) {}
    void reset() { nodesVisited = candidatesTested = candidateBytes = 0; }
};

// ---------------------------------------------------------------------------
// CandidateSink: the ONLY way this module reaches into a document
// ---------------------------------------------------------------------------
// Implemented by the platform (T04 adapter over the BVH) and by the tests.
// Deliberately minimal: a box query plus a count. A sink that could iterate the
// whole document would defeat the locality requirement, so it is not offered.
class CandidateSink {
public:
    virtual ~CandidateSink() {}
    // Append every candidate whose snap point lies inside `box` (document units).
    // MUST be backed by a spatial index; MUST NOT walk the document.
    virtual void query(const core::Box& box, std::vector<SnapCandidate>* out,
                       QueryStats* stats) = 0;
    // Number of indexed candidates. Used only for reporting/sanity checks.
    virtual size_t candidateCount() const = 0;
    // Monotonic counter the platform bumps whenever the document geometry
    // changes. The resolver caches nothing across a bump, which is how a stale
    // hotspot after undo/open is structurally impossible rather than a fix.
    virtual uint64_t geometrySerial() const = 0;
};

// ---------------------------------------------------------------------------
// SnapProfile: the tunables, with their provenance stated
// ---------------------------------------------------------------------------
// CAREFUL READING: only fixedLength/fixedAngles/angleStep have an official
// source. The pixel radii and the hysteresis are OUR usability choices and are
// labelled Project below. Presenting 10 px as a ChemDraw constant would be a
// false compatibility claim, which SOURCES_AND_LIMITS.md explicitly warns
// against.
struct SnapProfile {
    // [G82][G92-93] defaults ON, toggled by Ctrl+L / Ctrl+E, released by Alt.
    bool   fixedLength;
    bool   fixedAngles;
    // [G92-93] the guide states 15-degree increments.
    double angleStepDegrees;
    // [PROJECT] capture radius, in DEVICE pixels. Not an official constant.
    double captureRadiusDevice;
    // [PROJECT] release radius; must be >= captureRadiusDevice. This gap IS the
    // hysteresis that stops the boundary from flickering.
    double releaseRadiusDevice;
    // [PROJECT] smaller radius applied to tolerant candidates (label hotspots,
    // control handles) so a big label box does not swallow a whole neighbourhood.
    double tolerantRadiusScale;
    // [PROJECT] off by default: the raw pointer is authoritative for intent.
    bool   snapConstrainedEndpoint;
    // [PROJECT] keep the previous target inside the release ring.
    bool   retainPreviousTarget;
    // [PROJECT] distance below which a press/release pair counts as a CLICK
    // rather than a drag. The legacy editor used 4 device pixels; kept as a
    // named, configurable value so the branch is testable at its boundary.
    double clickSlopDevice;

    SnapProfile();
    // Throws std::invalid_argument on a non-finite or inconsistent profile.
    void validate(double zoom) const;
};

// Cheap validity entry point, so callers can report instead of throwing.
bool validProfile(const SnapProfile& profile, double zoom, std::string* why);

// Conservative query box half-extent in DOCUMENT units for a device radius.
// Querying exactly this box makes a local lookup provably sufficient.
double queryRadius(const SnapProfile& profile, double zoom);

// ---------------------------------------------------------------------------
// SnapResult
// ---------------------------------------------------------------------------
// Preview and commit share ONE instance of this. `endpoint` is in document
// space. When `joined` is true the endpoint is an existing atom's EXACT stored
// coordinate and the constraints are reported as NOT applied, because a join
// overrides geometry rather than being snapped to by it.
struct SnapResult {
    core::Vec2    endpoint;
    core::ObjectRef target;       // kInvalidObjectId when not joined
    bool          joined;
    bool          angleConstrained;
    bool          lengthConstrained;
    // A tolerant candidate (label hotspot / handle) was the winner. Surfaced so
    // the UI can draw a different highlight; never used to change the geometry.
    bool          tolerantTarget;
    bool          valid;          // false when the input was rejected
    std::string   rejectReason;

    SnapResult()
        : endpoint(0, 0), joined(false), angleConstrained(false),
          lengthConstrained(false), tolerantTarget(false), valid(true) {}
};

// ---------------------------------------------------------------------------
// Hotspot + SnapResolver
// ---------------------------------------------------------------------------
struct Hotspot {
    HotspotKind     kind;
    core::ObjectRef object;
    core::Vec2      point;      // document coordinates
    double          distance;   // document units from the pointer
    bool            tolerant;
    bool            retained;   // kept by hysteresis rather than freshly captured
    Hotspot()
        : kind(HotspotKind::None), distance(0.0), tolerant(false), retained(false) {}
    bool valid() const { return kind != HotspotKind::None; }
    HotspotContext context() const {
        return kind == HotspotKind::Atom ? HotspotContext::Atom :
               kind == HotspotKind::Bond ? HotspotContext::Bond : HotspotContext::None;
    }
};

// Hysteresis state. Held by the caller (the input state machine) so the
// resolver itself stays a pure function of (pointer, candidates, previous).
struct HysteresisState {
    core::ObjectRef previous;   // the hotspot retained from the last frame
    HotspotKind     previousKind;
    uint64_t        geometrySerial; // serial the previous value was computed at
    uint64_t        documentId;     // document the previous value belongs to
    HysteresisState()
        : previousKind(HotspotKind::None), geometrySerial(0), documentId(0) {}
    void clear() { previous = core::ObjectRef(); previousKind = HotspotKind::None; }
};

// The resolver. Stateless apart from the caller-held HysteresisState.
class SnapResolver {
public:
    // Find the hotspot under `pointer` (document units). It queries ONLY the
    // box implied by queryRadius(), then applies deterministic selection:
    //   1. a live target inside the release radius wins if hysteresis is on;
    //   2. otherwise the nearest eligible candidate inside the capture radius,
    //      with a deterministic tie break on (kind, id) so a BVH-travelling
    //      order can never change the answer;
    //   3. a tolerant candidate is measured against the scaled radius;
    //   4. result is ineligible (None) when the pointer is beyond release.
    // QueryStats is optional and accumulates.
    static Hotspot findHotspot(const SnapProfile& profile,
                               const core::Vec2& pointer, double zoom,
                               CandidateSink& sink,
                               const HysteresisState& previous,
                               QueryStats* stats);

    // Resolve the endpoint of a bond being drawn from `origin`. When `origin`
    // is a valid atom the endpoint may not resolve back to it (origin
    // exclusion). Alt suppresses BOTH geometry constraints but - as documented
    // in SOURCES_AND_LIMITS.md - still permits candidate capture, which is a
    // project policy and labelled as such.
    static SnapResult resolveBond(const SnapProfile& profile,
                                  const core::Vec2& origin,
                                  const core::Vec2& pointer, double bondLength,
                                  double zoom, CandidateSink& sink,
                                  uint32_t modifiers,
                                  const core::ObjectRef& originRef,
                                  const HysteresisState& previous,
                                  QueryStats* stats);

    // Resolve a free endpoint (no join allowed). Used by tools that must not
    // merge with an existing atom, e.g. functional-group sprouting, where
    // joining a nearby carbon would silently turn a carbonyl into C=C.
    static SnapResult resolveFree(const SnapProfile& profile,
                                  const core::Vec2& origin,
                                  const core::Vec2& pointer, double bondLength,
                                  double zoom, uint32_t modifiers);

    // Axis constraint used by Ctrl+Shift drag: project `pointer` onto the
    // dominant axis from `origin`. [G82][G124] Shift constrains; Ctrl+Shift
    // constrains to horizontal/vertical.
    static core::Vec2 constrainToAxis(const core::Vec2& origin,
                                      const core::Vec2& pointer);
};

} // namespace interaction
} // namespace r62

#endif // R62_NEXT_INTERACTION_HOTSPOT_HPP
