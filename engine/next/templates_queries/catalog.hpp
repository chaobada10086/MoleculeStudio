#ifndef R62_NEXT_TEMPLATES_QUERIES_CATALOG_HPP
#define R62_NEXT_TEMPLATES_QUERIES_CATALOG_HPP
// =============================================================================
// re_round62_viewer/next/templates_queries/catalog.hpp
//
// T13 - TEMPLATE CATALOG: the on-disk template format, the registry, and the
//        TEMPLATE-ID NAMESPACE that T06 asks T13 to own.
//
// -----------------------------------------------------------------------------
// WHAT A "TEMPLATE" IS HERE, AND WHAT IT IS NOT
// -----------------------------------------------------------------------------
// The drawing inventory (host_integration/drawing_inventory/tools.csv, row 182)
// counts the template picker as ONE tool entry (chooseTemplateTool, MainToolBarID).
// The inventory explicitly says the library contents are NOT counted there
// ("template picker counts as one entry; library contents and monomer libraries
//  are not counted individually", DRAWING_FEATURES_2026-10-14.md).  So "1 tool"
// does not mean "1 drawing".  This module therefore separates four things that a
// single template button conflates:
//
//   1. A CATALOG ENTRY is DATA (atoms + bonds + anchors + metadata) that can be
//      instantiated into any document.
//   2. A REGISTRY owns the entry ids and answers lookups.
//   3. AN INSTANTIATOR turns one entry into a document edit with freshly
//      remapped ids (see instantiate.hpp).
//   4. The T06 ITemplateService adapter exposes the same registry to the
//      abbreviation/functional-group path, so there is exactly ONE id namespace
//      for both the template picker and the atom-hotspot abbreviations.
//
// -----------------------------------------------------------------------------
// WHY THE FORMAT IS TEXT AND NOT BINARY
// -----------------------------------------------------------------------------
// Templates are small, hand-authored and reviewed.  A text format is diffable in
// review, has no struct layout in the file, tolerates added keys (an unknown key
// becomes a named issue, never a silent reinterpretation), and - most importantly
// - has a VERSION field so a future format change can refuse an old file instead
// of misreading it.  Same reasoning as next/graphics/geometry.hpp.
//
// -----------------------------------------------------------------------------
// LICENSING (a hard constraint of the task, not a formality)
// -----------------------------------------------------------------------------
// The task forbids scraping template assets of unclear licensing.  Therefore:
//   * This module ships NO third-party template geometry.
//   * Every built-in entry is authored HERE, from either (a) the periodic table
//     and standard valence, or (b) a structure any chemistry textbook states as
//     a fact (benzene is a six-membered carbocycle, and so on).
//   * Every entry carries source and license fields, and the loader REFUSES an
//     entry whose license is empty, so an untraceable asset cannot be smuggled
//     in by adding a file.
//   * The ChemDraw User Guide is used for SEMANTICS only (what a bracket usage
//     means, what a free site means).  It is never a source of coordinates.
//
// -----------------------------------------------------------------------------
// THE FILE FORMAT (".r62tpl", UTF-8, line oriented)
// -----------------------------------------------------------------------------
//   # comment                                  (ignored)
//   version 1                                  (required, first directive)
//   entry <templateId>
//     name        <display name>               (required)
//     group       <catalog group>              (required, see TemplateGroup)
//     license     <SPDX-ish or project-authored>
//     source      <provenance string>
//     attach      <anchorIndex>                (default 0)
//     atom        <element> <x> <y> [key=value ...]
//     bond        <from> <to> <order> [display=..] [semantics=..]
//     anchor      <atomIndex> <rank>
//     meta        <key> <value>
//   end
//
// Coordinates are DOCUMENT UNITS relative to the entry origin, in the T01
// convention (x right, y DOWN; angles CCW-in-math via core::direction()).  A
// template is instantiated by translating to a target point and rotating; no
// template stores an absolute page position.
// =============================================================================

#include "next/core/contracts.hpp"

#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace templates_queries {

// Interface version this module was compiled against.  Reported in RESULTS.md and
// checked at startup with core::requestCompatibility().
enum {
    kModuleInterfaceMajor   = 1,
    kModuleInterfaceMinor   = 0,
    kModuleInterfaceVersion = kModuleInterfaceMajor * 10000 + kModuleInterfaceMinor
};

// The format version this parser accepts.  A file declaring a different version
// is refused by name rather than read optimistically.
enum { kCatalogFormatVersion = 1 };

// ---------------------------------------------------------------------------
// Catalog grouping
// ---------------------------------------------------------------------------
// Deliberately coarse and STABLE: the picker groups by this and a test asserts
// every loaded entry has a known group.  Adding a group is additive; it never
// renames an existing one.
enum class TemplateGroup : uint8_t {
    Unknown = 0,
    Ring = 1,            // bare carbocycles / heterocycles
    FunctionalGroup = 2, // Me, Et, Ph, NO2, ... (shared with T06 abbreviations)
    Scaffold = 3,        // fused / polycyclic common cores
    Labelled = 4,        // a labelled atom + environment (query flavoured)
    Geometry = 5         // drawing geometry (e.g. a bracket pair frame)
};

const char* templateGroupName(TemplateGroup g);
bool templateGroupFromName(const std::string& name, TemplateGroup* out);

// ---------------------------------------------------------------------------
// Entry records
// ---------------------------------------------------------------------------
// A template atom mirrors T06 chemistry::TemplateAtom deliberately: the catalog is
// the SOURCE of a chemistry::GroupTemplate, not a second chemistry model.
struct CatalogAtom {
    int         element;        // 1..118, or 0 for a labelled node
    std::string labelText;      // used when element == 0, or as an override
    core::Vec2  offset;         // from the entry origin
    int         charge;
    int         hydrogenCount;  // -1 == compute
    bool        aromatic;
    CatalogAtom() : element(6), charge(0), hydrogenCount(-1), aromatic(false) {}
    bool operator==(const CatalogAtom& o) const {
        return element == o.element && labelText == o.labelText &&
               offset == o.offset && charge == o.charge &&
               hydrogenCount == o.hydrogenCount && aromatic == o.aromatic;
    }
};

struct CatalogBond {
    int               from;      // index into atoms
    int               to;        // index into atoms
    core::BondOrder   order;
    core::BondDisplay display;
    core::BondSemantics semantics;
    CatalogBond()
        : from(0), to(0), order(core::BondOrder::Single),
          display(core::BondDisplay::Normal),
          semantics(core::BondSemantics::Normal) {}
    bool operator==(const CatalogBond& o) const {
        return from == o.from && to == o.to && order == o.order &&
               display == o.display && semantics == o.semantics;
    }
};

// An attachment anchor.  rank is the ChemDraw attachment RANK ORDER from the
// guide ("The number displayed in the attachment point symbol is the attachment
// rank order", PDF p.336); rank 1 is primary.  Two anchors sharing a rank is a
// load error, because which one is "first" would then be undefined for a
// multi-attachment template.
struct CatalogAnchor {
    int atomIndex;
    int rank;
    CatalogAnchor() : atomIndex(0), rank(1) {}
    bool operator==(const CatalogAnchor& o) const {
        return atomIndex == o.atomIndex && rank == o.rank;
    }
};

struct CatalogEntry {
    std::string                        templateId;  // namespaced, globally unique
    std::string                        name;
    TemplateGroup                      group;
    std::string                        license;
    std::string                        source;
    std::vector<CatalogAtom>           atoms;
    std::vector<CatalogBond>           bonds;
    std::vector<CatalogAnchor>         anchors;
    std::map<std::string, std::string> meta;
    CatalogEntry() : group(TemplateGroup::Unknown) {}
    bool valid() const { return !templateId.empty() && !atoms.empty(); }
    bool operator==(const CatalogEntry& o) const {
        return templateId == o.templateId && name == o.name && group == o.group &&
               license == o.license && source == o.source && atoms == o.atoms &&
               bonds == o.bonds && anchors == o.anchors && meta == o.meta;
    }
};

// ---------------------------------------------------------------------------
// Load diagnostics
// ---------------------------------------------------------------------------
// A load NEVER throws and NEVER half-applies.  It returns the entries it could
// validate plus a list of REJECTIONS with the line number and reason, so a
// malformed asset is visible rather than silently absent.
struct CatalogIssue {
    int         line;        // 1-based source line, 0 when not line specific
    std::string templateId;  // empty when the failure is not entry scoped
    std::string reason;
    CatalogIssue() : line(0) {}
};

struct CatalogLoadResult {
    std::vector<CatalogEntry> entries;
    std::vector<CatalogIssue> issues;
    bool ok() const { return issues.empty(); }
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
// Owns entries by id.  Insertion order is preserved for the picker; lookups are
// exact and case-sensitive (a template id is a key, not user text).
class TemplateRegistry {
public:
    TemplateRegistry() {}

    // Add one entry.  Refuses a duplicate id (leaving the existing entry
    // untouched) so "which template did the saved document reference?" has one
    // answer.  Refuses an entry with no license or no valid anchor.
    core::Error add(const CatalogEntry& entry);
    void        addQuiet(const CatalogEntry& entry);   // builtin table only

    const CatalogEntry* find(const std::string& templateId) const;
    size_t size() const { return entries_.size(); }
    bool   empty() const { return entries_.empty(); }
    const std::vector<CatalogEntry>& entries() const { return entries_; }

    // Ids whose group matches, in insertion order.  Unknown == all.
    std::vector<std::string> idsInGroup(TemplateGroup group) const;
    // Ids whose display name or id contains needle, case-insensitive.  For the
    // picker filter box only; never used to resolve a document reference.
    std::vector<std::string> search(const std::string& needle) const;

    void clear() { entries_.clear(); }

private:
    std::vector<CatalogEntry> entries_;
};

// ---------------------------------------------------------------------------
// Parsing / serialising
// ---------------------------------------------------------------------------
// Parse one catalog text.  origin names the source file for diagnostics only.
CatalogLoadResult parseCatalog(const std::string& text, const std::string& origin);

// Parse and add to an existing registry.  An offending entry is skipped; the
// registry keeps whatever was already valid.
CatalogLoadResult loadCatalogInto(TemplateRegistry& registry,
                                  const std::string& text,
                                  const std::string& origin);

// Read a file from disk (UTF-8).  Returns false and fills error on I/O failure;
// a parse failure is NOT an I/O failure and comes back in the result.
bool readCatalogFile(const std::string& path, std::string* out, std::string* error);

// Serialise one entry back to the text format.  The tests assert
// parse(serialize(e)) == e for every built-in entry, which makes "the built-in
// library is expressible in the documented format" checkable.
std::string serializeEntry(const CatalogEntry& entry);
std::string serializeCatalog(const std::vector<CatalogEntry>& entries);

// ---------------------------------------------------------------------------
// The built-in library (project-authored, licence tagged)
// ---------------------------------------------------------------------------
// Populated in builtins.cpp.  builtinCatalogText() returns the SAME data as text,
// which is how the tests prove the file format can express every shipped
// template.  The ChemDraw User Guide is cited for semantics only.
const TemplateRegistry& builtinRegistry();
std::string             builtinCatalogText();

// Ids T06 asked T13 to confirm or rename (chemistry/INTERFACES.md 4.2).  Declared
// here so the confirmation is a compile-time fact: a test asserts each resolves
// and that it is the id the corresponding abbreviation maps to.
extern const char* const kIdGroupMethyl;
extern const char* const kIdGroupEthyl;
extern const char* const kIdGroupPhenyl;
extern const char* const kIdGroupBenzyl;
extern const char* const kIdGroupAcetyl;
extern const char* const kIdGroupMethoxy;
extern const char* const kIdGroupHydroxyl;
extern const char* const kIdGroupNitro;
extern const char* const kIdGroupCarboxyl;
extern const char* const kIdGroupTrifluoromethyl;
extern const char* const kIdGroupTertButyl;
extern const char* const kIdGroupAmine;

} // namespace templates_queries
} // namespace r62

#endif // R62_NEXT_TEMPLATES_QUERIES_CATALOG_HPP
