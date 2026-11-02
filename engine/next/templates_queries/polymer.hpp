#ifndef R62_NEXT_TEMPLATES_QUERIES_POLYMER_HPP
#define R62_NEXT_TEMPLATES_QUERIES_POLYMER_HPP
// =============================================================================
// re_round62_viewer/next/templates_queries/polymer.hpp
//
// T13 - POLYMER REPEAT UNITS: bracket USAGE, repeat count, connectivity and
//        molecular-weight metadata.
//
// -----------------------------------------------------------------------------
// THE BOUNDARY WITH T11
// -----------------------------------------------------------------------------
// next/graphics/geometry.hpp says it in one sentence:
//
//   "Chemistry business meaning (repeat counts, molecular weight, containership)
//    belongs to T13 and is NOT modelled here."
//
// So this file owns the MEANING and T11 owns the SHAPE.  A polymer bracket here is
// a reference to a T11 bracket object (or a bracket frame the caller will create)
// plus the semantics.  The geometry of the brackets is produced by
// graphics::buildBracketGeometry(); this module never re-implements it.  That is
// what makes "bracket geometry comes from T11" a structural fact rather than a
// promise.
//
// -----------------------------------------------------------------------------
// WHY A LOOK-ALIKE TEXT STICKER IS NOT ACCEPTABLE (the task says so explicitly)
// -----------------------------------------------------------------------------
// "不做只看起来像聚合物的普通文字贴图."  A caption reading "(CH2CH2)n" is a text
// object: it has no repeat unit, no crossing bonds, no bracket usage, and nothing
// that can be edited as a polymer.  The distinction is made concrete here by
// storing, for every polymer bracket:
//
//   * a USAGE from the guide's closed set (SRU, monomer, mer, component,
//     copolymer alt/ran/blk, crosslink, graft, modification, mixture mix/f,
//     multiple group, parameterizable),
//   * a REPEAT COUNT (a positive integer) or an open/parametric label,
//   * a LABEL (the "n", "c1", "c2" text the guide lets the user edit),
//   * the CROSSING BONDS (which bonds pass through each bracket side), which is
//     what makes crossing bonds "match up" (guide PDF 329),
//   * a REPEAT PATTERN (head-to-tail / head-to-head / either / unknown),
//   * a FLIP TYPE for ladder polymers,
//   * OPTIONAL molecular-weight metadata (Mn / Mw) with an explicit unit.
//
// A text object cannot carry any of that, so "did we model a polymer or a picture?"
// has a mechanical answer.
//
// -----------------------------------------------------------------------------
// SOURCES (bundled ChemDraw User Guide, PDF reader pages)
// -----------------------------------------------------------------------------
//   * Bracket tools and Bracket Properties dialog, PDF 328-329.
//   * Bracket Usage set, PDF 329-331: SRU (n); Crosslink (xl); Graft (grf);
//     Modification (mod); Monomer (mon); Mer (mer); Component (c); Copolymer
//     alternating (alt), random (ran), block (blk); Mixture unordered (mix),
//     ordered (f); Multiple Group (#); Parameterizable (#/# - #/MW).
//   * "the component order must be a non-negative integer, and is displayed as the
//     letter c followed by an integer (or c alone if an order of zero is
//     specified)" (p.329).
//   * "the repeat count must be a positive integer" (p.329) - multiple group only.
//   * "the SRU label can be any text" (p.329).
//   * Repeat Pattern, PDF 331: head-to-tail is the default; head-to-head and a
//     mixture/unknown are the other options.
//   * Flip Type, PDF 332-333: "only for brackets with exactly two crossing bonds".
//   * Link nodes, PDF 333: "Link nodes must have exactly two connecting bonds." and
//     the label's last part is "number hyphen number" with a subscript.
//   * Multiple group brackets "represent a specific repeat count, [so] you can
//     display the molecular weights (and related data) for the enclosed
//     structures in the Analysis window" (p.331) - the basis for the MW metadata.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/core/contracts.hpp"
#include "next/core/document.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace templates_queries {

// ---------------------------------------------------------------------------
// Bracket usage
// ---------------------------------------------------------------------------
// The guide's set, with the shorthand the guide prints.  Parameterized is kept
// separate from MultipleGroup because its count is a RANGE with an optional MW
// label (the guide calls it "Parameterizable (#/# - #/MW)").
enum class BracketUsage : uint8_t {
    Unspecified = 0,     // the default; removes any specific assignment
    SRU = 1,             // "n"     structure-based repeating unit
    Monomer = 2,         // "mon"   source-based
    Mer = 3,             // "mer"
    Component = 4,       // "c"     component order; "c1", "c2" when numbered
    CopolymerAlternating = 5,  // "alt"
    CopolymerRandom = 6,       // "ran"
    CopolymerBlock = 7,        // "blk"
    Crosslink = 8,       // "xl"
    Graft = 9,           // "grf"
    Modification = 10,   // "mod"
    MixtureUnordered = 11,     // "mix"
    MixtureOrdered = 12,       // "f"
    MultipleGroup = 13,        // "#"  a known repeat count
    Parameterized = 14   // "#/# - #/MW"  a range and/or a molecular weight
};

const char* bracketUsageName(BracketUsage u);       // stable token, for persistence
bool bracketUsageFromName(const std::string& n, BracketUsage* out);
const char* bracketUsageLabel(BracketUsage u);      // the subscript the guide prints
bool bracketUsageIsStructureBased(BracketUsage u);  // crossing bonds expected
bool bracketUsageIsSourceBased(BracketUsage u);
bool bracketUsageIsMixture(BracketUsage u);
// Which usages REQUIRE a repeat count, and which require a component order.  Used
// by validatePolymerBracket so the rules live in one place.
bool bracketUsageNeedsRepeatCount(BracketUsage u);
bool bracketUsageNeedsComponentOrder(BracketUsage u);

// ---------------------------------------------------------------------------
// Repeat pattern
// ---------------------------------------------------------------------------
// Guide PDF 331: head-to-tail is the default.  "Either" is our name for the
// guide's "their repeat pattern might be a mixture or unknown" case; it is a
// deliberate, documented value rather than an overload of HeadToTail.
enum class RepeatPattern : uint8_t {
    HeadToTail = 0,   // default
    HeadToHead = 1,
    Either = 2,       // mixture / unknown
    Undefined = 3     // not applicable for this usage
};
const char* repeatPatternName(RepeatPattern p);
bool repeatPatternFromName(const std::string& n, RepeatPattern* out);

// ---------------------------------------------------------------------------
// Flip type (ladder polymers)
// ---------------------------------------------------------------------------
// Guide PDF 332-333: "For ladder polymers ... the connection order may be
// undefined. You can assign the Flip Type property to define it. This property is
// only for brackets with exactly two crossing bonds."  So FlipType is meaningful
// ONLY when crossingBonds.size() == 2; validatePolymerBracket refuses it otherwise
// rather than storing a value that has no effect.
enum class FlipType : uint8_t {
    NoFlip = 0,       // "Because of its prevalence, the No Flip type does not
                      //  receive special annotation." (guide p.333)
    Flip = 1
};
const char* flipTypeName(FlipType f);
bool flipTypeFromName(const std::string& n, FlipType* out);

// ---------------------------------------------------------------------------
// Molecular weight metadata
// ---------------------------------------------------------------------------
// Units are EXPLICIT because "number of repeat units" and "molecular weight" are
// different quantities and a bare double is how they get confused.  The guide ties
// MW display to multiple-group brackets (p.331); we also allow it on
// Parameterized, which is where the guide puts an "MW or Mn" field.
struct MolecularWeight {
    double mn;        // number-average; 0 == not specified
    double mw;        // weight-average; 0 == not specified
    bool   specified; // false == this bracket carries no MW metadata
    MolecularWeight() : mn(0.0), mw(0.0), specified(false) {}
    bool operator==(const MolecularWeight& o) const {
        return specified == o.specified && mn == o.mn && mw == o.mw;
    }
};

// ---------------------------------------------------------------------------
// Polymer bracket record
// ---------------------------------------------------------------------------
struct PolymerBracket {
    core::ObjectId   bracket;        // the T11 bracket object (or kInvalid.. when new)
    BracketUsage     usage;
    int              repeatCount;    // 1..kMaxRepeatCount; meaningful per usage
    int              componentOrder; // >= 0; "c" alone is order 0 (guide p.329)
    std::string      label;          // the subscript text; "" == derive from usage
    std::vector<core::ObjectId> crossingBonds;  // bonds crossing each bracket side
    std::vector<core::ObjectId> enclosedAtoms;  // the repeat unit contents
    RepeatPattern    pattern;
    FlipType         flip;
    MolecularWeight  weight;
    PolymerBracket()
        : bracket(core::kInvalidObjectId), usage(BracketUsage::Unspecified),
          repeatCount(1), componentOrder(0), pattern(RepeatPattern::Undefined),
          flip(FlipType::NoFlip) {}
};

// A repeat count above this is refused with a reason.  999 is enough for every
// oligomer a drawing tool legitimately places; above it the label is a range or a
// named distribution and belongs in the SRU/parametric label instead.
enum { kMaxRepeatCount = 999 };

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
// The rules, each traceable to a page above:
//   * Unspecified is a valid assignment (it REMOVES a specific one, guide p.333).
//   * MultipleGroup requires repeatCount >= 1 ("must be a positive integer").
//   * Component requires componentOrder >= 0 ("non-negative integer").
//   * Parameterized requires either a weight or a label, since its whole purpose
//     is to carry a range/MW.
//   * FlipType != NoFlip requires EXACTLY two crossing bonds.
//   * Structure-based usages (SRU and friends) are the ones where crossing bonds
//     "match up"; a structure-based usage with zero crossing bonds is refused.
//   * A crossing bond must exist in the document and must not be a duplicate.
core::Error validatePolymerBracket(const core::Document& document,
                                  const PolymerBracket& bracket);

// ---------------------------------------------------------------------------
// The subscript text the guide prints
// ---------------------------------------------------------------------------
// Derived from usage + count + order + label, so there is exactly ONE place that
// decides what "(CH2)n" shows.  An explicit label wins (the guide lets the user
// edit it; "the SRU label can be any text").
std::string polymerLabelText(const PolymerBracket& bracket);

// ---------------------------------------------------------------------------
// Link nodes
// ---------------------------------------------------------------------------
// Guide PDF 333: "You can specify a variable-length chain, or ring, by indicating
// that one of the atoms can repeat a number of times using link nodes. Link nodes
// must have exactly two connecting bonds. ... The last part of the label must be in
// the form: number hyphen number, with the numbers and hyphen subscripted."
struct LinkNode {
    core::ObjectId  atom;      // must have exactly TWO connecting bonds
    int             low;       // >= 1; the guide's notation has no "0-3"
    int             high;      // >= low
    std::string     head;      // the part before the numbers, e.g. "CH2", "" for bare
    LinkNode() : atom(core::kInvalidObjectId), low(1), high(1) {}
    bool operator==(const LinkNode& o) const {
        return atom == o.atom && low == o.low && high == o.high && head == o.head;
    }
};

// Parse the guide's form, with optional brackets/parens/braces:
//   "[CH2]3-7"  ->  head "CH2", low 3, high 7
//   "O1-3"      ->  head "O",   low 1, high 3
//   "[6]4-4"    ->  head "",    low 4, high 4
// Refuses: no range, low < 1, high < low, non-numeric range, a range above
// kMaxRepeatCount, and a head that is not "a generic name, or no more than one
// element symbol" (guide p.333).
bool parseLinkNodeLabel(const std::string& text, LinkNode* out, std::string* reason);
std::string formatLinkNodeLabel(const LinkNode& node);

// Validate against the document: the atom must exist and have EXACTLY two
// connecting bonds.  Guide: "Link nodes must have exactly two connecting bonds."
core::Error validateLinkNode(const core::Document& document, const LinkNode& node);

// How many bonds an atom currently has.  Counted from the live adjacency, so a
// deleted bond does not inflate it.
int connectingBondCount(const core::Document& document, core::ObjectId atom);

// ---------------------------------------------------------------------------
// Persistence: a "k=v;k=v" text record, the same choice T11 made
// ---------------------------------------------------------------------------
// Unknown keys are ignored on read and `ok` is false on a malformed record, so a
// corrupt document cannot produce a half-configured polymer.  This encoder is the
// ONLY persistent form of a polymer bracket, which is what makes the acceptance
// criterion "a document saved with polymer counts keeps the semantics" checkable.
extern const char* const kPolymerParamsKey;   // "user.t13.polymer.params.1"
std::string encodePolymerBracket(const PolymerBracket& b);
PolymerBracket decodePolymerBracket(const std::string& text, bool* ok);

// Average repeat-unit molecular weight from the enclosed atoms, using the shared
// element data.  Returns 0 when the contents are unknown, and the caller must NOT
// present 0 as a computed weight.  This is a REPEAT UNIT weight, not a polymer
// weight: the polymer weight is repeatCount * unit + end groups, and this module
// deliberately does not invent end groups.
double repeatUnitWeight(const core::Document& document,
                        const std::vector<core::ObjectId>& enclosedAtoms);

} // namespace templates_queries
} // namespace r62

#endif // R62_NEXT_TEMPLATES_QUERIES_POLYMER_HPP
