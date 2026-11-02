#include "next/templates_queries/t06_bridge.hpp"

#include <algorithm>
#include <cstdlib>

namespace r62 {
namespace templates_queries {

namespace {

std::string abbreviationOf(const CatalogEntry& e) {
    std::map<std::string, std::string>::const_iterator it = e.meta.find("abbreviation");
    return (it == e.meta.end()) ? std::string() : it->second;
}

std::string metaOf(const CatalogEntry& e, const char* key) {
    std::map<std::string, std::string>::const_iterator it = e.meta.find(key);
    return (it == e.meta.end()) ? std::string() : it->second;
}

// The atom the group attaches through.  The GroupTemplate contract is that index 0
// is where the abbreviation sits, so the converter must REORDER when the catalog
// anchor is not atom 0 (OMe anchors on its oxygen, for example).
int primaryAnchorIndex(const CatalogEntry& e) {
    int best = -1;
    int bestRank = 0;
    for (size_t i = 0; i < e.anchors.size(); ++i) {
        if (best < 0 || e.anchors[i].rank < bestRank) {
            best = e.anchors[i].atomIndex;
            bestRank = e.anchors[i].rank;
        }
    }
    return best;
}

} // namespace

bool catalogEntryToGroupTemplate(const CatalogEntry& entry,
                                 chemistry::GroupTemplate* out) {
    if (!out) return false;
    const std::string abbr = abbreviationOf(entry);
    if (abbr.empty()) return false;     // not an abbreviation; T06 must not see it
    if (entry.atoms.empty()) return false;

    const int anchor = primaryAnchorIndex(entry);
    if (anchor < 0) return false;

    // Reorder atoms so the anchor becomes index 0, exactly as T06 expects.  The
    // remap table is what makes every bond index follow.
    std::vector<int> newIndexOf(entry.atoms.size(), -1);
    std::vector<int> order;
    order.push_back(anchor);
    for (size_t i = 0; i < entry.atoms.size(); ++i)
        if (static_cast<int>(i) != anchor) order.push_back(static_cast<int>(i));
    for (size_t i = 0; i < order.size(); ++i) newIndexOf[static_cast<size_t>(order[i])] = static_cast<int>(i);

    chemistry::GroupTemplate gt;
    gt.templateId = entry.templateId;
    gt.abbreviation = abbr;
    gt.expansion = metaOf(entry, "formula");
    gt.source = entry.source;

    // The anchor atom's element and charge become the template's attach element.
    const CatalogAtom& anchorAtom = entry.atoms[static_cast<size_t>(anchor)];
    gt.attachElement = anchorAtom.element == 0 ? 6 : anchorAtom.element;
    gt.charge = anchorAtom.charge;

    for (size_t i = 0; i < order.size(); ++i) {
        const CatalogAtom& a = entry.atoms[static_cast<size_t>(order[i])];
        chemistry::TemplateAtom ta;
        ta.element = a.element;
        ta.labelText = a.labelText;
        // GroupTemplate offsets are RELATIVE TO THE ATTACHMENT POINT.  We subtract
        // the anchor offset so the conversion is geometrically correct and T06's
        // planExpansion places the group where the catalog places it.
        ta.dx = a.offset.x - anchorAtom.offset.x;
        ta.dy = a.offset.y - anchorAtom.offset.y;
        ta.hydrogenCount = a.hydrogenCount;
        ta.charge = a.charge;
        gt.atoms.push_back(ta);
    }

    for (size_t i = 0; i < entry.bonds.size(); ++i) {
        const CatalogBond& b = entry.bonds[i];
        chemistry::TemplateBond tb;
        tb.from = newIndexOf[static_cast<size_t>(b.from)];
        tb.to = newIndexOf[static_cast<size_t>(b.to)];
        tb.order = b.order;
        tb.display = b.display;
        gt.bonds.push_back(tb);
    }

    // The contraction rule.  T06 refuses a contraction when the group has extra
    // substituents or when an internal bond order differs, so the rule states what
    // this catalogue entry actually created: its atom count, its bond count, and
    // whether every internal bond is single.  Aromatic rings set that false, which
    // is why contracting Ph out of a benzene ring still works (all ring bonds are
    // aromatic on both sides) but contracting it out of a Kekule ring would not.
    chemistry::ContractionRule rule;
    rule.atomCount = static_cast<int>(entry.atoms.size());
    rule.bondCount = static_cast<int>(entry.bonds.size());
    rule.requireAllSingleBonds = true;
    for (size_t i = 0; i < entry.bonds.size(); ++i) {
        if (entry.bonds[i].order != core::BondOrder::Single) {
            rule.requireAllSingleBonds = false;
            break;
        }
    }
    rule.requireNoExtraSubstituents = true;
    rule.requireExactRing = (entry.group == TemplateGroup::Ring);
    rule.refuseReason = "the subgraph does not match the shape template " + entry.templateId;
    gt.contraction = rule;

    *out = gt;
    return true;
}

bool CatalogTemplateService::findAbbreviation(const std::string& text,
                                              chemistry::GroupTemplate* out) const {
    if (text.empty()) return false;
    const std::vector<CatalogEntry>& entries = registry_->entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (abbreviationOf(entries[i]) != text) continue;
        return catalogEntryToGroupTemplate(entries[i], out);
    }
    return false;
}

bool CatalogTemplateService::findTemplate(const std::string& templateId,
                                          chemistry::GroupTemplate* out) const {
    const CatalogEntry* e = registry_->find(templateId);
    if (!e) return false;
    return catalogEntryToGroupTemplate(*e, out);
}

std::vector<chemistry::AbbreviationEntry> CatalogTemplateService::abbreviations() const {
    std::vector<chemistry::AbbreviationEntry> out;
    const std::vector<CatalogEntry>& entries = registry_->entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string abbr = abbreviationOf(entries[i]);
        if (abbr.empty()) continue;
        // A single-element abbreviation (OH, NH2) reports its element so an
        // abbreviation never has to be re-parsed as text downstream.
        int element = 0;
        if (entries[i].atoms.size() == 1 && entries[i].atoms[0].element != 0)
            element = entries[i].atoms[0].element;
        out.push_back(chemistry::AbbreviationEntry(abbr, entries[i].templateId, element,
                                                   metaOf(entries[i], "formula")));
    }
    return out;
}

size_t CatalogTemplateService::size() const {
    return abbreviations().size();
}

} // namespace templates_queries
} // namespace r62
