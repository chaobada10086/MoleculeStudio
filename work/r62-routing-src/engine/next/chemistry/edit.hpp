#ifndef R62_NEXT_CHEMISTRY_EDIT_HPP
#define R62_NEXT_CHEMISTRY_EDIT_HPP
// =============================================================================
// re_round62_viewer/next/chemistry/edit.hpp
//
// T06 - CHEMISTRY EDITS AS COMMANDS: element, charge, isotope, hydrogens, radical,
//       label, atomic number, and the atom-hotspot reactions.
//
// WHY EVERY EDIT IS A core::Command
// -----------------------------------------------------------------------------
// The core layer documents the rule: a mutator used outside a transaction does NOT
// participate in undo.  A chemistry edit therefore must be a Command so that it
// goes through CommandLog::execute and becomes exactly one undo step.  This module
// never calls Document::setAtomSemantics directly from an edit path.
//
// ATOMIC NUMBER AND LABEL ARE DIFFERENT FIELDS
// -----------------------------------------------------------------------------
// This is the distinction the task statement calls out explicitly.  Two independent
// pieces of state:
//   * the ELEMENT (a periodic-table number, topology identity), and
//   * the DISPLAY LABEL (a typed AtomLabel; see label.hpp).
// setElementText() writes both consistently.  setLabelOnly() writes the label and
// leaves the element alone, which is exactly what "type arbitrary text on an atom"
// must do.  An isotope edit writes neither: it writes the isotope field only.
//
// ATOMIC NUMBER DISPLAY (the ChemDraw apostrophe hotkey)
// -----------------------------------------------------------------------------
// ChemDraw draws the atomic number as a small superscript-ish token next to the
// element.  In this model that is NOT a separate field: it is DERIVED from the
// element whenever the caller asks for an atomic-number label.  Storing it twice
// would let the two disagree.  atomicNumberText() is that derivation.
//
// ATOM HOTSPOT REACTIONS: ENVIRONMENT DECIDES, AND WE SAY SO
// -----------------------------------------------------------------------------
// The verified hotkey table says atom-hotspot 1 sprouts a carbon, 2 is an
// environment-dependent carbonyl/acetyl action, 3 is environment-dependent phenyl.
// The table does NOT say "2 means double bond" - that is the BOND hotspot table.
// The reaction table below therefore resolves each key against the atom environment
// and returns a ReactionDecision that is either APPLY (with a plan) or REFUSE (with
// a reason).  A key whose chemistry is not implemented refuses instead of quietly
// doing something else, which is what "do not turn adding O into connecting a
// nearby C" requires.
//
// "DO NOT CONFUSE ADD WITH ATTACH" (the crowded-carbonyl acceptance case)
// -----------------------------------------------------------------------------
// Adding a carbonyl means: create a NEW carbon at a computed direction one bond
// length away, create a double bond from the hotspot atom to it, create an oxygen
// double-bonded to that new carbon.  It must NOT look for an existing nearby carbon
// and attach to it.  planCarbonyl() uses the atom MOST RECENTLY at the hotspot and
// never consults the spatial index; the test builds a crowded environment and
// asserts that no existing atom id appears in the created-bond list.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/chemistry/label.hpp"
#include "next/chemistry/template.hpp"
#include "next/chemistry/valence.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace chemistry {

// ---------------------------------------------------------------------------
// Attribute reads and writes
// ---------------------------------------------------------------------------
// Read the typed label of an atom, folding in the fields that live in
// AtomSemantics and in the label side table.  This is the ONE place the two
// representations are combined.
AtomLabel readAtomLabel(const core::Document& document, core::ObjectId atom,
                        const std::vector<AbbreviationEntry>& abbreviations);

// Write an element and keep the label consistent.  `showLabel` is set from the
// organic-subset convention unless the caller forces it.  Returns a Command.
core::Command setElementCommand(core::ObjectId atom, int element);

// Write ONLY the label.  The element field is untouched.  This is the command that
// must be used for arbitrary text, R groups and abbreviations, because it is the
// one that cannot change topology.
core::Command setLabelCommand(core::ObjectId atom, const AtomLabel& label);

// Write the label from raw text, interpreting it first.  An unrecognised string
// becomes an Arbitrary label; the command still succeeds and the atom keeps its
// element.  `outKind`, when non-null, receives the decided kind so a caller can
// report "preserved verbatim" without re-interpreting.
core::Command setLabelTextCommand(core::ObjectId atom, const std::string& text,
                                  const std::vector<AbbreviationEntry>& abbreviations,
                                  LabelKind* outKind);

core::Command setChargeCommand(const std::vector<core::ObjectId>& atoms, int charge);
core::Command setIsotopeCommand(const std::vector<core::ObjectId>& atoms, int isotope);
core::Command setRadicalCommand(const std::vector<core::ObjectId>& atoms, int radical);
// -1 restores the computed count; >=0 pins an explicit number of hydrogens.
core::Command setHydrogenCountCommand(const std::vector<core::ObjectId>& atoms, int count);
core::Command setShowLabelCommand(const std::vector<core::ObjectId>& atoms, bool show);

// Clear the label override so the atom draws its element again.  Equivalent to
// Backspace/Delete on an atom hotspot in the official table, which clears the LABEL
// and does not delete the atom.  Kept separate from delete for exactly that reason.
core::Command clearLabelCommand(const std::vector<core::ObjectId>& atoms);

// The atomic-number token ChemDraw shows.  Derived from the element, never stored
// twice.  Returns "" for an invalid element.
std::string atomicNumberText(int element);

// ---------------------------------------------------------------------------
// Attribute / graphic-mark binding
// ---------------------------------------------------------------------------
// T12 owns the floating charge/radical symbols: a mark is a standalone graphic that
// a user places NEAR an atom.  Whether it is BOUND to that atom (and therefore
// writes charge/radical state) or merely floating is a decision with rules, and the
// rules must be stateable and reversible.
//
// The rule this module implements (and states in RESULTS.md):
//   * A mark binds to an atom when the mark CENTRE lies within
//     bindRadius = kMarkBindFactor * bondLength of the atom position.
//   * Among candidates, the NEAREST wins; an exact tie is broken by the smaller
//     stable ObjectId, so the binding does not depend on iteration order.
//   * A mark that moves outside the radius UNBINDS.  Unbinding restores the atom
//     attribute to exactly what it was before binding, which is why binding records
//     the prior value rather than assuming zero.
//   * A second mark of the same kind on the same atom REPLACES the first; it does
//     not add.  Two plus marks are not a 2+ charge (that is a different, explicit
//     edit), and pretending otherwise would double a charge on a stray click.
struct MarkBinding {
    core::ObjectId mark;
    core::ObjectId atom;
    bool           bound;
    int            priorCharge;    // atom charge before binding
    int            priorRadical;   // atom radical before binding
    MarkBinding()
        : mark(core::kInvalidObjectId), atom(core::kInvalidObjectId),
          bound(false), priorCharge(0), priorRadical(0) {}
};

// The bind radius factor.  This is OUR usability constant, not an official value;
// SOURCES_AND_LIMITS.md warns against presenting such a number as a ChemDraw
// constant, so it is named and documented as a project value.
const double kMarkBindFactor = 0.75;

// Resolve which atom a charged/radical mark binds to, given candidate atoms and
// their positions.  Returns kInvalidObjectId when none is within the radius.
core::ObjectId resolveMarkBinding(const core::Vec2& markPosition,
                                  const std::vector<core::ObjectId>& candidates,
                                  const std::vector<core::Vec2>& candidatePositions,
                                  double bondLength, double* outDistance);

// ---------------------------------------------------------------------------
// Incremental label / hydrogen refresh after a BOND change
// ---------------------------------------------------------------------------
// T07 owns bond edits.  When a bond order changes, the atoms at its TWO endpoints
// are the only ones whose implicit hydrogen count and label can change.  This
// function returns exactly that list, so the caller refreshes two atoms and not the
// document.  The acceptance test asserts the returned size is 2 for a normal bond
// and that no other atom id is present.
std::vector<core::ObjectId> atomsAffectedByBond(core::ObjectId begin,
                                                core::ObjectId end);

// Recompute the derived label/hydrogen fields for one atom from its environment.
// Returns the number of fields that actually changed, so "only the relevant labels
// were updated" is a measured number.  The caller builds the Command from the
// result, so this function does not mutate.
struct RefreshResult {
    int  changedFields;
    int  newHydrogenCount;
    bool overCoordinated;
    std::vector<ChemicalWarning> warnings;
    RefreshResult() : changedFields(0), newHydrogenCount(-1), overCoordinated(false) {}
};

RefreshResult refreshAtomDerived(const core::Document& document, core::ObjectId atom,
                                 const std::vector<AbbreviationEntry>& abbreviations);

// Build a bond-order change followed by the endpoint refreshes, all inside the ONE
// transaction the caller already has open.  Convenience for T07 so it does not have
// to know the refresh rule.
core::Command setBondOrderWithRefreshCommand(core::ObjectId bond,
                                             core::BondOrder order);

// ---------------------------------------------------------------------------
// Atom hotspot reactions
// ---------------------------------------------------------------------------
// ReactionKind enumerates the resolved outcome so a caller never has to match on a
// raw key character.
enum class ReactionKind : uint8_t {
    None = 0,
    AddCarbon,          // hotspot 1: sprout a single bond to a new carbon
    AddCarbonyl,        // hotspot 2: new C, C=O, attached to the hotspot
    AddPhenyl,          // hotspot 3 / a: sprout a benzene ring
    AddMethyl,          // hotspot 9: dimethyl-style single methyl
    AddOxygen,          // element keys o/q: set the hotspot atom to O
    SetElement,         // an element hotkey
    SetLabel,           // an abbreviation hotkey (Ph, Ac, Me, ...)
    SelectHotspot,      // g
    EditLabel,          // Enter
    ClearLabel,         // Backspace / Delete
    AtomicNumber,       // apostrophe: show the atomic number
    ChargePositive,     // +
    ChargeNegative      // -
};

const char* reactionKindName(ReactionKind kind);

// The atom environment a reaction resolves against.  Supplied by the caller from
// the document and the current hotspot, so this module holds no document pointer
// and the resolver is unit-testable.
struct HotspotContext {
    core::ObjectId atom;
    int    element;
    int    charge;
    int    radical;
    int    bondCount;
    int    bondOrderSum;
    bool   hasLabelOverride;
    double directionDeg;     // the direction a new group should grow
    double bondLength;
    bool   shiftHeld;
    HotspotContext()
        : atom(core::kInvalidObjectId), element(6), charge(0), radical(0),
          bondCount(0), bondOrderSum(0), hasLabelOverride(false),
          directionDeg(0.0), bondLength(14.4), shiftHeld(false) {}
};

struct ReactionDecision {
    ReactionKind kind;
    bool         apply;          // false == refuse, and `reason` says why
    std::string  reason;
    int          newElement;     // for SetElement / AddOxygen
    std::string  labelText;      // for SetLabel / AddPhenyl
    int          chargeDelta;    // for ChargePositive / ChargeNegative
    HotspotContext context;
    ReactionDecision()
        : kind(ReactionKind::None), apply(false), newElement(0),
          chargeDelta(0) {}
};

// Resolve a key on an ATOM hotspot.  `key` is the character as typed, CASE
// PRESERVED, because the verified table makes c != Shift+c and b != Shift+b.
ReactionDecision resolveAtomHotkey(char key, const HotspotContext& context,
                                   const ITemplateService& templates);

// ---------------------------------------------------------------------------
// Sprout plans (pure geometry, no document access)
// ---------------------------------------------------------------------------
struct SproutAtom {
    int        element;
    std::string labelText;
    core::Vec2 position;
    int        charge;
    SproutAtom() : element(6), charge(0) {}
};

struct SproutBond {
    core::ObjectId fromExisting;   // the hotspot atom
    int            toNewIndex;     // index into SproutAtom list
    core::BondOrder order;
    SproutBond() : fromExisting(core::kInvalidObjectId), toNewIndex(0),
                  order(core::BondOrder::Single) {}
};

struct SproutPlan {
    std::vector<SproutAtom> atoms;
    std::vector<SproutBond> bonds;    // bonds from the existing atom to new atoms
    std::vector<SproutBond> extraBonds; // bonds between NEW atoms (rings)
    bool        ok;
    std::string error;
    SproutPlan() : ok(false) {}
};

// Plan a single new carbon one bond length along directionDeg from the hotspot.
SproutPlan planAddCarbon(const core::Vec2& hotspotPosition, double directionDeg,
                         double bondLength);

// Plan a carbonyl: a NEW carbon one bond length out, a double bond hotspot->C, and
// an oxygen double-bonded to that new carbon, placed so it does not overlap.
// NEVER searches for an existing neighbour.
SproutPlan planCarbonyl(const core::Vec2& hotspotPosition, double directionDeg,
                        double bondLength);

// Plan a phenyl: a new six-membered carbocycle attached by one single bond.
SproutPlan planPhenyl(const core::Vec2& hotspotPosition, double directionDeg,
                      double bondLength);

} // namespace chemistry
} // namespace r62

#endif // R62_NEXT_CHEMISTRY_EDIT_HPP
