#ifndef R62_NEXT_TEMPLATES_QUERIES_T06_BRIDGE_HPP
#define R62_NEXT_TEMPLATES_QUERIES_T06_BRIDGE_HPP
// =============================================================================
// re_round62_viewer/next/templates_queries/t06_bridge.hpp
//
// T13 - THE CONCRETE chemistry::ITemplateService T06 ASKED FOR.
//
// -----------------------------------------------------------------------------
// WHAT PROBLEM THIS SOLVES
// -----------------------------------------------------------------------------
// chemistry/template.hpp defines:
//
//     class ITemplateService {
//         virtual bool findAbbreviation(const std::string&, GroupTemplate*) const = 0;
//         virtual bool findTemplate(const std::string&, GroupTemplate*) const = 0;
//         virtual std::vector<AbbreviationEntry> abbreviations() const = 0;
//         virtual size_t size() const = 0;
//         virtual const char* providerName() const = 0;
//     };
//
// and ships BuiltinAbbreviationTable as a STOPGAP, with the explicit instruction
// that T13 confirms or renames those ids and supplies the real service.  This is
// that service.  It is an ADAPTER, not a second library:
//
//   * every abbreviation resolves to a catalog entry id in the T13 registry, so the
//     template picker and the atom-hotspot abbreviation path share ONE namespace;
//   * the GroupTemplate it returns is CONVERTED from the catalog entry, so a change
//     to a template's geometry is visible to T06 without editing T06;
//   * a catalog entry with no abbreviation is simply not offered, so T06 sees fewer
//     entries than the picker and the counts are reported honestly.
//
// -----------------------------------------------------------------------------
// THE IDS ARE CONFIRMED, NOT RENAMED
// -----------------------------------------------------------------------------
// BuiltinAbbreviationTable uses "r62.template.group.<name>".  T13 adopts those exact
// strings (they are declared in catalog.hpp and defined in builtins.cpp), because
// renaming would invalidate any saved document that already recorded one, and an
// opaque namespace gains nothing from a different spelling.  The test suite asserts
// that every abbreviation T06 ships resolves through THIS adapter to a registry id,
// which is what turns "the namespaces agree" into a check rather than a claim.
// =============================================================================

#include "next/chemistry/template.hpp"
#include "next/templates_queries/catalog.hpp"

namespace r62 {
namespace templates_queries {

// The adapter.  Holds a REFERENCE to a registry (defaults to the built-in one), so
// a test can inject a small synthetic registry and prove the swap works without
// touching the shared built-in table.
class CatalogTemplateService : public chemistry::ITemplateService {
public:
    // Non-owning.  `registry` must outlive this object.  Pass 0 to use builtinRegistry().
    explicit CatalogTemplateService(const TemplateRegistry* registry = 0)
        : registry_(registry ? registry : &builtinRegistry()) {}

    virtual bool findAbbreviation(const std::string& text,
                                  chemistry::GroupTemplate* out) const;
    virtual bool findTemplate(const std::string& templateId,
                              chemistry::GroupTemplate* out) const;
    virtual std::vector<chemistry::AbbreviationEntry> abbreviations() const;
    virtual size_t size() const;
    virtual const char* providerName() const { return "t13.catalog"; }

    const TemplateRegistry& registry() const { return *registry_; }

private:
    const TemplateRegistry* registry_;
};

// Convert one catalog entry into the chemistry::GroupTemplate T06 consumes.
// Returns false when the entry cannot be expressed as a GroupTemplate (for example
// it declares no abbreviation, or it is a bare ring the expand path has no rule
// for).  The conversion is one-way and lossy in the documented direction:
// aromaticity and charges are carried where GroupTemplate has a field for them, and
// anything GroupTemplate cannot hold is reflected in the contraction rule rather
// than silently dropped.
bool catalogEntryToGroupTemplate(const CatalogEntry& entry,
                                 chemistry::GroupTemplate* out);

} // namespace templates_queries
} // namespace r62

#endif // R62_NEXT_TEMPLATES_QUERIES_T06_BRIDGE_HPP
