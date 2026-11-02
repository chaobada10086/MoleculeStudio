// =============================================================================
// re_round62_viewer/next/chemistry/template.cpp
//
// T06 - built-in abbreviation/functional-group templates, expansion planning and
// contraction matching.
//
// WHERE THE ABBREVIATIONS COME FROM
// -----------------------------------------------------------------------------
// The abbreviation TEXT set in this file is the set the verified ChemDraw atom
// hotspot table lists that denotes a STRUCTURAL GROUP (tests/chemdraw_hotkeys_
// excerpt.txt, PDF pages 70-75).  The EXPANSION for each one is the conventional
// meaning of that abbreviation in an organic formula.  The CONTRACTION RULE is our
// own explicit design - the official guide does not publish a contraction matcher,
// so this file states its own rule instead of implying a compatibility.
//
// The element hotkeys (c, n, o, s, f, p, h, i, b, l, Shift+b, Shift+s, Shift+l) are
// NOT templates here.  They are ELEMENT edits; see edit.hpp.
// =============================================================================

#include "next/chemistry/template.hpp"

#include <cmath>

namespace r62 {
namespace chemistry {
namespace {

// Provisional ids.  T13 is asked to confirm or adopt these; see INTERFACES.md.
const char* kIdMethyl  = "r62.template.group.methyl";
const char* kIdEthyl   = "r62.template.group.ethyl";
const char* kIdPhenyl  = "r62.template.group.phenyl";
const char* kIdAcetyl  = "r62.template.group.acetyl";
const char* kIdBenzyl  = "r62.template.group.benzyl";
const char* kIdMethoxy = "r62.template.group.methoxy";
const char* kIdNitro   = "r62.template.group.nitro";
const char* kIdCarboxyl= "r62.template.group.carboxyl";
const char* kIdTrifluoroMethyl = "r62.template.group.trifluoromethyl";
const char* kIdTertButyl = "r62.template.group.tertbutyl";
const char* kIdAmine   = "r62.template.group.amine";
const char* kIdHydroxyl= "r62.template.group.hydroxyl";

const char* kSourceHotkey = "ChemDraw atom hotspot table (PDF 70-75); expansion is conventional organic notation (project design)";

// Ring helper.  Benzene is drawn as alternating single/double, which is what the
// acceptance criterion for an aromatic ring wants to see as real topology (not as
// an order=4 token).
void buildRing(std::vector<TemplateAtom>* atoms, std::vector<TemplateBond>* bonds,
               int size, double radius) {
    for (int i = 0; i < size; ++i) {
        const double a = 2.0 * 3.14159265358979323846 * static_cast<double>(i) /
                         static_cast<double>(size);
        TemplateAtom ta(6, radius * std::cos(a), radius * std::sin(a));
        atoms->push_back(ta);
    }
    for (int i = 0; i < size; ++i) {
        const int j = (i + 1) % size;
        core::BondOrder o = core::BondOrder::Single;
        if (size == 6 && (i % 2) == 0) o = core::BondOrder::Double;
        bonds->push_back(TemplateBond(i, j, o));
    }
}

GroupTemplate makeMethyl() {
    GroupTemplate t;
    t.templateId = kIdMethyl; t.abbreviation = "Me"; t.expansion = "CH3";
    t.attachElement = 6; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(6, 0.0, 0.0));
    t.contraction.atomCount = 1; t.contraction.bondCount = 0;
    t.contraction.requireAllSingleBonds = true;
    t.contraction.refuseReason = "a methyl group is a single carbon with no internal bond";
    return t;
}

GroupTemplate makeEthyl() {
    GroupTemplate t;
    t.templateId = kIdEthyl; t.abbreviation = "Et"; t.expansion = "C2H5";
    t.attachElement = 6; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(6, 0.0, 0.0));
    t.atoms.push_back(TemplateAtom(6, 1.0, 0.0));
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Single));
    t.contraction.atomCount = 2; t.contraction.bondCount = 1;
    t.contraction.requireAllSingleBonds = true;
    t.contraction.refuseReason = "an ethyl group is two singly bonded carbons";
    return t;
}

GroupTemplate makePhenyl() {
    GroupTemplate t;
    t.templateId = kIdPhenyl; t.abbreviation = "Ph"; t.expansion = "C6H5";
    t.attachElement = 6; t.source = kSourceHotkey;
    buildRing(&t.atoms, &t.bonds, 6, 1.0);
    t.contraction.atomCount = 6; t.contraction.bondCount = 6;
    t.contraction.requireAllSingleBonds = false;
    t.contraction.requireExactRing = true;
    t.contraction.refuseReason = "a phenyl group is a six-membered carbocycle";
    return t;
}

GroupTemplate makeBenzyl() {
    GroupTemplate t;
    t.templateId = kIdBenzyl; t.abbreviation = "Bn"; t.expansion = "C7H7";
    t.attachElement = 6; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(6, 0.0, 0.0));
    std::vector<TemplateAtom> ringAtoms; std::vector<TemplateBond> ringBonds;
    buildRing(&ringAtoms, &ringBonds, 6, 1.0);
    for (size_t i = 0; i < ringAtoms.size(); ++i) {
        TemplateAtom a = ringAtoms[i];
        a.dx += 2.0;
        t.atoms.push_back(a);
    }
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Single));
    for (size_t i = 0; i < ringBonds.size(); ++i)
        t.bonds.push_back(TemplateBond(ringBonds[i].from + 1, ringBonds[i].to + 1,
                                       ringBonds[i].order));
    t.contraction.atomCount = 7; t.contraction.bondCount = 7;
    t.contraction.refuseReason = "a benzyl group is CH2 plus a six-membered ring";
    return t;
}

GroupTemplate makeAcetyl() {
    GroupTemplate t;
    t.templateId = kIdAcetyl; t.abbreviation = "Ac"; t.expansion = "C2H3O";
    t.attachElement = 6; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(6, 0.0, 0.0));
    t.atoms.push_back(TemplateAtom(8, 1.0, 0.0));
    t.atoms.push_back(TemplateAtom(6, -0.5, 0.87));
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Double));
    t.bonds.push_back(TemplateBond(0, 2, core::BondOrder::Single));
    t.contraction.atomCount = 3; t.contraction.bondCount = 2;
    t.contraction.refuseReason = "an acetyl group is C(=O)CH3";
    return t;
}

GroupTemplate makeMethoxy() {
    GroupTemplate t;
    t.templateId = kIdMethoxy; t.abbreviation = "OMe"; t.expansion = "OCH3";
    t.attachElement = 8; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(8, 0.0, 0.0));
    t.atoms.push_back(TemplateAtom(6, 1.0, 0.0));
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Single));
    t.contraction.atomCount = 2; t.contraction.bondCount = 1;
    t.contraction.requireAllSingleBonds = true;
    t.contraction.refuseReason = "a methoxy group is O-CH3";
    return t;
}

GroupTemplate makeHydroxyl() {
    GroupTemplate t;
    t.templateId = kIdHydroxyl; t.abbreviation = "OH"; t.expansion = "OH";
    t.attachElement = 8; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(8, 0.0, 0.0));
    t.contraction.atomCount = 1; t.contraction.bondCount = 0;
    t.contraction.refuseReason = "a hydroxyl group is a single oxygen";
    return t;
}

GroupTemplate makeNitro() {
    GroupTemplate t;
    t.templateId = kIdNitro; t.abbreviation = "NO2"; t.expansion = "NO2";
    t.attachElement = 7; t.charge = 0; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(7, 0.0, 0.0));
    t.atoms.push_back(TemplateAtom(8, 1.0, 0.0));
    t.atoms.push_back(TemplateAtom(8, -0.5, 0.87));
    t.atoms[0].charge = 1;
    t.atoms[1].charge = -1;
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Double));
    t.bonds.push_back(TemplateBond(0, 2, core::BondOrder::Single));
    t.contraction.atomCount = 3; t.contraction.bondCount = 2;
    t.contraction.refuseReason = "a nitro group is N with two oxygens";
    return t;
}

GroupTemplate makeCarboxyl() {
    GroupTemplate t;
    t.templateId = kIdCarboxyl; t.abbreviation = "CO2H"; t.expansion = "CO2H";
    t.attachElement = 6; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(6, 0.0, 0.0));
    t.atoms.push_back(TemplateAtom(8, 1.0, 0.0));
    t.atoms.push_back(TemplateAtom(8, -0.5, 0.87));
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Double));
    t.bonds.push_back(TemplateBond(0, 2, core::BondOrder::Single));
    t.contraction.atomCount = 3; t.contraction.bondCount = 2;
    t.contraction.refuseReason = "a carboxyl group is C with two oxygens";
    return t;
}

GroupTemplate makeTrifluoroMethyl() {
    GroupTemplate t;
    t.templateId = kIdTrifluoroMethyl; t.abbreviation = "CF3"; t.expansion = "CF3";
    t.attachElement = 6; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(6, 0.0, 0.0));
    t.atoms.push_back(TemplateAtom(9, 1.0, 0.0));
    t.atoms.push_back(TemplateAtom(9, -0.5, 0.87));
    t.atoms.push_back(TemplateAtom(9, -0.5, -0.87));
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Single));
    t.bonds.push_back(TemplateBond(0, 2, core::BondOrder::Single));
    t.bonds.push_back(TemplateBond(0, 3, core::BondOrder::Single));
    t.contraction.atomCount = 4; t.contraction.bondCount = 3;
    t.contraction.requireAllSingleBonds = true;
    t.contraction.refuseReason = "a trifluoromethyl group is C with three fluorines";
    return t;
}

GroupTemplate makeTertButyl() {
    GroupTemplate t;
    t.templateId = kIdTertButyl; t.abbreviation = "t-Bu"; t.expansion = "C4H9";
    t.attachElement = 6; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(6, 0.0, 0.0));
    t.atoms.push_back(TemplateAtom(6, 1.0, 0.0));
    t.atoms.push_back(TemplateAtom(6, -0.5, 0.87));
    t.atoms.push_back(TemplateAtom(6, -0.5, -0.87));
    t.bonds.push_back(TemplateBond(0, 1, core::BondOrder::Single));
    t.bonds.push_back(TemplateBond(0, 2, core::BondOrder::Single));
    t.bonds.push_back(TemplateBond(0, 3, core::BondOrder::Single));
    t.contraction.atomCount = 4; t.contraction.bondCount = 3;
    t.contraction.requireAllSingleBonds = true;
    t.contraction.refuseReason = "a tert-butyl group is C with three methyls";
    return t;
}

GroupTemplate makeAmine() {
    GroupTemplate t;
    t.templateId = kIdAmine; t.abbreviation = "NH2"; t.expansion = "NH2";
    t.attachElement = 7; t.source = kSourceHotkey;
    t.atoms.push_back(TemplateAtom(7, 0.0, 0.0));
    t.contraction.atomCount = 1; t.contraction.bondCount = 0;
    t.contraction.refuseReason = "an amino group is a single nitrogen";
    return t;
}

} // namespace

// ---------------------------------------------------------------------------
// BuiltinAbbreviationTable
// ---------------------------------------------------------------------------
BuiltinAbbreviationTable::BuiltinAbbreviationTable() {
    templates_.push_back(makeMethyl());
    templates_.push_back(makeEthyl());
    templates_.push_back(makePhenyl());
    templates_.push_back(makeBenzyl());
    templates_.push_back(makeAcetyl());
    templates_.push_back(makeMethoxy());
    templates_.push_back(makeHydroxyl());
    templates_.push_back(makeNitro());
    templates_.push_back(makeCarboxyl());
    templates_.push_back(makeTrifluoroMethyl());
    templates_.push_back(makeTertButyl());
    templates_.push_back(makeAmine());
}

bool BuiltinAbbreviationTable::findAbbreviation(const std::string& text,
                                                GroupTemplate* out) const {
    for (size_t i = 0; i < templates_.size(); ++i)
        if (templates_[i].abbreviation == text) { if (out) *out = templates_[i]; return true; }
    return false;
}

bool BuiltinAbbreviationTable::findTemplate(const std::string& templateId,
                                            GroupTemplate* out) const {
    for (size_t i = 0; i < templates_.size(); ++i)
        if (templates_[i].templateId == templateId) { if (out) *out = templates_[i]; return true; }
    return false;
}

std::vector<AbbreviationEntry> BuiltinAbbreviationTable::abbreviations() const {
    std::vector<AbbreviationEntry> out;
    for (size_t i = 0; i < templates_.size(); ++i)
        out.push_back(AbbreviationEntry(templates_[i].abbreviation,
                                        templates_[i].templateId,
                                        templates_[i].attachElement,
                                        templates_[i].expansion));
    return out;
}

// ---------------------------------------------------------------------------
// InjectedTemplateService
// ---------------------------------------------------------------------------
bool InjectedTemplateService::findAbbreviation(const std::string& text,
                                               GroupTemplate* out) const {
    for (size_t i = 0; i < templates_.size(); ++i)
        if (templates_[i].abbreviation == text) { if (out) *out = templates_[i]; return true; }
    return false;
}

bool InjectedTemplateService::findTemplate(const std::string& templateId,
                                           GroupTemplate* out) const {
    for (size_t i = 0; i < templates_.size(); ++i)
        if (templates_[i].templateId == templateId) { if (out) *out = templates_[i]; return true; }
    return false;
}

std::vector<AbbreviationEntry> InjectedTemplateService::abbreviations() const {
    std::vector<AbbreviationEntry> out;
    for (size_t i = 0; i < templates_.size(); ++i)
        out.push_back(AbbreviationEntry(templates_[i].abbreviation,
                                        templates_[i].templateId,
                                        templates_[i].attachElement,
                                        templates_[i].expansion));
    return out;
}

// ---------------------------------------------------------------------------
// Expansion planning
// ---------------------------------------------------------------------------
ExpansionPlan planExpansion(const ITemplateService& service,
                            const std::string& abbreviation,
                            core::ObjectId attachmentAtom,
                            double directionDeg,
                            double bondLength) {
    ExpansionPlan plan;
    plan.abbreviation = abbreviation;
    plan.attachmentAtom = attachmentAtom;

    GroupTemplate t;
    if (!service.findAbbreviation(abbreviation, &t)) {
        plan.error = "abbreviation is not in the template service";
        return plan;
    }
    if (!(bondLength > 0.0) || !std::isfinite(bondLength)) {
        plan.error = "bond length must be finite and positive";
        return plan;
    }
    if (!std::isfinite(directionDeg)) {
        plan.error = "direction must be finite";
        return plan;
    }

    plan.templateId = t.templateId;

    // Placement.  Unit-space templates are scaled by bondLength and rotated so the
    // group grows along directionDeg (core::direction() convention: 0 -> +x,
    // 90 -> visually up, i.e. -y).
    const double rad = directionDeg * 3.14159265358979323846 / 180.0;
    const double ux = std::cos(rad);
    const double uy = -std::sin(rad);
    const bool ringLike = t.contraction.requireExactRing;

    // Ring templates are authored around a CENTRE at the origin with unit radius.
    // The ring must sit entirely beyond the attachment, so the centre is pushed out
    // by (ring radius + one bond length).  Then the nearest ring atom - the one whose
    // template angle points back along -u - lands exactly one bond length from the
    // attachment, and no part of the ring overlaps it.  Shifting by only the radius
    // (the earlier defect) put the opposite ring atom exactly ON the attachment.
    double ringRadius = 0.0;
    if (ringLike) {
        for (size_t i = 0; i < t.atoms.size(); ++i) {
            const double r = std::sqrt(t.atoms[i].dx * t.atoms[i].dx +
                                       t.atoms[i].dy * t.atoms[i].dy);
            if (r > ringRadius) ringRadius = r;
        }
    }
    const double centreOffset = ringLike ? (ringRadius + 1.0) * bondLength : 0.0;

    for (size_t i = 0; i < t.atoms.size(); ++i) {
        const TemplateAtom& ta = t.atoms[i];
        const double lx = ta.dx * bondLength;
        const double ly = ta.dy * bondLength;
        const double ox = ringLike ? centreOffset : 0.0;
        const double px = lx + ox;
        const double py = ly;
        PlannedAtom pa;
        pa.element = ta.element;
        pa.labelText = ta.labelText;
        pa.hydrogenCount = ta.hydrogenCount;
        pa.charge = ta.charge;
        pa.position = core::Vec2(px * ux - py * uy, px * uy + py * ux);
        plan.atoms.push_back(pa);
    }

    for (size_t i = 0; i < t.bonds.size(); ++i) {
        PlannedBond pb;
        pb.fromTemplateIndex = t.bonds[i].from;
        pb.toTemplateIndex = t.bonds[i].to;
        pb.order = t.bonds[i].order;
        pb.display = t.bonds[i].display;
        pb.existingAtom = attachmentAtom;
        plan.bonds.push_back(pb);
    }

    // The templates in this file declare only INTRA-group bonds, so the attachment
    // bond from the existing atom to template atom 0 is added here.
    PlannedBond attach;
    attach.fromTemplateIndex = -1;
    attach.toTemplateIndex = 0;
    attach.existingAtom = attachmentAtom;
    attach.order = core::BondOrder::Single;
    plan.bonds.insert(plan.bonds.begin(), attach);

    plan.labelAtomIndex = 0;
    plan.ok = true;
    return plan;
}

// ---------------------------------------------------------------------------
// Contraction matching
// ---------------------------------------------------------------------------
namespace {

int countInternalBonds(const Subgraph& s) {
    int n = 0;
    for (size_t i = 0; i < s.bonds.size(); ++i) {
        bool fromIn = false, toIn = false;
        for (size_t k = 0; k < s.atoms.size(); ++k) {
            if (s.atoms[k].id == s.bonds[i].from) fromIn = true;
            if (s.atoms[k].id == s.bonds[i].to) toIn = true;
        }
        if (fromIn && toIn) ++n;
    }
    return n;
}

} // namespace

ContractionMatch planContraction(const ITemplateService& service,
                                 const std::string& abbreviation,
                                 const Subgraph& subgraph) {
    ContractionMatch m;
    m.abbreviation = abbreviation;

    GroupTemplate t;
    if (!service.findAbbreviation(abbreviation, &t)) {
        m.refuseReason = "abbreviation is not in the template service";
        return m;
    }
    m.templateId = t.templateId;

    // 1. Atom and bond counts must match the template exactly.  A user may have added
    //    a substituent to the group; contracting it then would delete that substituent
    //    along with the group - a chemistry change nobody asked for.
    if (static_cast<int>(subgraph.atoms.size()) != t.contraction.atomCount) {
        m.refuseReason = t.contraction.refuseReason;
        return m;
    }
    if (countInternalBonds(subgraph) != t.contraction.bondCount) {
        m.refuseReason = t.contraction.refuseReason;
        return m;
    }

    // 2. No atom of the group may have an extra substituent when the rule says so.
    if (t.contraction.requireNoExtraSubstituents) {
        for (size_t i = 0; i < subgraph.atoms.size(); ++i) {
            if (subgraph.atoms[i].externalBondCount > 0) {
                m.refuseReason = "a group atom has an extra substituent; contracting would delete it";
                return m;
            }
        }
    }

    // 3. Internal bond orders must be single when the rule requires it.
    if (t.contraction.requireAllSingleBonds) {
        for (size_t i = 0; i < subgraph.bonds.size(); ++i) {
            bool fromIn = false, toIn = false;
            for (size_t k = 0; k < subgraph.atoms.size(); ++k) {
                if (subgraph.atoms[k].id == subgraph.bonds[i].from) fromIn = true;
                if (subgraph.atoms[k].id == subgraph.bonds[i].to) toIn = true;
            }
            if (fromIn && toIn && subgraph.bonds[i].order != 1) {
                m.refuseReason = "a group bond is not single; refusing to contract";
                return m;
            }
        }
    }

    // 4. Exactly one attachment to the outside is what the group bonds back to.  Zero
    //    attachments means the group is free-floating and contracting it would leave a
    //    disconnected atom behind.
    if (subgraph.attachments.empty()) {
        m.refuseReason = "the group has no bond to the outside; contraction would leave a disconnected atom";
        return m;
    }

    for (size_t i = 0; i < subgraph.atoms.size(); ++i)
        m.atomsToDelete.push_back(subgraph.atoms[i].id);
    for (size_t i = 0; i < subgraph.bonds.size(); ++i) {
        bool fromIn = false, toIn = false;
        for (size_t k = 0; k < subgraph.atoms.size(); ++k) {
            if (subgraph.atoms[k].id == subgraph.bonds[i].from) fromIn = true;
            if (subgraph.atoms[k].id == subgraph.bonds[i].to) toIn = true;
        }
        if (fromIn && toIn) m.bondsToDelete.push_back(subgraph.bonds[i].from);
    }
    m.attachmentAtom = subgraph.attachments[0];
    m.labelAtom = subgraph.atoms[0].id;
    m.ok = true;
    return m;
}

} // namespace chemistry
} // namespace r62
