#ifndef R62_NEXT_ADVANCED_FRAGMENTS_HPP
#define R62_NEXT_ADVANCED_FRAGMENTS_HPP
// =============================================================================
// next/advanced/fragments.hpp  --  T17 fragment tools
//
// THE FOUR MAIN-TOOLBAR COMMANDS IN SCOPE
// -----------------------------------------------------------------------------
//   chooseFragmentTool_MassFragmentation  "Mass Fragmentation [K]"
//   chooseFragmentTool_Synthesis          "Synthesis"
//   chooseFragmentTool_Retrosynthesis     "Retrosynthesis"
//   chooseStructurePerspectiveTool        (see perspective.hpp)
//
// WHAT THE NATIVE GUIDE SAYS (this is the specification, not a guess)
// -----------------------------------------------------------------------------
//   Mass Fragmentation: "you must specify what bonds are to be broken ... drag
//   the cursor across one or more bonds ... bonds that you cross are broken ...
//   the formula and exact mass for the fragments on either side of the bond
//   appear as if the bond were homolytically broken. That is, a single bond
//   turns into a monoradical on each fragment; a double bond turns into a pair
//   of diradicals. If more than one bond is crossed, all fragments on each side
//   of the line are considered together. If the only bond crossed is a ring
//   bond, a single formula/mass pair displays." It is explicitly NOT predictive.
//
//   Synthesis / Retrosynthesis: draw a synthesis reaction based on a product
//   structure, or the reverse reaction with the product on the left and an
//   arrow pointing to the reactants. This is a REACTION RULE problem: deciding
//   which reactants give a product is retrosynthetic analysis.
//
// THE DISTINCTION THIS MODULE REFUSES TO BLUR
// -----------------------------------------------------------------------------
//   * OWN-IMPLEMENTED: breaking the bonds a user explicitly crossed or listed,
//     adding the bond a user explicitly drew between two atoms, and reporting
//     the formula and mass of the resulting connected components - all from
//     T06's valence/isotope tables. Deterministic and fully tested.
//   * NATIVE-BACKED: only when a verified native port (T18) actually executed
//     the operation on a normal document copy. A port that merely exists in COM
//     metadata does NOT count; the adapter must answer verifiedFor(op).
//   * UNSUPPORTED: inferring fragments for mass spec from an unspecified
//     fragmentation pattern, and any synthesis/retrosynthesis INFERENCE. T17
//     never invents a fragment mass, a reaction path or a stabilised
//     stereocentre. With no verified native port, plan() returns Unsupported.
//
// Masses are T06 estimates: MassResult::exact is false unless every atom had an
// assigned isotope and the natural-weight fallback was used. The plan carries
// that flag rather than printing a bare number.
// =============================================================================

#include "next/advanced/common.hpp"
#include "next/advanced/perspective.hpp"
#include "next/chemistry/valence.hpp"
#include "next/core/tool_registry.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace advanced {

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------
enum class FragmentOperation : uint8_t {
    MassFragmentation = 0,   // explicit crossings -> homolytic cleavage + mass
    Synthesis,               // reaction with the bond ADDED (inference is native-only)
    Retrosynthesis,          // reaction with the bond BROKEN (inference is native-only)
    ManualCut,               // explicit bond ids removed
    ManualConnect            // explicit atom pairs joined
};
const char* fragmentOperationName(FragmentOperation op);

struct FragmentCapability {
    FragmentOperation op;
    Provenance        provenance;
    const char*       sourceCommand;   // tools.csv command, or "" for an API item
    const char*       provider;
    std::string       note;
    FragmentCapability()
        : op(FragmentOperation::ManualCut), provenance(Provenance::Unsupported),
          sourceCommand(""), provider("") {}
};

// ---------------------------------------------------------------------------
// Request
// ---------------------------------------------------------------------------
struct FragmentRequest {
    FragmentOperation op;
    core::Vec2 lineStart;      // MassFragmentation drag line (document units)
    core::Vec2 lineEnd;
    bool       keepLine;       // Alt: keep the drawn line in the document
    bool       homolytic;      // default true for mass fragmentation
    std::vector<core::ObjectId> cutBonds;         // explicit bonds
    std::vector<core::ObjectId> connectFrom;      // ManualConnect pairs
    std::vector<core::ObjectId> connectTo;
    core::BondOrder connectOrder;   // order of an explicitly added bond
    FragmentRequest()
        : op(FragmentOperation::ManualCut), lineStart(0, 0), lineEnd(0, 0),
          keepLine(false), homolytic(true), connectOrder(core::BondOrder::Single) {}
};

// ---------------------------------------------------------------------------
// Plan
// ---------------------------------------------------------------------------
struct FragmentPiece {
    std::vector<core::ObjectId> atoms;   // ascending
    std::string formula;
    chemistry::MassResult mass;
    int side;                            // 0/1 = side of the cut line, -1 unknown
    FragmentPiece() : side(-1) {}
};

struct FragmentSideSummary {
    std::string formula;
    chemistry::MassResult mass;
    int pieceCount;
    FragmentSideSummary() : pieceCount(0) {}
};

struct BondAdd {
    core::ObjectId a;
    core::ObjectId b;
    core::BondOrder order;
    BondAdd() : a(core::kInvalidObjectId), b(core::kInvalidObjectId),
                order(core::BondOrder::Single) {}
    BondAdd(core::ObjectId a_, core::ObjectId b_, core::BondOrder o)
        : a(a_), b(b_), order(o) {}
};

struct FragmentRadical {
    core::ObjectId atom;
    uint8_t        radical;    // core::Radical
    FragmentRadical() : atom(core::kInvalidObjectId), radical(0) {}
    FragmentRadical(core::ObjectId a, uint8_t r) : atom(a), radical(r) {}
};

struct FragmentPlan {
    bool          valid;
    core::Error   error;
    Provenance    provenance;
    std::string   provider;
    core::DocumentId documentId;
    uint64_t      revision;

    std::vector<core::ObjectId> bondsToRemove;
    std::vector<BondAdd>        bondsToAdd;
    std::vector<FragmentRadical> radicals;
    std::vector<FragmentPiece>  pieces;
    FragmentSideSummary         sides[2];

    int  crossedBondCount;
    bool ringBondOnly;
    bool nativeApplied;
    bool keepLine;
    std::string note;

    FragmentPlan()
        : valid(false), provenance(Provenance::Unsupported),
          documentId(0), revision(0), crossedBondCount(0), ringBondOnly(false),
          nativeApplied(false), keepLine(false) {}
};

// ---------------------------------------------------------------------------
// Native port (implemented by T18, never created here)
// ---------------------------------------------------------------------------
struct NativeFragmentOutcome {
    bool        applied;
    std::string description;
    NativeFragmentOutcome() : applied(false) {}
};

class INativeFragmentPort {
public:
    virtual ~INativeFragmentPort() {}
    virtual const char* providerName() const = 0;
    // Must return true ONLY when this operation was actually executed on a
    // normal document copy and verified. Metadata alone is not verification.
    virtual bool verifiedFor(FragmentOperation op) const = 0;
    // Run it on the host. T17 does not mutate the host through this port.
    virtual core::Error execute(FragmentOperation op, const FragmentRequest& request,
                                NativeFragmentOutcome* out) = 0;
};

// ---------------------------------------------------------------------------
// Service interface
// ---------------------------------------------------------------------------
class IFragmentService {
public:
    virtual ~IFragmentService() {}
    virtual const char* providerName() const = 0;
    virtual FragmentCapability capability(FragmentOperation op) const = 0;
    // Pure planning over the document; never mutates it.
    virtual core::Error plan(const core::Document& doc, const FragmentRequest& request,
                             FragmentPlan* out) const = 0;
    virtual std::vector<FragmentCapability> capabilities() const = 0;
};

// Built-in plan engine. An optional native port is consulted ONLY for the two
// inference operations; the explicit operations never leave this module.
class FragmentService : public IFragmentService {
public:
    explicit FragmentService(INativeFragmentPort* nativePort = 0)
        : native_(nativePort) {}
    const char* providerName() const;
    FragmentCapability capability(FragmentOperation op) const;
    core::Error plan(const core::Document& doc, const FragmentRequest& request,
                     FragmentPlan* out) const;
    std::vector<FragmentCapability> capabilities() const;
private:
    INativeFragmentPort* native_;
};

// ---------------------------------------------------------------------------
// Pure helpers (also directly testable)
// ---------------------------------------------------------------------------
// Bonds whose segment properly intersects the segment a-b. Deterministic id
// order. A bond sharing an endpoint with the line ends is still returned: the
// native tool breaks whatever the line crosses.
std::vector<core::ObjectId> bondsCrossedByLine(const core::Document& doc,
                                               const core::Vec2& a, const core::Vec2& b);

// True when every listed bond lies on a cycle (removing it keeps its endpoints
// connected).
bool ringBondOnly(const core::Document& doc,
                  const std::vector<core::ObjectId>& bonds);

// Homolytic radical for one fragment side of a cleaved bond, per the guide:
// single -> monoradical (core::Radical::Doublet), double -> diradical
// (core::Radical::Triplet). Returns core::Radical::None when the guide has
// nothing to say for the order.
uint8_t homolyticRadicalFor(core::BondOrder order);

// Hill-style formula of an atom set, using T06 for the implicit hydrogens.
// A '?' appears when T06 cannot resolve a hydrogen count; the caller must not
// treat such a formula as exact.
// plannedRadicals lets the caller account for a homolytic cleavage that is
// still only PLANNED: a monoradical carbon has one hydrogen fewer than the
// closed-shell valence rule gives, a diradical two fewer. Passing null keeps
// the ordinary closed-shell formula. The guide's "as if the bond were
// homolytically broken" mass depends on this.
std::string pieceFormula(const core::Document& doc, const std::vector<core::ObjectId>& atoms,
                         chemistry::MassResult* massOut, bool allowNaturalWeight = true,
                         const std::vector<FragmentRadical>* plannedRadicals = 0);

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------
// One transaction: removes bonds, adds bonds, sets radicals. All in one undo
// step. A native-backed plan carries no local edits and is a no-op here.
core::Command applyFragmentPlanCommand(const FragmentPlan& plan,
                                       bool enforceFreshPlan = true);

// ---------------------------------------------------------------------------
// Tool
// ---------------------------------------------------------------------------
// Drag tool shared by the mass-fragmentation and the explicit cut actions. It
// resolves the crossed bonds during the gesture (read-only), previews them, and
// commits exactly the resolved set on release. handleChar/handleKey are NOT
// overridden: keys belong to T04's keymap.
class FragmentTool : public core::Tool {
public:
    FragmentTool();

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::Select; }

    void setPreset(FragmentOperation op) { op_ = op; }
    FragmentOperation preset() const { return op_; }

    const std::vector<core::ObjectId>& pendingCrossings() const { return crossing_; }
    const std::vector<core::ObjectId>& lastCommitted() const { return committed_; }
    const std::string& status() const { return status_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

private:
    core::ToolId id_;
    FragmentOperation op_;
    core::Vec2 origin_;
    core::Vec2 current_;
    bool pressed_;
    std::vector<core::ObjectId> crossing_;
    std::vector<core::ObjectId> committed_;
    std::string status_;
};

// A service instance owned by the caller, plus a registration helper.
struct ToolRegistration {
    size_t presetsRegistered;
    size_t toolsRegistered;
    ToolRegistration() : presetsRegistered(0), toolsRegistered(0) {}
};

// Publishes the four fragment/perspective tool presets through T01's registry.
// The tool instances stay owned by the caller.
core::Error registerFragmentTools(core::ToolRegistry& registry,
                                  FragmentTool* fragmentTool,
                                  PerspectiveTool* perspectiveTool,
                                  ToolRegistration* out);

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_FRAGMENTS_HPP
