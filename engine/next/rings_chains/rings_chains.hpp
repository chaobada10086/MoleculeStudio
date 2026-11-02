#ifndef R62_NEXT_RINGS_CHAINS_RINGS_CHAINS_HPP
#define R62_NEXT_RINGS_CHAINS_RINGS_CHAINS_HPP
// =============================================================================
// re_round62_viewer/next/rings_chains/rings_chains.hpp
//
// T08 - RING PRESETS, CARBON CHAINS, SPROUTING AND STRUCTURE FUSION.
//
// DEPENDS ON (read only; NOTHING here is redefined):
//   next/core/contracts.hpp      Vec2, BondOrder, BondDisplay, ObjectId, Error
//   next/core/document.hpp       Document (read for topology only)
//   next/core/commands.hpp       Command, CommandContext, Invalidation
//   next/core/tool_registry.hpp  Tool, ToolHost, ToolPreset, ToolBehavior
//   next/bonds/edit.hpp          T07 - bond creation incl. duplicate/self rules
//   next/bonds/spec.hpp          T07 - BondAxes (order/display/semantics/stereo)
//   next/interaction/hotspot.hpp T04 - SnapResult
//   next/chemistry/valence.hpp   T06 - valence rule, used as the ONE overlap test
//
// =============================================================================
// WHAT THIS MODULE IS
// =============================================================================
// The source product enumerates exactly these commands (tools.csv rows, verified
// by host_integration/drawing_inventory/DRAWING_FEATURES_2026-10-14.md lines 16
// and 17):
//
//   RING STRUCTURES - 10 commands
//     chooseRingTool_Cyclopropane          chooseRingTool_Cyclobutane
//     chooseRingTool_Cyclopentane          chooseRingTool_Cyclohexane
//     chooseRingTool_Cycloheptane          chooseRingTool_Cyclooctane
//     chooseRingTool_Chair_Cyclohexane1    chooseRingTool_Chair_Cyclohexane2
//     chooseRingTool_Benzene               chooseRingTool_Cyclopentadiene
//
//   CARBON CHAINS - 2 commands
//     chooseAlkaneTool_Acyclic_Chain       chooseAlkaneTool_Snaking_Chain
//
// Those 12 commands are NOT 12 algorithms. They are 12 parameter vectors over a
// small, bounded algorithm set, exactly as T07 did for the 21 bond commands:
//
//   Ring placement   : PlaceRing  (click places a ring, optionally fused)
//   Chain placement  : PlaceChain (drag lays down a chain of a given length)
//
// This module therefore owns ZERO keyboard handling. It publishes core::Tool
// objects and one T04 ActionHandler; T04 resolves the hotkey and calls in.
// SOURCES_AND_LIMITS.md records that keys 6 / 7 / 4 / 5 / 8 / 9 / 0 are
// CONTEXT DEPENDENT on a bond hotspot and that the generic table does NOT list
// 6 for the empty canvas - so the generic-canvas ring keys are j (benzene) and
// Shift+j (cyclopentadiene) only, and everything else arrives by toolbar
// command. That restriction is data in the preset table (RING_HOTKEY_NOTE).
//
// =============================================================================
// THE FIVE PLACEMENT RULES (each is a separate, testable rule)
// =============================================================================
//  1. EMPTY CANVAS   the ring is created at the pointer position, with the
//                    pointer at the ring CENTRE. All atoms and bonds are new.
//  2. GROW FROM AN ATOM   the ring shares exactly ONE atom with the document.
//                    That atom is ring vertex 0 so the attachment angle is
//                    deterministic.
//  3. FUSION ON A BOND    the ring shares exactly ONE BOND - the hot bond.
//                    The shared bond's TWO atom ids are REUSED; the ring adds
//                    exactly (size - 2) new atoms and exactly (size - 3) new
//                    bonds, plus the fusion bond which IS the existing one. The
//                    two shared atoms keep their ids and the shared edge is
//                    NEVER duplicated (a second bond between the same pair is
//                    refused by T01/T07).
//  4. SPIRO   the ring shares exactly one atom (rule 2) AND the caller asks for
//                    a spiro angle. The shared atom's existing bond directions
//                    are measured and the new ring is rotated so its wedges fall
//                    in the gaps between them; a spiro atom with 4 in-plane
//                    neighbours has no gap and the operation is REFUSED with a
//                    reason instead of drawing an overlapping ring.
//  5. PREVIEW   the SAME RingPlacement / ChainPlan value drives the preview and
//                    the commit. The preview computes no document mutation at
//                    all: it is a pure function of (document read, pointer,
//                    modifiers) and the commit consumes the identical struct.
//                    A cancelled preview therefore cannot change the document
//                    because it never touched it.
//
// =============================================================================
// HONEST SCOPE (read RESULTS.md section "what is NOT verified")
// =============================================================================
// * The EXACT numeric coordinates of ChemDraw's ring presets and of its chair
//   conformers are NOT published in the material this project has verified. The
//   geometry constants below are therefore labelled PROJECT (our deliberate
//   choice) except where a document source exists, which is marked GUIDE.
//   "Looks like ChemDraw" is explicitly not a compatibility claim.
// * The sprout DIRECTION heuristic for an atom with 2+ neighbours is our own;
//   the official guide only states the 120 degree chain angle (GUIDE, p.92-93).
//   The difference is recorded in RESULTS.md as required.
// * Aromatic benzene is stored as alternating SINGLE/DOUBLE orders plus the
//   semantics axis; the T01 model has no aromatic-order shortcut and this module
//   does not invent one. Ring closure is REAL: every ring atom is connected by a
//   real bond, and the renderer draws the edges, not a decorative outline.
// =============================================================================

#include "next/bonds/spec.hpp"
#include "next/chemistry/valence.hpp"
#include "next/core/commands.hpp"
#include "next/core/contracts.hpp"
#include "next/core/document.hpp"
#include "next/core/tool_registry.hpp"
#include "next/interaction/hotspot.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace rings_chains {

// ---------------------------------------------------------------------------
// Version handshake (independent of core / bonds / interaction / render)
// ---------------------------------------------------------------------------
enum {
    kRingsMajor    = 1,
    kRingsMinor    = 0,
    kRingsRevision = 0,
    kRingsVersion  = kRingsMajor * 10000 + kRingsMinor * 100 + kRingsRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }
inline int compiledBondsVersion() { return bonds::kBondsVersion; }

inline bool requestRingsChainsCompatibility(int major, int minor) {
    return major == kRingsMajor && minor <= kRingsMinor;
}

// ---------------------------------------------------------------------------
// Provenance. A geometry constant without a source is a defect, not a footnote.
// ---------------------------------------------------------------------------
enum class EvidenceSource : uint8_t {
    Uncited = 0,     // must never appear in a shipped preset
    Toolbar,         // tools.csv row (the command exists)
    Guide,           // bundled ChemDraw User Guide text
    ToolbarGuide,    // both
    Project          // our own deliberate choice, labelled as such
};
const char* evidenceName(EvidenceSource s);

// ---------------------------------------------------------------------------
// RingPresetId - the dense index into ringPresets()
// ---------------------------------------------------------------------------
enum class RingPresetId : uint8_t {
    Cyclopropane = 0,
    Cyclobutane,
    Cyclopentane,
    Cyclohexane,
    Cycloheptane,
    Cyclooctane,
    Chair1,
    Chair2,
    Benzene,
    Cyclopentadiene,
    Count
};

enum class ChainPresetId : uint8_t {
    Acyclic = 0,   // chooseAlkaneTool_Acyclic_Chain  - straight / free chain
    Snaking,       // chooseAlkaneTool_Snaking_Chain  - the "can turn" chain
    Count
};

// ---------------------------------------------------------------------------
// RingTopologyClass - WHAT KIND OF POLYGON the ring is, as data
// ---------------------------------------------------------------------------
//   Flat      a regular polygon in the plane; every vertex lies on the
//             circumcircle. Covers 3..8 rings and cyclopentadiene.
//   Chair     a 3D cyclohexane chair drawn in 2D: the six vertices are NOT
//             concyclic and two bonds are the up/down pucks.
//   Aromatic  a flat polygon whose bonds alternate order. It is a Flat ring plus
//             a bond-pattern axis; the class exists so the bond pattern is never
//             silently dropped when chemistry changes.
enum class RingTopologyClass : uint8_t { Flat = 0, Chair, Aromatic };
const char* ringTopologyClassName(RingTopologyClass c);

// ---------------------------------------------------------------------------
// RingGeometry - the resolved vertex set of one preset
// ---------------------------------------------------------------------------
// Positions are in DOCUMENT units, Y DOWN (core contract), relative to a ring
// LOCAL ORIGIN at (0,0). A placement rotates/translates this verbatim; it never
// re-derives the shape.
//
// Local origin conventions (PROJECT, documented so a caller can rely on them):
//   * Flat / Aromatic  origin = ring CENTROID; ring vertex 0 lies at angle 0
//                      (on the +x axis, to the RIGHT of the centroid).
//   * Chair            origin = the centroid of the six vertices, so the shape
//                      is centred the same way. Vertex order is the walk order.
struct RingVertex {
    core::Vec2  local;
    int         element;    // 1..118
    RingVertex() : element(6) {}
};

struct RingBondSpec {
    int             from;
    int             to;
    core::BondOrder order;
    core::BondDisplay display;
    int             cyclicRow;  // 0 == first half of the alternation, 1 == second
    RingBondSpec() : from(0), to(0), order(core::BondOrder::Single),
                     display(core::BondDisplay::Normal), cyclicRow(0) {}
};

struct RingGeometry {
    RingPresetId        preset;
    RingTopologyClass   topology;
    int                 size;
    std::vector<RingVertex>   vertices;
    std::vector<RingBondSpec> bonds;   // EXACTLY size edges forming ONE cycle
    double              circumRadius;
    double              bondLength;
    bool                planar;        // false == not an equal-edge 2D polygon
    double              minEdgeLength; // measured, for the chair completeness check
    double              maxEdgeLength;
    EvidenceSource      ringShapeEvidence;
    std::string         note;
    RingGeometry()
        : preset(RingPresetId::Cyclopropane), topology(RingTopologyClass::Flat),
          size(0), circumRadius(0.0), bondLength(0.0), planar(true),
          minEdgeLength(0.0), maxEdgeLength(0.0),
          ringShapeEvidence(EvidenceSource::Project) {}
};

// Regular n-gon radius from a bond length:
//     R = bondLength / (2 * sin(pi / n))            [PROJECT: elementary geometry]
// Every edge of a regular n-gon with circumradius R has length 2*R*sin(pi/n), so
// this is exact, not a fit. A test asserts every edge of every flat preset equals
// the requested bond length to 1e-9.
double regularRingRadius(int size, double bondLength);

// Build the geometry of one preset. bondLength is the DOCUMENT bond length
// (core::Document::defaultBondLength()). Returns an error for an out-of-range
// preset id or a non-positive bond length.
core::Error buildRingGeometry(RingPresetId preset, double bondLength,
                              RingGeometry* out);

// The chair conformers. Chair1 and Chair2 are the two orientations the source
// exposes (chooseRingTool_Chair_Cyclohexane1 / 2). They are the SAME ring in two
// rotational states, produced by rotating the base conformer by the angle
// returned here - they are NOT two different molecules.
// PROJECT: the base conformer's own shape. GUIDE: the existence and count of the
// two orientations (two separate toolbar commands exist).
double chairOrientationDegrees(RingPresetId preset);

// ---------------------------------------------------------------------------
// RingPlacement - WHAT to do with a preset, resolved, ready to commit
// ---------------------------------------------------------------------------
// This is the value the PREVIEW and the COMMIT both consume. It contains no
// document pointer, so it stays valid after the gesture ends.
//
//   reuseAtom[i] != kInvalidObjectId -> ring vertex i IS that existing atom; it
//                                       is NOT created and its position is NOT
//                                       changed (fusion / sprout rule).
//   reuseAtom[i] == kInvalidObjectId -> ring vertex i is created at
//                                       worldPosition[i].
// Every ring edge is created UNLESS the document already has a bond between the
// two resolved atoms, in which case that existing bond id lands in reuseBond and
// no new bond is created. That is "reuse the correct atoms and edges, do not
// simply stack two rings", and the commit re-checks it.
struct RingPlacement {
    bool                valid;
    std::string         rejectReason;
    RingPresetId        preset;
    RingGeometry        geometry;
    double              rotationDeg;
    core::Vec2          origin;
    std::vector<core::Vec2>      worldPosition;
    std::vector<core::ObjectId>  reuseAtom;
    std::vector<core::ObjectId>  reuseBond;
    std::vector<uint8_t>         noDouble;   // 1 == this vertex must stay sp3 (CH2)
    core::DocumentId            documentId;
    uint64_t                    documentRevision;
    int createdAtomCount;
    int createdBondCount;
    int reusedAtomCount;
    int reusedBondCount;
    int existingAtomOverlapCount;
    RingPlacement()
        : valid(false), preset(RingPresetId::Cyclopropane), rotationDeg(0.0),
          documentId(core::kInvalidDocumentId), documentRevision(0),
          createdAtomCount(0), createdBondCount(0), reusedAtomCount(0),
          reusedBondCount(0), existingAtomOverlapCount(0) {}
};

enum class RingAttachMode : uint8_t {
    EmptyCanvas = 0,   // rule 1: all new
    GrowFromAtom,      // rule 2: share one atom
    FuseOnBond,        // rule 3: share one bond
    FuseOnPath,        // rule 3b: share two adjacent bonds (peri/bay fusion)
    Spiro              // rule 4: share one atom with a spiro rotation
};
const char* ringAttachModeName(RingAttachMode m);

struct RingRequest {
    RingPresetId     preset;
    RingAttachMode   mode;
    core::Vec2       pointer;           // document coordinates
    core::ObjectId   anchorAtom;        // for GrowFromAtom / Spiro
    core::ObjectId   anchorBond;        // for FuseOnBond / first edge of FuseOnPath
    core::ObjectId   anchorBond2;       // second edge of FuseOnPath
    double           rotationDeg;       // clamped to [0,360); not an "unset" flag
    bool             rotationGiven;     // false == derive the rotation
    double           bondLength;        // 0 == document default
    bool             strictAttachment;  // true == atom0 on anchor / edge0 == shared
    RingRequest()
        : preset(RingPresetId::Cyclohexane), mode(RingAttachMode::EmptyCanvas),
          pointer(0, 0), anchorAtom(core::kInvalidObjectId),
          anchorBond(core::kInvalidObjectId), anchorBond2(core::kInvalidObjectId),
          rotationDeg(0.0),
          rotationGiven(false), bondLength(0.0), strictAttachment(true) {}
};

// Resolve a ring request against a document. PURE with respect to the document:
// it only reads. Never allocates a mutation, never opens a transaction.
core::Error resolveRingPlacement(const core::Document& document,
                                 const RingRequest& request,
                                 RingPlacement* out);

// ---------------------------------------------------------------------------
// Chain plans
// ---------------------------------------------------------------------------
// The two chain tools:
//   Acyclic ("straight")  the chain continues in the CURRENT direction. The
//                         user's drag direction sets it; the default when there
//                         is no drag is defaultDirectionDeg.
//   Snaking ("turns")     the angle between two consecutive bonds - the interior
//                         bond angle the drawing shows - is always
//                         chainAngleDegrees, so the running direction alternates
//                         by +/- (180 - chainAngleDegrees): the zig-zag a
//                         hydrocarbon chain is drawn as. The drag END chooses the
//                         turn SIGN, so dragging back the other way mirrors the
//                         zig-zag instead of drawing a new shape.
struct ChainOptions {
    int      atomCount;
    double   bondLength;         // 0 == document default
    double   chainAngleDegrees;  // GUIDE p.92-93: the standard interior bond angle is 120
    bool     snaking;            // false == Acyclic, true == Snaking
    bool     fixedLength;        // Ctrl+L state; Alt releases it
    bool     fixedAngles;        // Ctrl+E state; Alt releases it
    double   angleStepDegrees;   // platform fixed-angle increment (GUIDE 15)
    bool     reverse;            // draw the chain from the far end back
    ChainOptions()
        : atomCount(1), bondLength(0.0), chainAngleDegrees(120.0), snaking(true),
          fixedLength(true), fixedAngles(true), angleStepDegrees(15.0),
          reverse(false) {}
};

struct ChainRequest {
    ChainOptions  options;
    core::Vec2    origin;
    core::ObjectId originAtom;    // kInvalidObjectId == a free origin point
    core::Vec2    pointer;
    uint32_t      modifiers;      // core::kModAlt etc.
    bool          haveDirection;
    double        defaultDirectionDeg;
    core::DocumentId documentId;
    uint64_t      documentRevision;
    ChainRequest()
        : origin(0, 0), originAtom(core::kInvalidObjectId), pointer(0, 0),
          modifiers(core::kModNone), haveDirection(false), defaultDirectionDeg(0.0),
          documentId(core::kInvalidDocumentId), documentRevision(0) {}
};

struct ChainAtom {
    core::Vec2 position;
    int        element;   // 6 (carbon) for both chain tools
    ChainAtom() : element(6) {}
};

struct ChainBond {
    int             fromNewIndex;   // -1 == the origin atom (existing or free)
    int             toNewIndex;
    core::BondOrder order;
    ChainBond() : fromNewIndex(-1), toNewIndex(0), order(core::BondOrder::Single) {}
};

struct ChainPlan {
    bool                  valid;
    std::string           rejectReason;
    std::vector<ChainAtom> atoms;      // NEW atoms, in drawing order
    std::vector<ChainBond> bonds;      // bonds[0] is origin -> atoms[0]
    core::ObjectId        originAtom;
    core::Vec2            origin;
    double                endDirectionDeg;
    bool                  lengthConstrained;
    bool                  anglesReleased;   // Alt was held: constraints released
    bool                  reversed;
    core::DocumentId      documentId;
    uint64_t              documentRevision;
    int                   pointCount;   // distinct positions produced
    ChainPlan()
        : valid(false), originAtom(core::kInvalidObjectId), origin(0, 0),
          endDirectionDeg(0.0), lengthConstrained(true), anglesReleased(false),
          reversed(false), documentId(core::kInvalidDocumentId),
          documentRevision(0), pointCount(0) {}
};

// Plan a chain. PURE: reads the document, mutates nothing.
core::Error resolveChainPlan(const core::Document& document,
                             const ChainRequest& request,
                             ChainPlan* out);

// The turn the snaking chain applies after the k-th bond. k is 0-based; bond 0
// leaves the origin. Exposed because "how does it turn" is the part a reviewer
// must be able to check.
double snakingTurnDegrees(int k, double chainAngleDegrees, bool reverse);

// ---------------------------------------------------------------------------
// Sprouting from an existing atom
// ---------------------------------------------------------------------------
// The official atom-hotspot table (PDF 70-75) makes keys 0..9 environment
// dependent. T06 owns the CHEMICAL decision (what to build); T08 owns the
// DIRECTION and the PLACEMENT. The split is deliberate: T06 does not know ring
// geometry, T08 does not decide chemistry.
//
// PROJECT heuristic - the difference the task requires us to state:
//   0 neighbours -> the caller's preferred direction.
//   1 neighbour  -> 120 degrees away from the bond, on the LESS CROWDED side.
//   2 neighbours -> 120 degrees from the nearest neighbour, choosing the side
//                   that stays farther from the other bond.
//   3+ neighbours-> the largest angular GAP between consecutive neighbour
//                   directions; the new bond goes at its bisector.
//   any          -> when the largest gap is below minGapDegrees the sprout is
//                   REFUSED, because no honest direction exists.
// The guide states the 120 degree chain angle (GUIDE); it does NOT publish this
// gap heuristic, so every branch above is labelled PROJECT.
struct SproutEnvironment {
    core::Vec2             position;
    std::vector<double>    neighbourDirectionsDeg;
    double                 minGapDegrees;
    SproutEnvironment() : minGapDegrees(25.0) {}
};

struct SproutDirection {
    bool        ok;
    std::string rejectReason;
    double      directionDeg;
    bool        fromSingleNeighbour;
    double      largestGapDeg;
    int         neighbourCount;
    SproutDirection()
        : ok(false), directionDeg(0.0), fromSingleNeighbour(false),
          largestGapDeg(0.0), neighbourCount(0) {}
};

SproutDirection resolveSproutDirection(const SproutEnvironment& environment,
                                       double preferredDeg);

// Bounded one-hop neighbourhood read. This is the only place T08 walks the graph.
SproutEnvironment buildSproutEnvironment(const core::Document& document,
                                         core::ObjectId atom,
                                         double minGapDegrees);

// ---------------------------------------------------------------------------
// Fusion legality - ONE rule, owned by T06/T07, not re-invented here
// ---------------------------------------------------------------------------
// T08 does NOT contain a second valence table. Every "may this atom take another
// bond" question is answered by chemistry::implicitHydrogens() (T06) fed with a
// real AtomEnvironment built from the document.
enum class FuseReject : uint8_t {
    None = 0,
    AnchorMissing,
    OverValence,
    DuplicateEdge,
    SelfBond,
    NoGapForSpiro,
    DocumentMismatch,
    Degenerate,
    PlacementInvalid,
    NotPlaced        // cluster fingerprint disagrees with the document
};
const char* fuseRejectName(FuseReject r);

// The one call into T06: the atom as it would be AFTER extraOrder more bond
// order is added.
chemistry::HydrogenResult valenceAfterJoin(const core::Document& document,
                                           core::ObjectId atom,
                                           int extraOrder);

// The atom's current sum of incident bond orders, aromatic counted as 1 exactly
// as T06's AtomEnvironment documents. Written once here so no caller re-derives
// it with a different rounding.
int incidentBondOrderSum(const core::Document& document, core::ObjectId atom);

// ---------------------------------------------------------------------------
// Commands - the ONLY mutation entry points this module offers
// ---------------------------------------------------------------------------
namespace commands {

struct ClusterAtom {
    core::Vec2      position;
    int             element;
    core::ObjectId  reuse;
    bool            noDouble;
    ClusterAtom() : position(0, 0), element(6), reuse(core::kInvalidObjectId), noDouble(false) {}
};
struct ClusterBond {
    int             fromAtom;
    int             toAtom;
    core::ObjectId  reuse;
    core::BondOrder order;
    core::BondDisplay display;
    ClusterBond() : fromAtom(-1), toAtom(-1), reuse(core::kInvalidObjectId),
                    order(core::BondOrder::Single),
                    display(core::BondDisplay::Normal) {}
};
struct Cluster {
    std::vector<ClusterAtom> atoms;
    std::vector<ClusterBond> bonds;
    core::DocumentId         documentId;
    uint64_t                 documentRevision;
    std::string              tag;
    Cluster() : documentId(core::kInvalidDocumentId), documentRevision(0) {}
};

FuseReject validateCluster(const core::Document& document, const Cluster& cluster,
                           std::string* detail);

Cluster clusterFromRingPlacement(const RingPlacement& placement);
Cluster clusterFromChainPlan(const ChainPlan& plan);

// ONE transaction: every atom and every bond of the cluster is created (or the
// pre-existing ones reused) inside a single command, so one Undo removes the
// whole ring / chain / sprout.
core::Command createRingCommand(const RingPlacement& placement,
                                std::vector<core::ObjectId>* outAllAtoms,
                                std::vector<core::ObjectId>* outAllBonds,
                                std::string* notice = nullptr);
core::Command createChainCommand(const ChainPlan& plan,
                                 std::vector<core::ObjectId>* outAllAtoms,
                                 std::vector<core::ObjectId>* outAllBonds);
core::Command createClusterCommand(const Cluster& cluster,
                                   std::vector<core::ObjectId>* outAtoms,
                                   std::vector<core::ObjectId>* outBonds);

// Swap every bond's endpoints: the tool's "draw the other way" action, ONE step.
core::Command reverseChainCommand(const std::vector<core::ObjectId>& bonds);

} // namespace commands

// ---------------------------------------------------------------------------
// Tool integration (T04/T01 seam) - NO keyboard handling
// ---------------------------------------------------------------------------
struct RingPresetSpec {
    uint32_t        presetId;
    RingPresetId    id;
    std::string     toolId;
    std::string     command;
    std::string     displayName;
    std::string     category;
    RingTopologyClass topology;
    int             size;
    bool            implemented;
    EvidenceSource  evidence;
    std::string     note;
    RingPresetSpec() : presetId(0), id(RingPresetId::Cyclopropane),
                       topology(RingTopologyClass::Flat), size(0),
                       implemented(false), evidence(EvidenceSource::Uncited) {}
};

struct ChainPresetSpec {
    uint32_t        presetId;
    ChainPresetId   id;
    std::string     toolId;
    std::string     command;
    std::string     displayName;
    std::string     category;
    bool            snaking;
    bool            implemented;
    EvidenceSource  evidence;
    std::string     note;
    ChainPresetSpec() : presetId(0), id(ChainPresetId::Acyclic), snaking(false),
                        implemented(false), evidence(EvidenceSource::Uncited) {}
};

const std::vector<RingPresetSpec>& ringPresets();
const std::vector<ChainPresetSpec>& chainPresets();
std::vector<core::ToolPreset> ringsChainsToolPresets();

const RingPresetSpec*  ringPresetForCommand(const std::string& command);
const ChainPresetSpec* chainPresetForCommand(const std::string& command);
const RingPresetSpec*  ringPresetById(RingPresetId id);
const ChainPresetSpec* chainPresetById(ChainPresetId id);

std::vector<std::string> ringChainCommandNames();

// ---------------------------------------------------------------------------
// The placement policy bundle: everything the tool needs that is platform state
// ---------------------------------------------------------------------------
struct PlacementPolicy {
    core::ObjectId anchorAtom;      // hotspot under the pointer, or invalid
    core::ObjectId anchorBond;
    core::ObjectId anchorBond2;
    double         pointerDirectionDeg;  // direction from the anchor to the pointer
    double         rotationDeg;
    bool           rotationGiven;
    bool           spiroRequested;
    bool           reverseChain;
    int            chainAtomCount;
    double         bondLength;
    bool           fixedLength;
    bool           fixedAngles;
    double         angleStepDegrees;
    uint32_t       modifiers;
    PlacementPolicy()
        : anchorAtom(core::kInvalidObjectId), anchorBond(core::kInvalidObjectId),
          anchorBond2(core::kInvalidObjectId), pointerDirectionDeg(0.0), rotationDeg(0.0), rotationGiven(false),
          spiroRequested(false), reverseChain(false), chainAtomCount(1),
          bondLength(0.0), fixedLength(true), fixedAngles(true),
          angleStepDegrees(15.0), modifiers(core::kModNone) {}
};

// Build the RingRequest the tool would use for a given pointer. Exposed so the
// preview and the commit build their request through the SAME function and can
// therefore not disagree. This is the T04 "preview == commit" contract applied
// to placement.
RingRequest ringRequestFor(const core::Document& document,
                           RingPresetId preset,
                           const core::Vec2& pointer,
                           const PlacementPolicy& policy);

ChainRequest chainRequestFor(const core::Document& document,
                             const core::Vec2& origin,
                             const core::Vec2& pointer,
                             const PlacementPolicy& policy);

} // namespace rings_chains
} // namespace r62

#endif // R62_NEXT_RINGS_CHAINS_RINGS_CHAINS_HPP
