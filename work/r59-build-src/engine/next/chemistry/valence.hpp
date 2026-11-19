#ifndef R62_NEXT_CHEMISTRY_VALENCE_HPP
#define R62_NEXT_CHEMISTRY_VALENCE_HPP
// =============================================================================
// re_round62_viewer/next/chemistry/valence.hpp
//
// T06 - IMPLICIT HYDROGEN, BASIC VALENCE CHECKS AND CHEMICAL WARNINGS.
//
// WHAT THIS IS, AND WHAT IT IS NOT
// -----------------------------------------------------------------------------
// This is a VERIFIABLE BASIC rule set, not a chemistry engine.  It answers exactly
// these questions and no others:
//
//   * Given an atom and the sum of its incident bond orders, how many hydrogens are
//     implied by the conventional valence?
//   * Is the atom over-coordinated (bond order sum above the highest conventional
//     valence)?
//   * Is a charge/radical/isotope combination one this module can explain?
//   * Does a label describe a topology change or only a display change?
//
// It deliberately does NOT output:
//   * an aromaticity perception  (needs a ring/electron model and a decision about
//     which of the many aromaticity models is meant),
//   * a CIP / R,S stereochemistry assignment (needs full graph ranking),
//   * an authoritative exact mass or elemental analysis (needs every atom, including
//     the implicit hydrogens and their isotopes, resolved - see exactMass()),
//   * a total-valence or oxidation-state analysis.
//
// The task statement says these must not be presented as authoritative when they
// are not reliably implemented.  The right shape for that is: the functions do not
// exist, or they exist behind a name that says "estimate" and return a confidence
// flag.  This file takes the second route for mass and the first for CIP/aromaticity.
//
// WARNINGS NEVER BLOCK THE VIEW
// -----------------------------------------------------------------------------
// A chemical warning is a DIAGNOSTIC ATTACHED TO AN ATOM, never a reason to refuse
// a view or a pan or a selection.  ChemicalWarning is a plain value; the caller may
// draw it, log it, or ignore it.  Edit commands in edit.hpp are the only place a
// warning can cause a refusal, and only for the specific case where accepting the
// edit would destroy topology the user did not ask to destroy (see edit.hpp).
//
// The warning computation is INCREMENTAL: warningsForAtom() takes one atom and its
// local environment.  A caller changing one bond is expected to recompute warnings
// for the two endpoint atoms only, not for the document.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/chemistry/element_data.hpp"
#include "next/chemistry/label.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace chemistry {

// ---------------------------------------------------------------------------
// Local atom environment: everything the rules need, and nothing else
// ---------------------------------------------------------------------------
// Passing a Document would make the rules untestable in isolation and would tempt
// them to walk the whole graph.  This struct is the bounded neighbourhood.
struct AtomEnvironment {
    int    element;
    int    charge;
    int    radical;             // core::Radical as int
    int    isotope;
    int    explicitHydrogens;   // -1 == none set
    int    bondOrderSum;        // sum of incident bond orders (aromatic counted as 1)
    int    bondCount;           // number of incident bonds
    int    aromaticBondCount;   // incident bonds flagged aromatic
    bool   hasLabelOverride;    // an arbitrary/abbreviation/rgroup label is present
    LabelKind labelKind;
    AtomEnvironment()
        : element(6), charge(0), radical(0), isotope(0), explicitHydrogens(-1),
          bondOrderSum(0), bondCount(0), aromaticBondCount(0),
          hasLabelOverride(false), labelKind(LabelKind::None) {}
};

// ---------------------------------------------------------------------------
// Implicit hydrogen
// ---------------------------------------------------------------------------
// Result of applying the conventional-valence rule.  `valid` is false when the
// rule has nothing to say (no conventional valence for the element, or a label
// override made the atom not-an-element); `count` is -1 in that case.
//
// The rule, stated so a reviewer can check it:
//   expected = conventionalValence(element, bondOrderSum) + charge
//   count    = max(0, expected - bondOrderSum - bondCountOfExplicitH)
// Aromatic bonds count as order 1 for this purpose, which UNDERCOUNTS the
// hydrogens on an aromatic carbon that was drawn with alternating single/double
// bonds.  That is stated in the warning so the number is never presented as fact
// for an aromatic system.
struct HydrogenResult {
    int    count;           // implied hydrogens; -1 when not computable
    bool   valid;           // false == the rule does not apply
    int    conventionalValence;  // -1 when none
    bool   overCoordinated; // bondOrderSum above the conventional maximum
    bool   aromaticApproximation; // count may be off for an aromatic system
    std::string note;
    HydrogenResult()
        : count(-1), valid(false), conventionalValence(-1),
          overCoordinated(false), aromaticApproximation(false) {}
};

HydrogenResult implicitHydrogens(const AtomEnvironment& env);

// Convenience overload for the common case of an element and a plain bond sum.
HydrogenResult implicitHydrogensSimple(int element, int charge, int bondOrderSum,
                                       bool aromatic);

// ---------------------------------------------------------------------------
// Chemical warnings
// ---------------------------------------------------------------------------
// Codes are stable integers so a test asserts a code, never message text.
enum class WarningCode : uint8_t {
    None = 0,
    OverValence,          // more bonds than any conventional valence allows
    RadicalAndCharge,     // an unusual combination worth showing
    ImplicitHUnavailable, // element has no conventional valence to count from
    LabelNotInterpreted,  // an arbitrary label: display override only
    ZeroValenceWithBonds, // a noble gas or metal drawn with bonds
    IsotopeUnknown,       // the requested isotope is not in this module table
    AromaticOrderOnLabel  // an aromatic bond meets an arbitrary label
};

const char* warningCodeName(WarningCode code);

struct ChemicalWarning {
    WarningCode code;
    int         atom;        // ObjectId as int, or -1 when not atom-specific
    std::string detail;
    ChemicalWarning() : code(WarningCode::None), atom(-1) {}
    ChemicalWarning(WarningCode c, int a, const std::string& d)
        : code(c), atom(a), detail(d) {}
};

// Warnings for ONE atom.  Never walks the document.
std::vector<ChemicalWarning> warningsForAtom(const AtomEnvironment& env, int atomId);

// ---------------------------------------------------------------------------
// Mass - explicitly an ESTIMATE unless every atom is resolved
// ---------------------------------------------------------------------------
// exactMass() sums isotope exact masses.  It returns ok=false when any atom has no
// isotope assigned AND no natural-weight fallback was requested, because a "mass"
// computed from natural weights is an AVERAGE mass, not an exact mass, and calling
// it exact would be the kind of overclaim the task forbids.
struct MassAtom {
    int element;
    int isotope;        // 0 == natural
    int hydrogenCount;  // implied + explicit; the caller must have already applied
                        // the implicit-hydrogen rule, so the mass is not silently
                        // missing the hydrogens
    MassAtom() : element(6), isotope(0), hydrogenCount(0) {}
    MassAtom(int e, int i, int h) : element(e), isotope(i), hydrogenCount(h) {}
};

struct MassResult {
    double value;           // u
    bool   exact;           // true == every atom used an assigned isotope mass
    bool   ok;              // false == at least one atom could not be resolved
    int    unresolvedAtoms;
    std::string note;
    MassResult() : value(0.0), exact(false), ok(false), unresolvedAtoms(0) {}
};

MassResult estimateMass(const std::vector<MassAtom>& atoms, bool allowNaturalWeight);

// ---------------------------------------------------------------------------
// Charge / radical sanity helpers used by the commands
// ---------------------------------------------------------------------------
bool validChargeValue(int charge);
bool validRadicalValue(int radical);
// True when the radical is a real electron-counting radical this module supports.
bool radicalSupported(int radical);

// ---------------------------------------------------------------------------
// Valence-table exposure for evidence
// ---------------------------------------------------------------------------
// Human-readable one-line summary of the rule applied to an atom, used by the
// demo output so the numbers in a report can be traced to the table.
std::string describeValence(int element, int bondOrderSum, int charge);

} // namespace chemistry
} // namespace r62

#endif // R62_NEXT_CHEMISTRY_VALENCE_HPP
