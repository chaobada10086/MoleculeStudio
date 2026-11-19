#ifndef R62_NEXT_CHEMISTRY_ELEMENT_DATA_HPP
#define R62_NEXT_CHEMISTRY_ELEMENT_DATA_HPP
// =============================================================================
// re_round62_viewer/next/chemistry/element_data.hpp
//
// T06 - PERIODIC TABLE TABLE DATA (the chemistry module's element facts).
//
// WHY THIS IS NOT core::elementSymbol()
// -----------------------------------------------------------------------------
// core/contracts.hpp supplies the SYMBOL and the NUMBER.  That is a rendering and
// parsing concern: "how do I print element 17" and "which element is Cl".  It is
// deliberately NOT a chemistry table - it does not answer "what is the usual
// valence of sulfur", "which elements are normally drawn as a label", or "what
// does the element default to when a bond changes order".
//
// This header adds the chemistry columns T06 needs:
//   * standard atomic weight          (only for the monoisotopic-mass helper;
//                                      T06 does NOT emit a mass-spec analysis)
//   * a DEFAULT valence pair          (low/high), which drives implicit hydrogen
//                                      counting for the common main-group cases
//   * a conventional-valence tag      (metals and lanthanides have no single one;
//                                      we say so instead of inventing a number)
//   * whether the element is normally shown as a text label in a skeletal
//     formula (carbon and the "organic subset" heteroatoms follow the skeleton
//     convention; everything else is normally labelled)
//   * the common ISOTOPES with exact mass, so an isotope edit has a real value to
//     round-trip rather than a bare integer nobody can check
//
// PROVENANCE AND DELIBERATE LIMITS
// -----------------------------------------------------------------------------
// The valence numbers are the conventional main-group values used by every
// introductory drawing package; they are NOT a quantum-chemistry result and they
// are NOT a claim about every oxidation state the element can adopt.  Where an
// element has no single conventional valence (transition metals, lanthanides,
// actinides, noble gases) the table records valenceLow = valenceHigh = -1 with a
// reason string, and callers MUST treat -1 as "no implicit hydrogen prediction".
// Inventing "Fe has valence 2" would silently rewrite chemistry, which the task
// statement forbids.
//
// Isotope masses are the IUPAC 2021 representative/standard values as published
// by NIST (Atomic Weights and Isotopic Compositions).  They are stored to 6
// decimals; the module rounds to 4 for display.  T06 does not compute an exact
// mass for a whole molecule unless every atom has an assigned isotope - see
// valence.hpp::exactMass() for the stated precondition.
// =============================================================================

#include <cstdint>
#include <string>

namespace r62 {
namespace chemistry {

// ---------------------------------------------------------------------------
// ElementInfo
// ---------------------------------------------------------------------------
struct ElementInfo {
    uint8_t     number;         // 1..118; 0 == the "unknown" row
    const char* symbol;         // "H", "He", ... ; "?" for 0
    const char* name;           // English name; "" for 0
    double      weight;         // standard atomic weight, 0.0 when not defined
    int8_t      valenceLow;     // common lower valence, -1 == none conventional
    int8_t      valenceHigh;    // common higher valence, -1 == none conventional
    uint8_t     isOrganicSubset;// 1 when skeletal drawing normally hides the label
    uint8_t     isMetal;        // 1 for metals (no implicit-H prediction)
    const char* valenceNote;    // why a valence is absent; "" when it is defined
};

// Lookup by atomic number.  Returns the "unknown" record (number 0) for anything
// outside 1..118 rather than a null pointer, so callers never need a null check.
const ElementInfo& elementInfo(int number);

// Lookup by exact symbol (case sensitive: "Cl" != "CL").  Returns 0 when the
// symbol is not an element.  Case sensitivity matters: ChemDraw hotkeys treat
// "c" and "Shift+c" as different labels, and a case-insensitive lookup would turn
// "CO" (cobalt) into "Co" or vice versa.
const ElementInfo* elementInfoBySymbol(const char* symbol);
const ElementInfo* elementInfoBySymbol(const char* symbol, int len);

// ---------------------------------------------------------------------------
// Organic subset
// ---------------------------------------------------------------------------
// The "organic subset" is the drawing convention, not a chemistry fact: in a
// skeletal formula, carbon is implicit and the heteroatoms B, N, O, P, S, F, Cl,
// Br, I are drawn as labels.  Everything else is always labelled.  This is what
// decides whether setElement() should also set showLabel.
bool isOrganicSubset(int number);

// True when the element has a conventional valence the implicit-hydrogen rule can
// use.  Metals, lanthanides and the noble gases return false.
bool hasConventionalValence(int number);

// ---------------------------------------------------------------------------
// Isotopes
// ---------------------------------------------------------------------------
struct IsotopeInfo {
    uint16_t massNumber;
    double   exactMass;     // unified atomic mass units
    double   abundance;     // natural abundance in percent; 0 when synthetic
};

// The isotope list for an element.  Returns 0 when the module has no table row
// for that element; *count is set to 0 in that case.  A missing row means
// "unknown to this module", never "this element has no isotopes".
const IsotopeInfo* isotopes(int number, int* count);

// Guaranteed-present convenience rows used by the acceptance tests.  These are
// real NIST values and are asserted in the test suite so a typo in the big table
// is visible.
bool knownIsotope(int number, int massNumber, IsotopeInfo* out);

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------
// Format a charge the way an atom label draws it: magnitude omitted for +/-1,
// "+"/"-" (ASCII hyphen-minus) otherwise, sign last.  Returns "" for 0.
// Example: charge 2 -> "2+", charge -1 -> "-".
std::string chargeSuffix(int charge);

// Format an isotope prefix: "13" for 13, "" for 0.
std::string isotopePrefix(int massNumber);

// ---------------------------------------------------------------------------
// Valence / hydrogen helpers that need only the table
// ---------------------------------------------------------------------------
// Conventional valence of an element at a given bond-order sum, choosing the
// lowest conventional valence that still covers the observed bond sum.  Returns
// -1 when the element has no conventional valence.  This is the rule the
// implicit-hydrogen count uses; it is NOT an aromaticity or oxidation-state
// analysis.
int conventionalValenceFor(int number, int bondOrderSum);

} // namespace chemistry
} // namespace r62

#endif // R62_NEXT_CHEMISTRY_ELEMENT_DATA_HPP
