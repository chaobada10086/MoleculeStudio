#ifndef R62_NEXT_CHEMISTRY_LABEL_HPP
#define R62_NEXT_CHEMISTRY_LABEL_HPP
// =============================================================================
// re_round62_viewer/next/chemistry/label.hpp
//
// T06 - ATOM LABEL TYPING: "element" and "display text" are DIFFERENT things.
//
// THE PROBLEM THIS FILE EXISTS TO PREVENT
// -----------------------------------------------------------------------------
// The legacy editor stored an atom label as one std::string.  That makes "N",
// "Ph", "R1", "OMe" and "garbage the user typed" indistinguishable at the model
// level, and every consumer guesses.  The classic failure is a consumer that
// cannot parse the string falling back to element = 6 (carbon), which silently
// rewrites a phenyl group into a methyl carbon and changes what a user sees with
// no warning at all.
//
// The fix is an explicit TYPE on every label, decided once, with the unknown case
// being a first-class value that is PRESERVED rather than coerced:
//
//   LabelKind::Element      - a real periodic-table element.  element is
//                             meaningful and the label text derives from it.
//   LabelKind::Abbreviation - a known abbreviation from the T13 template service
//                             (Ph, Me, Et, Ac, Bn, ...).  It has a template id
//                             and can be expanded; it is NOT an element, and no
//                             code may treat it as carbon.
//   LabelKind::RGroup       - R, R1, R2 ... a generic/variable group.  Carries
//                             an index.  Never an element.
//   LabelKind::Query        - A, Q, X, M, L, ...: query atoms, kept as query text.
//   LabelKind::Arbitrary    - any other non-empty string.  PRESERVED VERBATIM.
//                             The atom keeps whatever element field it had; the
//                             label only overrides DISPLAY.  No parser may turn
//                             an Arbitrary label into carbon.
//   LabelKind::None         - no label override; draw the element symbol (or the
//                             skeletal convention for carbon).
//
// AtomLabel is the single value a caller passes to and from the document.  The
// persisted form is ASCII-tagged (encodeLabel/decodeLabel) so a round trip
// through the extension bag cannot lose the kind.
//
// RELATIONSHIP TO T09 (text/atom_label.hpp)
// -----------------------------------------------------------------------------
// T09 owns GLYPHS AND PLACEMENT and has its own LabelSpec/parseLabel, which is a
// LAYOUT-side tokenizer.  This file is the CHEMISTRY side.  toLabelSpec() is the
// adapter that converts a typed AtomLabel into T09 layout input, so neither
// module needs the other's internals and neither one owns the other's
// representation.  A raw string is turned into a typed label via interpretLabel()
// - once, at the boundary - not at every call site.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/chemistry/element_data.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace chemistry {

// ---------------------------------------------------------------------------
// LabelKind
// ---------------------------------------------------------------------------
enum class LabelKind : uint8_t {
    None = 0,           // no override: derive from element + semantics
    Element = 1,        // a real element symbol (case-exact)
    Abbreviation = 2,   // known abbreviation with a template mapping
    RGroup = 3,         // R group (index in AtomLabel::rGroupIndex)
    Query = 4,          // query atom token (A, Q, X, M, L, ...)
    Arbitrary = 5       // user text, preserved verbatim, never interpreted
};

const char* labelKindName(LabelKind kind);
// The inverse of labelKindName, used by the persistence encoder.  Returns false
// when the tag is unknown (so a future tag is refused, not guessed).
bool labelKindFromName(const std::string& name, LabelKind* out);

// ---------------------------------------------------------------------------
// AtomLabel
// ---------------------------------------------------------------------------
struct AtomLabel {
    LabelKind   kind;
    std::string text;           // display text WITHOUT markup, e.g. "Ph", "R", "CO2Me"
    int         element;        // 0 unless kind == Element
    int         charge;         // -15..+15
    int         isotope;        // 0 == natural
    int         hydrogenCount;  // -1 == compute from valence, else explicit count
    uint8_t     radical;        // core::Radical as uint8_t
    int         rGroupIndex;    // 1-based for RGroup; 0 == bare "R"
    uint8_t     nodeType;       // core::NodeType as uint8_t
    bool        showLabel;      // force the label to be drawn

    AtomLabel()
        : kind(LabelKind::None), element(0), charge(0), isotope(0),
          hydrogenCount(-1), radical(0), rGroupIndex(0),
          nodeType(static_cast<uint8_t>(core::NodeType::Element)),
          showLabel(false) {}

    bool operator==(const AtomLabel& o) const {
        return kind == o.kind && text == o.text && element == o.element &&
               charge == o.charge && isotope == o.isotope &&
               hydrogenCount == o.hydrogenCount && radical == o.radical &&
               rGroupIndex == o.rGroupIndex && nodeType == o.nodeType &&
               showLabel == o.showLabel;
    }
    bool operator!=(const AtomLabel& o) const { return !(*this == o); }

    // True when this label describes an atom whose topological identity is a real
    // element.  Arbitrary/Abbreviation/RGroup return false - the point of the type.
    bool hasElement() const { return kind == LabelKind::Element && element >= 1 && element <= 118; }
};

// ---------------------------------------------------------------------------
// Text rendering of the label the way an atom label draws it
// ---------------------------------------------------------------------------
std::string formatLabelText(const AtomLabel& label, bool includeHydrogens);

// ---------------------------------------------------------------------------
// Classification: the ONE place a string becomes a typed label
// ---------------------------------------------------------------------------
// interpretLabel() never guesses "carbon".  The rules, in order:
//   1. empty                                 -> LabelKind::None
//   2. exact element symbol (case sensitive) -> Element
//   3. matches an abbreviation the CALLER supplied (the T13 template list)
//                                            -> Abbreviation
//   4. matches R / R<digits>                 -> RGroup
//   5. matches a query token (A, Q, X, M, L, *) -> Query
//   6. otherwise                             -> Arbitrary (text preserved)
//
// The abbreviation list is INJECTED rather than hard-coded here, because T13 owns
// the template ID namespace and a second hard-coded copy would drift.  Pass an
// empty list and abbreviations become Arbitrary - still safe (the label is
// preserved and the atom keeps its element) and correct when the template service
// is not linked in.
struct AbbreviationEntry {
    std::string text;       // "Ph"
    std::string templateId; // "r62.template.group.phenyl"
    int         element;    // 0 == not a single element
    std::string expansion;  // canonical expansion formula, for evidence only
    AbbreviationEntry() : element(0) {}
    AbbreviationEntry(const std::string& t, const std::string& id, int e,
                      const std::string& ex)
        : text(t), templateId(id), element(e), expansion(ex) {}
};

struct LabelInterpretation {
    AtomLabel   label;
    int         element;        // best element match; 0 when none
    std::string templateId;     // set when kind == Abbreviation
    std::string expansion;      // set when kind == Abbreviation
    size_t      warnings;
    std::string note;
    LabelInterpretation() : element(0), warnings(0) {}
};

LabelInterpretation interpretLabel(const std::string& text,
                                   const std::vector<AbbreviationEntry>& abbreviations);

// Convenience: interpret with no abbreviation list.
LabelInterpretation interpretLabel(const std::string& text);

// ---------------------------------------------------------------------------
// Markup stripping
// ---------------------------------------------------------------------------
// Remove rich-text tags used by the CDXML label content and the rich-text path.
// Returns the stripped text and reports how many tags were removed.
std::string stripTags(const std::string& utf8, size_t* tagsRemoved);

// ---------------------------------------------------------------------------
// Persistence encoding (side-table safe, ASCII, order stable)
// ---------------------------------------------------------------------------
std::string encodeLabel(const AtomLabel& label);
core::Error decodeLabel(const std::string& encoded, AtomLabel* out);

// ---------------------------------------------------------------------------
// Element-symbol validation used by the edit commands
// ---------------------------------------------------------------------------
bool isElementSymbol(const std::string& text, int* number);

} // namespace chemistry
} // namespace r62

#endif // R62_NEXT_CHEMISTRY_LABEL_HPP
