#ifndef R62_NEXT_TEMPLATES_QUERIES_ENUMERATE_HPP
#define R62_NEXT_TEMPLATES_QUERIES_ENUMERATE_HPP
// =============================================================================
// re_round62_viewer/next/templates_queries/enumerate.hpp
//
// T13 - QUERY ENUMERATION, WITH EXPLICIT REFUSAL.
//
// -----------------------------------------------------------------------------
// THE ORDER THE TASK DEMANDS
// -----------------------------------------------------------------------------
// "Query-structure enumeration is a different stage: first save, edit and display
//  the query semantics reliably, then enumerate within a range that has rules and
//  tests.  R = any carbon must NOT be treated as a legal expansion result."
//
// So this module is deliberately NOT a combinatorial expander.  It is a RULE
// ENGINE that answers one question: "given this query structure, which expansions
// are honest?" and refuses everything else with a reason the user can read.
//
// -----------------------------------------------------------------------------
// WHAT IS HONESTLY ENUMERABLE (each with a documented basis)
// -----------------------------------------------------------------------------
//   1. ELEMENT LISTS.  "[Cl, Br, I]" at one site means exactly three structures,
//      one per element (guide PDF 327).  FINITE and EXACT.  An element NOT list is
//      NOT enumerable: its complement is unbounded, so it is refused by name.
//   2. ALTERNATIVE GROUPS with a finite fragment list.  "R1 = {Me, Et, Ph}" means
//      exactly as many structures as the product over all alternative-group sites
//      (guide PDF 334-338).  FINITE and EXACT, subject to the cap below.
//   3. FREE SITES are NOT enumerable.  They cap the number of substituents in a
//      HIT; they do not name alternatives.  A free site expands to nothing, so
//      enumerating it would be inventing structures.  REFUSED, with the reason.
//   4. R GROUPS WITH NO DEFINITION are NOT enumerable.  This is the "R = any
//      carbon" case the task names: an undefined variable has no finite image.  The
//      only honest outcomes are "refuse" or "expand against a user-supplied
//      definition", and the latter is exactly case 2.
//
// -----------------------------------------------------------------------------
// WHAT IS COMMONLY EXPECTED AND DELIBERATELY ABSENT
// -----------------------------------------------------------------------------
// ChemDraw has an "Expand Generic Structure" tool limited to 500 structures and an
// "Expand Generic Structure To SDF" variant with no imposed limit (guide PDF 340,
// 343-344).  This module does NOT implement the SDF path, does not write SDF, and
// does not perform any 3D or valence-aware "cleanup" of the results.  What it does
// is produce the COMBINATION LIST and the placement plan for each combination, with
// a hard cap and a per-request refusal reason.  The results are documents built
// from the SAME instantiation service as a manual paste, so an enumerated structure
// and a pasted structure cannot diverge.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/core/contracts.hpp"
#include "next/core/document.hpp"
#include "next/templates_queries/catalog.hpp"
#include "next/templates_queries/query.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace templates_queries {

// The cap.  ChemDraw's own generic-structure tool is documented as "limited to 500
// generated structures" (guide PDF 340); we adopt the same number so a user who
// knows that limit is not surprised, and we name it as ADOPTED rather than derived.
enum { kMaxEnumerations = 500 };

// ---------------------------------------------------------------------------
// Why an expansion cannot be produced
// ---------------------------------------------------------------------------
enum class RefusalReason : uint8_t {
    None = 0,
    UndefinedRGroup,       // an R site with no alternative-group definition
    FreeSiteNotEnumerable, // a free site caps substituents; it names no alternative
    NegatedElementList,    // "NOT ..." has an unbounded complement
    NoEnumerableSite,      // the structure contains no enumerable query site at all
    CombinationOverflow,   // the product exceeds kMaxEnumerations
    InvalidDefinition,     // an alternative group that fails validateAlternativeGroup
    AnonymousTextOnly,     // an anonymous group whose entries are not catalog ids
    EmptyStructure         // no atoms to expand
};

const char* refusalReasonName(RefusalReason r);

// The result of asking "can this be enumerated?".  When ok is true, `combinations`
// holds one entry per generated structure and each entry names, for every site, the
// chosen value.  Nothing is mutated by this call: the caller decides whether to
// build the documents.
struct EnumerationSite {
    core::ObjectId        atom;            // the atom carrying the query
    std::string           definitionName;  // the alternative group this site refers to
    std::vector<int>      elementChoices;  // element-list case
    bool                  hasDeuterium;
    bool                  hasTritium;
    std::vector<std::string> fragmentChoices;  // alternative-group case (template ids)
    // Free-site markers are carried so the planner can NAME the reason for a refusal
    // rather than reporting a generic "no choices".
    bool                  isFreeSite;
    bool                  isUndefinedRGroup;
    EnumerationSite() : atom(core::kInvalidObjectId), hasDeuterium(false),
                         hasTritium(false), isFreeSite(false),
                         isUndefinedRGroup(false) {}
    size_t choiceCount() const {
        return elementChoices.size() + (hasDeuterium ? 1u : 0u) +
               (hasTritium ? 1u : 0u) + fragmentChoices.size();
    }
};

// One resolved combination: for each site, the index of the chosen value.
struct Combination {
    std::vector<size_t> choiceIndex;   // parallel to EnumerationResult::sites
    Combination() {}
};

struct EnumerationResult {
    bool                        ok;
    RefusalReason               reason;
    std::string                 message;   // human-readable detail; never parsed
    std::vector<EnumerationSite> sites;
    std::vector<Combination>    combinations;
    EnumerationResult() : ok(false), reason(RefusalReason::None) {}
};

// ---------------------------------------------------------------------------
// The request
// ---------------------------------------------------------------------------
// The caller supplies the sites it found (which atoms carry which query), because
// finding them is a document walk and this module does not hold a Document beyond
// the validation one.  The alternative-group definitions are supplied by name so an
// R site and its definition are linked by a value the user can see.
struct EnumerationRequest {
    std::vector<EnumerationSite>          sites;
    std::vector<AlternativeGroup>         definitions;
    int                                   cap;   // 0 == kMaxEnumerations
    EnumerationRequest() : cap(0) {}
};

// Compute the combination list, or refuse.  This function performs NO document
// mutation and NO catalog instantiation; it is pure logic over the request, which
// is why every refusal rule is directly unit-testable.
EnumerationResult planEnumeration(const TemplateRegistry& registry,
                                  const EnumerationRequest& request);

// ---------------------------------------------------------------------------
// Applying one combination
// ---------------------------------------------------------------------------
// One combination becomes a list of EDIT INTENTS, not a new document.  The caller
// (the tool layer) decides whether to build a scratch document, a new page, or an
// SDF; this module does not pick for it.  Every intent is expressed with the SAME
// catalog + instantiate vocabulary a manual paste uses, which is the structural
// guarantee that enumeration and pasting cannot drift apart.
struct EditIntent {
    enum Kind { ReplaceElement, InstantiateFragment };
    Kind              kind;
    core::ObjectId    atom;        // the site atom
    int               element;     // ReplaceElement
    std::string       templateId;  // InstantiateFragment
    EditIntent() : kind(ReplaceElement), atom(core::kInvalidObjectId), element(6) {}
};

core::Error applyCombination(const TemplateRegistry& registry,
                            const EnumerationResult& result,
                            size_t combinationIndex,
                            std::vector<EditIntent>* out);

} // namespace templates_queries
} // namespace r62

#endif // R62_NEXT_TEMPLATES_QUERIES_ENUMERATE_HPP
