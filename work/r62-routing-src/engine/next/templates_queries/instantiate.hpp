#ifndef R62_NEXT_TEMPLATES_QUERIES_INSTANTIATE_HPP
#define R62_NEXT_TEMPLATES_QUERIES_INSTANTIATE_HPP
// =============================================================================
// re_round62_viewer/next/templates_queries/instantiate.hpp
//
// T13 - TEMPLATE INSTANTIATION: read, preview, place, and the T06 service bridge.
//
// -----------------------------------------------------------------------------
// THE FAILURE THIS FILE EXISTS TO PREVENT
// -----------------------------------------------------------------------------
// "Paste the same template twice and the second copy silently reuses the first
// copy's ids".  On a slot-indexed document (T01: ObjectId IS the slab index) that
// is not a theoretical risk: a naive instantiation that copies a stored template
// would copy stored ids, and the resulting bond endpoints would point at whatever
// now occupies those slots.  The acceptance criterion is therefore explicit:
//
//     "pasting one template several times does not collide ids and does not
//      reference the original template or a previous instance"
//
// The design answer: a template NEVER stores an ObjectId.  A catalog entry refers
// to its own atoms by LOCAL INDEX only, and instantiation produces a fresh id for
// every atom and bond through Document::addAtom/addBond, so the new ids are a
// property of the target document and can never alias an earlier instance.  The
// tests assert it by instantiating twice and checking the id sets are disjoint.
//
// -----------------------------------------------------------------------------
// PLAN, THEN COMMIT - THE SAME RULE THE TOOL LAYER USES
// -----------------------------------------------------------------------------
// core/INTERFACES.md 8.4: "Preview and commit MUST use the same parse result."
// Instantiation follows it: instantiatePlan() resolves a placement into concrete
// coordinates and a per-object intent, and instantiateCommand() executes that SAME
// plan inside one transaction.  A drag preview calls the plan; the release calls
// the command with the plan the preview already produced.
//
// -----------------------------------------------------------------------------
// ANCHOR CONNECTIVITY
// -----------------------------------------------------------------------------
// Placing a template on empty canvas creates a disconnected fragment.  Placing it
// with an anchor target BONDS the template's anchor atom to an existing atom and
// the template is ROTATED so that it extends along the requested direction.  Both
// are the SAME plan, with one optional field, precisely so "connect" and "cancel"
// are one undo step apart rather than two code paths.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/core/contracts.hpp"
#include "next/core/document.hpp"
#include "next/templates_queries/catalog.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace templates_queries {

// ---------------------------------------------------------------------------
// Placement request
// ---------------------------------------------------------------------------
struct Placement {
    core::Vec2  origin;         // where the template ORIGIN lands, document units
    double      rotationDeg;    // CCW in math orientation, core::direction() convention
    double      scale;          // 1.0 == as authored; 0 is refused, not defaulted
    // Optional connectivity.  When anchorTarget is valid the template's first
    // anchor atom is bonded to it with anchorOrder, and the whole template is
    // rotated so that anchor -> (next atom) points along directionDeg.
    core::ObjectId   anchorTarget;
    core::BondOrder  anchorOrder;
    double           directionDeg;   // used only when anchorTarget is valid
    Placement()
        : rotationDeg(0.0), scale(1.0), anchorTarget(core::kInvalidObjectId),
          anchorOrder(core::BondOrder::Single), directionDeg(0.0) {}
};

// ---------------------------------------------------------------------------
// The resolved plan
// ---------------------------------------------------------------------------
// An id-free description of what will be created.  Positions are FINAL document
// coordinates, so a preview can draw exactly this and the commit can place exactly
// this.  No field here is a template-local index except atomOfBond, which is
// documented as an index into THIS plan.
struct PlannedPlacementAtom {
    int         element;
    std::string labelText;   // "" unless the template carried a label
    core::Vec2  position;
    int         charge;
    int         hydrogenCount;
    bool        aromatic;
    bool        isAnchor;    // the atom that connects to an existing structure
    int         anchorRank;  // 0 when not an anchor
    PlannedPlacementAtom()
        : element(6), charge(0), hydrogenCount(-1), aromatic(false),
          isAnchor(false), anchorRank(0) {}
};

struct PlannedPlacementBond {
    int               fromLocal;   // index into PlannedPlacement::atoms, or -1 for the
    int               toLocal;     // existing anchorTarget atom
    core::BondOrder   order;
    core::BondDisplay display;
    core::BondSemantics semantics;
    PlannedPlacementBond()
        : fromLocal(-1), toLocal(0), order(core::BondOrder::Single),
          display(core::BondDisplay::Normal),
          semantics(core::BondSemantics::Normal) {}
};

struct PlacementPlan {
    std::string                       templateId;
    std::vector<PlannedPlacementAtom> atoms;
    std::vector<PlannedPlacementBond> bonds;   // template-internal AND the anchor bond
    core::Box                         bounds;  // of the placed atoms, for framing/preview
    bool                              ok;
    std::string                       error;
    PlacementPlan() : ok(false) {}
};

// Build a plan.  Pure: reads the registry and nothing else, mutates nothing.
// Refusals (ok == false, error set, atoms empty):
//   * unknown template id
//   * scale <= 0 or not finite, non-finite origin/rotation/direction
//   * anchorTarget supplied but not a live atom in `document`
//   * anchorTarget supplied but the template has no atom to grow FROM in a
//     direction (a single-atom template still connects; the direction is then
//     ignored rather than invented)
//   * anchorTarget already bonded to the target with an incompatible bond: the
//     caller must undo/erase first.  This function does NOT silently replace it.
PlacementPlan planPlacement(const core::Document& document,
                            const TemplateRegistry& registry,
                            const std::string& templateId,
                            const Placement& placement);

// Execute a plan as ONE transaction (one undo step).  Returns the created atom
// ids in plan order and the created bond ids in plan order, so a test can assert
// "disjoint from the previous instance" without guessing at slot arithmetic.
//
// ID LIFETIME (important): the id vectors are filled when the COMMAND BODY RUNS, not
// when the command is built.  Object ids are a property of the target document and
// cannot exist before the edit, so a caller that needs the ids must read them after
// CommandLog::execute succeeds.  The PLAN, by contrast, is complete before execution
// and is what a preview draws.
core::Error instantiateCommand(const PlacementPlan& plan,
                               core::ObjectId anchorTarget,
                               core::Command* outCommand,
                               std::vector<core::ObjectId>* createdAtoms,
                               std::vector<core::ObjectId>* createdBonds);

// Convenience: plan + command in one call.  The plan is returned too, because the
// caller that wants a preview needs it and recomputing it would violate the
// "preview and commit share one parse" rule.
core::Error instantiateTemplate(const core::Document& document,
                                const TemplateRegistry& registry,
                                const std::string& templateId,
                                const Placement& placement,
                                core::Command* outCommand,
                                PlacementPlan* outPlan,
                                std::vector<core::ObjectId>* createdAtoms,
                                std::vector<core::ObjectId>* createdBonds);

// ---------------------------------------------------------------------------
// WHERE DOES A NEW BOND GO - the geometry the anchor bond uses
// ---------------------------------------------------------------------------
// The anchor bond length is the DOCUMENT default bond length, not the template's
// internal spacing, because the new bond connects to the user's structure and must
// match it.  This helper is exposed so the preview draws the same length the commit
// uses; a mismatch here is the classic "the preview looked right and the result did
// not" defect.
core::Vec2 anchorBondEndpoint(const core::Vec2& target, double directionDeg,
                              double bondLength);

// ---------------------------------------------------------------------------
// T06 BRIDGE: the template service T06 injects
// ---------------------------------------------------------------------------
// chemistry/template.hpp declares ITemplateService and expects T13 to provide a
// concrete implementation, with BuiltinAbbreviationTable as the stopgap.  This is
// that implementation, and it is a THIN ADAPTER: it does not hold a second copy of
// the data, it reads the same TemplateRegistry the picker reads.
//
// The direction of the conversion is one way and deliberate: the catalog is richer
// (it can express aromaticity, charges, labels, multiple anchors) and
// chemistry::GroupTemplate is the subset the expand/contract path needs.  A catalog
// entry that cannot be expressed as a GroupTemplate (for example a bare ring with
// no abbreviation) is simply NOT offered to T06, which is the honest behaviour:
// T06 asks about ABBREVIATIONS, not about every drawing in the library.
struct ServiceStats {
    size_t abbreviationCount;
    size_t catalogEntryCount;
    size_t unmappedEntries;    // catalog entries with no abbreviation, so T06 sees fewer
    ServiceStats() : abbreviationCount(0), catalogEntryCount(0), unmappedEntries(0) {}
};

// Fill `outAbbreviations` with (abbreviation, templateId) pairs for every catalog
// entry that declares a meta["abbreviation"].  Returned in registry order.  This is
// the bridge that makes "T06 and the picker share one id namespace" checkable: a
// test asserts every abbreviation T06 ships resolves to a catalog entry id.
std::vector<std::pair<std::string, std::string> >
abbreviationIndex(const TemplateRegistry& registry);

ServiceStats serviceStats(const TemplateRegistry& registry);

} // namespace templates_queries
} // namespace r62

#endif // R62_NEXT_TEMPLATES_QUERIES_INSTANTIATE_HPP
