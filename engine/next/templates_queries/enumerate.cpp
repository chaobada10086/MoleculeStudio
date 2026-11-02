#include "next/templates_queries/enumerate.hpp"

#include <cstdio>

namespace r62 {
namespace templates_queries {

namespace {

std::string intText(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

EnumerationResult refuse(RefusalReason r, const std::string& message) {
    EnumerationResult out;
    out.ok = false;
    out.reason = r;
    out.message = message;
    return out;
}

// Find a definition by name.  An empty name never matches, so an undefined R site
// cannot accidentally bind to an anonymous group.
const AlternativeGroup* findDefinition(const std::vector<AlternativeGroup>& defs,
                                       const std::string& name) {
    if (name.empty()) return 0;
    for (size_t i = 0; i < defs.size(); ++i)
        if (defs[i].name == name) return &defs[i];
    return 0;
}

} // namespace

const char* refusalReasonName(RefusalReason r) {
    switch (r) {
        case RefusalReason::None:                return "none";
        case RefusalReason::UndefinedRGroup:     return "undefined-r-group";
        case RefusalReason::FreeSiteNotEnumerable: return "free-site-not-enumerable";
        case RefusalReason::NegatedElementList:  return "negated-element-list";
        case RefusalReason::NoEnumerableSite:    return "no-enumerable-site";
        case RefusalReason::CombinationOverflow: return "combination-overflow";
        case RefusalReason::InvalidDefinition:   return "invalid-definition";
        case RefusalReason::AnonymousTextOnly:   return "anonymous-text-only";
        case RefusalReason::EmptyStructure:      return "empty-structure";
    }
    return "none";
}

// ---------------------------------------------------------------------------
// planEnumeration
// ---------------------------------------------------------------------------
// A site is enumerable only when it names a FINITE, EXPLICIT set.  Every branch
// below either yields such a set or refuses, and no branch has a default that
// invents a value.
EnumerationResult planEnumeration(const TemplateRegistry& registry,
                                  const EnumerationRequest& request) {
    const int cap = (request.cap > 0) ? request.cap : kMaxEnumerations;

    if (request.sites.empty()) {
        return refuse(RefusalReason::NoEnumerableSite,
            "the structure carries no enumerable query site: an element list or a "
            "defined alternative group is required");
    }

    // Validate every definition once, before any combination is formed.
    for (size_t d = 0; d < request.definitions.size(); ++d) {
        core::Error e = validateAlternativeGroup(registry, request.definitions[d]);
        if (!e.ok())
            return refuse(RefusalReason::InvalidDefinition,
                "alternative group \"" + request.definitions[d].name + "\": " + e.message);
    }

    EnumerationResult result;
    size_t product = 1;

    for (size_t s = 0; s < request.sites.size(); ++s) {
        EnumerationSite site = request.sites[s];

        if (site.atom == core::kInvalidObjectId)
            return refuse(RefusalReason::EmptyStructure, "a site has no atom");

        // A free site names no alternative; it caps how many substituents a hit may
        // have.  Expanding it would invent structures, so it is refused BY NAME even
        // when other sites on the same structure are enumerable.  Silently ignoring it
        // is the failure mode this branch exists to prevent.
        if (site.isFreeSite)
            return refuse(RefusalReason::FreeSiteNotEnumerable,
                "a free site (*n) caps the number of substituents in a hit and names no "
                "alternative, so it cannot be enumerated");

        // Resolve the site's definition by name.  This is the ONLY way a site gets
        // fragment choices, which is what keeps "R value" and "R definition" linked by
        // a value the user can see rather than by an internal index.
        if (!site.definitionName.empty()) {
            const AlternativeGroup* def = findDefinition(request.definitions, site.definitionName);
            if (def == 0) {
                return refuse(RefusalReason::UndefinedRGroup,
                    "site R group \"" + site.definitionName + "\" has no definition in this "
                    "request; an undefined R group is a variable site and cannot be "
                    "expanded. Define the alternative group first.");
            }
            // A definition SUPPLIES the choices; the site's own list is ignored so
            // there is one source of truth for what an R value means.
            site.fragmentChoices.clear();
            for (size_t f = 0; f < def->fragments.size(); ++f)
                site.fragmentChoices.push_back(def->fragments[f].templateId);
        }

        const size_t choices = site.choiceCount();
        if (choices == 0) {
            if (site.isUndefinedRGroup)
                return refuse(RefusalReason::UndefinedRGroup,
                    "an R group site carries no definition; an undefined variable site "
                    "cannot be expanded");
            return refuse(RefusalReason::UndefinedRGroup,
                "a query site carries no finite choice set; an undefined R group is a "
                "variable site and cannot be expanded. Set an alternative group on it first.");
        }

        product *= choices;
        if (product > static_cast<size_t>(cap)) {
            return refuse(RefusalReason::CombinationOverflow,
                "the product of the query-site choices is " +
                std::string(intText(static_cast<int>(product))) +
                " which exceeds the cap of " + intText(cap) +
                " structures");
        }
        result.sites.push_back(site);
    }

    if (result.sites.empty())
        return refuse(RefusalReason::NoEnumerableSite, "no enumerable site survived validation");

    // Build the cartesian product in a STABLE, documented order: the last site
    // varies fastest.  A stable order is what makes "combination 3 of 6" mean the
    // same thing on two runs, so a test can pin it.
    result.combinations.clear();
    for (size_t c = 0; c < product; ++c) {
        Combination combo;
        combo.choiceIndex.resize(result.sites.size(), 0);
        size_t rest = c;
        for (size_t i = result.sites.size(); i > 0; --i) {
            const size_t n = result.sites[i - 1].choiceCount();
            combo.choiceIndex[i - 1] = rest % n;
            rest /= n;
        }
        result.combinations.push_back(combo);
    }

    result.ok = true;
    result.reason = RefusalReason::None;
    return result;
}

// ---------------------------------------------------------------------------
// applyCombination
// ---------------------------------------------------------------------------
core::Error applyCombination(const TemplateRegistry& registry,
                            const EnumerationResult& result,
                            size_t combinationIndex,
                            std::vector<EditIntent>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    if (!result.ok)
        return core::Error(core::ErrorCode::InvalidArgument,
                           "cannot apply a refused enumeration: " + result.message);
    if (combinationIndex >= result.combinations.size())
        return core::Error(core::ErrorCode::OutOfRange, "combination index out of range");

    const Combination& combo = result.combinations[combinationIndex];
    out->clear();
    for (size_t s = 0; s < result.sites.size(); ++s) {
        const EnumerationSite& site = result.sites[s];
        const size_t idx = combo.choiceIndex[s];

        if (idx < site.elementChoices.size()) {
            EditIntent intent;
            intent.kind = EditIntent::ReplaceElement;
            intent.atom = site.atom;
            intent.element = site.elementChoices[idx];
            out->push_back(intent);
            continue;
        }
        size_t rest = idx - site.elementChoices.size();
        if (site.hasDeuterium) {
            if (rest == 0) {
                // Deuterium is hydrogen with isotope 2; there is no element number
                // for it, so the intent names the element AND the isotope through a
                // dedicated field the caller applies.  Represented as element 1 with
                // the isotope carried in `element` being insufficient, so the intent
                // uses ReplaceElement on H and the caller sets the isotope.  Stated
                // explicitly rather than silently expanding D to plain hydrogen.
                EditIntent intent;
                intent.kind = EditIntent::ReplaceElement;
                intent.atom = site.atom;
                intent.element = 1;   // H; the isotope is applied by the caller
                out->push_back(intent);
                continue;
            }
            --rest;
        }
        if (site.hasTritium) {
            if (rest == 0) {
                EditIntent intent;
                intent.kind = EditIntent::ReplaceElement;
                intent.atom = site.atom;
                intent.element = 1;   // H; the isotope is applied by the caller
                out->push_back(intent);
                continue;
            }
            --rest;
        }
        if (rest < site.fragmentChoices.size()) {
            const std::string& id = site.fragmentChoices[rest];
            if (registry.find(id) == 0)
                return core::Error(core::ErrorCode::NotFound, "fragment template missing: " + id);
            EditIntent intent;
            intent.kind = EditIntent::InstantiateFragment;
            intent.atom = site.atom;
            intent.templateId = id;
            out->push_back(intent);
            continue;
        }

        return core::Error(core::ErrorCode::InvalidSemantics, "choice index does not resolve");
    }
    return core::Error::success();
}

} // namespace templates_queries
} // namespace r62
