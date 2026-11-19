#ifndef R62_NEXT_TEMPLATES_QUERIES_QUERY_HPP
#define R62_NEXT_TEMPLATES_QUERIES_QUERY_HPP
// =============================================================================
// re_round62_viewer/next/templates_queries/query.hpp
//
// T13 - QUERY-STRUCTURE SEMANTICS for the six Query-toolbar commands.
//
// -----------------------------------------------------------------------------
// THE SIX COMMANDS (verbatim from the installed GUI configuration)
// -----------------------------------------------------------------------------
//   host_integration/drawing_inventory/tools.csv rows 174-179, all sourced from
//   C:/ProgramData/RevvitySignalsSoftware/ChemDrawApplications/ChemDraw/GUI/
//   Common/XML/Toolbars/Query_Toolbar.xml (read-only, SHA in out/T13):
//
//     chooseQueryTool_ReactionAtomAtomMap
//     chooseQueryTool_AlternativeGroup
//     chooseQueryTool_FreeSite
//     chooseQueryTool_AbsoluteStereochemistry
//     chooseQueryTool_RacemicStereochemistry
//     chooseQueryTool_RelativeStereochemistry
//
// Those are six PARAMETER VECTORS over distinct sub-models, not six algorithms.
// This file gives each one an explicit data representation so that:
//
//   * a saved document preserves the SEMANTICS (not a look-alike glyph),
//   * the editor can edit the semantics and undo it,
//   * enumeration can REFUSE what it cannot honestly enumerate.
//
// -----------------------------------------------------------------------------
// SOURCES (bundled ChemDraw User Guide, PDF reader page numbers; extracted with
// PyMuPDF at build time and stored under out/T13/probe for review)
// -----------------------------------------------------------------------------
//   * Atom-to-atom mapping, PDF 346-348.  "Atom-to-Atom Mapping ... create
//     correspondences between atoms in different structures"; the map value is a
//     positive integer; dragging to empty space CLEARS the mapping.
//   * Stereochemical flags, PDF 349: "There are three types of flags that
//     indicate stereochemistry: Racemic - A racemic mixture. Absolute - A pure
//     enantiomer of known configuration. Relative - A pure enantiomer of unknown
//     configuration."  The flag is placed NEXT TO a structure, not on an atom.
//   * Free Sites, PDF 322 + 324: notation *0..*15, "specify the maximum number of
//     substituents a result can have"; clicking increments, Alt+click decrements.
//   * Element lists, PDF 327: "[Cl, Br, I]"; "may contain only atomic symbols,
//     plus D (deuterium) and T (tritium)".
//   * Element NOT lists, PDF 328: "[NOT Cl, Br, I]"; NOT all-caps; "-" is an
//     accepted alternative; same D/T restriction.
//   * Alternative groups, PDF 334-338: named group (R, G, ...) with a box of
//     fragments; ATTACHMENT POINTS carry a RANK ORDER shown in the parent;
//     anonymous alternative groups (PDF 339-340) are an element-list-like bracket
//     list of elements/fragments/nicknames.
//   * Exported query property table, PDF 350-354: enumerates the query properties
//     ChemDraw itself writes (Substituents, Free Sites, Reaction Change, Reaction
//     Stereo, Element Lists, Element Not Lists, Alternative Groups, Atom-Atom
//     mapping, Variable Attachment Positions, ...) and the export caveats that go
//     with each format.  This table is why "unsupported" in this module names a
//     REAL property rather than an absent feature.
//
// -----------------------------------------------------------------------------
// WHAT THIS MODULE DOES *NOT* DO
// -----------------------------------------------------------------------------
// It does not evaluate a query against a database, and it does not claim that
// "R = any carbon".  An R group is a VARIABLE with an optional definition; until a
// definition exists, the only honest statement is "unsubstituted variable site".
// See enumerate.hpp for the refusal rules.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/core/commands.hpp"
#include "next/core/document.hpp"
#include "next/templates_queries/catalog.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace templates_queries {

// ---------------------------------------------------------------------------
// The six commands, as an exact enumeration
// ---------------------------------------------------------------------------
// Names are the platform command strings, so tools.csv, the registry and this
// enum can be checked against each other mechanically.
enum class QueryTool : uint8_t {
    None = 0,
    ReactionAtomAtomMap = 1,
    AlternativeGroup = 2,
    FreeSite = 3,
    AbsoluteStereochemistry = 4,
    RacemicStereochemistry = 5,
    RelativeStereochemistry = 6
};

const char* queryToolCommand(QueryTool t);      // "chooseQueryTool_FreeSite"
bool        queryToolFromCommand(const std::string& command, QueryTool* out);
const char* queryToolName(QueryTool t);         // "free-site"

// ---------------------------------------------------------------------------
// 1. Reaction atom-atom mapping
// ---------------------------------------------------------------------------
// A map number is a POSITIVE integer shared by corresponding atoms across the
// reactant and product sides.  0 means "unmapped"; it is not map number zero.
// The mapping lives on the ATOM, so it is represented as atom state, not as a
// graphic floating near the atom.
enum { kMaxAtomMapNumber = 999 };   // project bound; see RESULTS.md for why

struct AtomMapEntry {
    core::ObjectId atom;
    int            mapNumber;   // 1..kMaxAtomMapNumber, or 0 == unmapped
    AtomMapEntry() : atom(core::kInvalidObjectId), mapNumber(0) {}
    AtomMapEntry(core::ObjectId a, int n) : atom(a), mapNumber(n) {}
};

core::Error setAtomMapNumber(const std::vector<core::ObjectId>& atoms,
                             int mapNumber, std::vector<core::Command>* out);

core::Error clearAtomMap(const std::vector<core::ObjectId>& atoms,
                         std::vector<core::Command>* out);

// Map numbers currently in use, ascending.  The caller uses this to pick the next
// free number and to report a collision.
std::vector<int> usedAtomMapNumbers(const core::Document& document);

// The unique atom carrying map number n, or kInvalidObjectId.  A well-formed
// reaction has at most one atom per side per number; this returns the first in
// stable-id order and sets *count to the true number found, so the caller can
// report a duplicate rather than silently pick one.
core::ObjectId atomWithMapNumber(const core::Document& document, int mapNumber,
                                 int* count);

// ---------------------------------------------------------------------------
// 2. Free sites  ("*0" .. "*15")
// ---------------------------------------------------------------------------
// Guide: "specify the maximum number of substituents a result can have. However,
// it also requires that the bonds in the results appear exactly as they appear in
// the query structure."  So a free site is a CAP ON SUBSTITUENTS, expressed on an
// atom, and it does NOT by itself change the drawn bonds - which is exactly why it
// cannot be rendered as a "looks like a polymer" text sticker.
enum { kMaxFreeSites = 15 };

struct FreeSiteSpec {
    core::ObjectId atom;
    int            count;   // 0..kMaxFreeSites; 0 means "as drawn"
    FreeSiteSpec() : atom(core::kInvalidObjectId), count(0) {}
    FreeSiteSpec(core::ObjectId a, int c) : atom(a), count(c) {}
};

// The displayed notation, exactly as the guide prints it: "*0", "*1", ... "*15".
std::string freeSiteLabel(int count);

// Parse "*n" / "n" / "*n*".  Refuses > 15 and negatives (guide lists 0..15).
bool parseFreeSiteLabel(const std::string& text, int* out);

core::Error setFreeSiteCount(const std::vector<core::ObjectId>& atoms, int count,
                             std::vector<core::Command>* out);

// The increment/decrement the Free Site tool performs on click.  The tool is
// stateful in ChemDraw ("Continue to click the atom to increase the free site
// number", Alt+click to reduce), so the STEP is modelled explicitly and the
// caller owns "what is the current value".
int stepFreeSite(int current, bool decrease);

// ---------------------------------------------------------------------------
// 3. Element lists and element NOT lists
// ---------------------------------------------------------------------------
// Guide: elements separated by commas; a space after each comma and the brackets
// are optional; ONLY atomic symbols plus D and T are allowed.  The NOT form uses
// the all-caps word NOT followed by a space, or a minus sign.
//
// This is stored as a STRUCTURE (a list of element numbers plus a negate flag),
// not as the literal display string, so that the query meaning survives a font
// change, an export and a re-import.
struct ElementList {
    bool negated;                 // true == "NOT" list
    std::vector<int> elements;    // 1..118; D and T are described below
    bool hasDeuterium;            // D: the guide allows it explicitly
    bool hasTritium;              // T
    ElementList() : negated(false), hasDeuterium(false), hasTritium(false) {}
    bool empty() const {
        return elements.empty() && !hasDeuterium && !hasTritium;
    }
    bool operator==(const ElementList& o) const {
        return negated == o.negated && elements == o.elements &&
               hasDeuterium == o.hasDeuterium && hasTritium == o.hasTritium;
    }
};

// Parse "[Cl, Br, I]" and "[NOT Cl, Br, I]" / "[-Cl, Br, I]".  Brackets and spaces
// are optional.  `ok` is false with a reason on: an unknown symbol, an empty list,
// a duplicated element, a symbol that is not an atomic element/D/T.
bool parseElementList(const std::string& text, ElementList* out, std::string* reason);

// Canonical display text, exactly the guide spelling: "[Cl, Br, I]" / "[NOT Cl, Br, I]".
std::string formatElementList(const ElementList& list);

// ---------------------------------------------------------------------------
// 4. Alternative groups (named R tables and anonymous bracket lists)
// ---------------------------------------------------------------------------
// Guide PDF 334: "create alternative group definitions (R, G, and so on.) that
// represent a set of substituents".  PDF 336: a fragment may carry MORE THAN ONE
// attachment point and the rank order matters.
//
// An alternative group is therefore: a NAME, a set of FRAGMENTS, and for each
// fragment a list of attachment points with ranks.  A fragment references a
// catalog entry plus an attachment-point mapping, which is what lets one service
// instantiate the fragment (see instantiate.hpp) instead of a second code path.
struct AltAttachment {
    int atomIndex;   // inside the fragment template
    int rank;        // 1-based, unique within the fragment
    AltAttachment() : atomIndex(0), rank(1) {}
};

struct AltFragment {
    std::string               templateId;  // a catalog entry id
    std::vector<AltAttachment> attachments;
    AltFragment() {}
};

struct AlternativeGroup {
    std::string               name;        // "R1", "G", ...  may be empty (anonymous)
    bool                      anonymous;   // true for the bracket-list form
    std::vector<AltFragment>  fragments;
    AlternativeGroup() : anonymous(false) {}
    bool operator==(const AlternativeGroup& o) const {
        if (name != o.name || anonymous != o.anonymous) return false;
        if (fragments.size() != o.fragments.size()) return false;
        for (size_t i = 0; i < fragments.size(); ++i) {
            if (fragments[i].templateId != o.fragments[i].templateId) return false;
            if (fragments[i].attachments.size() != o.fragments[i].attachments.size()) return false;
            for (size_t j = 0; j < fragments[i].attachments.size(); ++j) {
                if (fragments[i].attachments[j].atomIndex != o.fragments[i].attachments[j].atomIndex) return false;
                if (fragments[i].attachments[j].rank != o.fragments[i].attachments[j].rank) return false;
            }
        }
        return true;
    }
};

// Validate a definition: non-empty when named, at least one fragment, every
// fragment resolvable in `registry`, attachment ranks unique per fragment, and
// the rank set of every fragment identical (ChemDraw cross-links fragments by
// rank; fragments with different rank sets cannot be substituted for each other).
core::Error validateAlternativeGroup(const TemplateRegistry& registry,
                                    const AlternativeGroup& group);

// ---------------------------------------------------------------------------
// 5. Stereochemistry flags
// ---------------------------------------------------------------------------
// Guide PDF 349 names three flags and says where they go: "Click next to the
// structure to which you want to assign the flag."  So a flag is an OBJECT with a
// position and a kind, associated with a set of atoms - NOT a bond wedge and NOT an
// atom label.  Storing it as an atom label would make "racemic" indistinguishable
// from the text "racemic" typed by a user.
enum class StereoFlagKind : uint8_t {
    None = 0,
    Absolute = 1,   // pure enantiomer, known configuration
    Racemic = 2,    // racemic mixture
    Relative = 3    // pure enantiomer, unknown configuration
};

const char* stereoFlagName(StereoFlagKind k);
bool        stereoFlagFromName(const std::string& name, StereoFlagKind* out);
QueryTool   stereoFlagTool(StereoFlagKind k);

struct StereoFlag {
    StereoFlagKind      kind;
    core::Vec2          position;   // where the flag is drawn
    std::vector<core::ObjectId> atoms;  // the structure it is assigned to
    double              textSize;   // 0 == document default (guide: default label size)
    StereoFlag() : kind(StereoFlagKind::None), textSize(0.0) {}
};

// A structure may carry at most one flag of each kind, and an atom may belong to
// at most one structure carrying a flag.  Two contradictory flags on one structure
// ("absolute" and "racemic") is a refusal, not a last-one-wins overwrite.
core::Error validateStereoFlag(const core::Document& document,
                               const StereoFlag& flag);

} // namespace templates_queries
} // namespace r62

#endif // R62_NEXT_TEMPLATES_QUERIES_QUERY_HPP
