#include "next/templates_queries/catalog.hpp"

#include <cmath>
#include <cstdio>

namespace r62 {
namespace templates_queries {

// ---------------------------------------------------------------------------
// The id namespace T06 asked T13 to own
// ---------------------------------------------------------------------------
// chemistry/INTERFACES.md 4.2 lists twelve abbreviations and twelve provisional
// ids.  T13 CONFIRMS those ids verbatim: renaming them would invalidate any
// document already written by the built-in T06 table, and a rename buys nothing
// because the namespace is opaque.  They are declared in catalog.hpp so the
// confirmation is a link-time fact rather than a note in a document.
const char* const kIdGroupMethyl          = "r62.template.group.methyl";
const char* const kIdGroupEthyl           = "r62.template.group.ethyl";
const char* const kIdGroupPhenyl          = "r62.template.group.phenyl";
const char* const kIdGroupBenzyl          = "r62.template.group.benzyl";
const char* const kIdGroupAcetyl          = "r62.template.group.acetyl";
const char* const kIdGroupMethoxy         = "r62.template.group.methoxy";
const char* const kIdGroupHydroxyl        = "r62.template.group.hydroxyl";
const char* const kIdGroupNitro           = "r62.template.group.nitro";
const char* const kIdGroupCarboxyl        = "r62.template.group.carboxyl";
const char* const kIdGroupTrifluoromethyl = "r62.template.group.trifluoromethyl";
const char* const kIdGroupTertButyl       = "r62.template.group.tertbutyl";
const char* const kIdGroupAmine           = "r62.template.group.amine";

// Ring ids, also owned here.
const char* const kIdRingBenzene      = "r62.template.ring.benzene";
const char* const kIdRingCyclopropane = "r62.template.ring.cyclopropane";
const char* const kIdRingCyclobutane  = "r62.template.ring.cyclobutane";
const char* const kIdRingCyclopentane = "r62.template.ring.cyclopentane";
const char* const kIdRingCyclohexane  = "r62.template.ring.cyclohexane";
const char* const kIdRingPyridine     = "r62.template.ring.pyridine";
const char* const kIdRingFuran        = "r62.template.ring.furan";
const char* const kIdRingPyrrole      = "r62.template.ring.pyrrole";
const char* const kIdScaffoldNaphthalene = "r62.template.scaffold.naphthalene";
const char* const kIdScaffoldIndole      = "r62.template.scaffold.indole";
const char* const kIdQueryRGroup1        = "r62.template.query.rgroup1";
const char* const kIdQueryRGroup2        = "r62.template.query.rgroup2";

namespace {

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
// A regular N-gon of radius r (circumradius), first vertex at angle startDeg, in
// the T01 convention (x right, y down; angle is CCW in math orientation, which
// core::direction() already encodes).  Every ring template is built from this, so
// "the ring is regular" is a property of one function and not of N hand-computed
// coordinate lists.
void regularPolygon(int n, double r, double startDeg, std::vector<core::Vec2>* out) {
    out->clear();
    for (int i = 0; i < n; ++i) {
        const double a = startDeg + 360.0 * static_cast<double>(i) / static_cast<double>(n);
        core::Vec2 d = core::direction(a);
        out->push_back(core::Vec2(d.x * r, d.y * r));
    }
}

CatalogAtom atomAt(int element, const core::Vec2& p) {
    CatalogAtom a;
    a.element = element;
    a.offset = p;
    return a;
}

CatalogAtom labelAtomAt(const std::string& text, const core::Vec2& p) {
    CatalogAtom a;
    a.element = 0;
    a.labelText = text;
    a.offset = p;
    return a;
}

CatalogBond bondOf(int f, int t, core::BondOrder o) {
    CatalogBond b;
    b.from = f;
    b.to = t;
    b.order = o;
    return b;
}

// Attach one neighbour atom to a template.  `hostIndex` is the atom the group
// attaches THROUGH (always index 0 by convention below), `dirDeg` the direction the
// group grows, `len` the bond length.  Returns the new atom index.
int appendTerminal(CatalogEntry* e, int element, int hostIndex,
                   double dirDeg, double len, core::BondOrder o,
                   int charge = 0) {
    const core::Vec2 base = e->atoms[static_cast<size_t>(hostIndex)].offset;
    core::Vec2 d = core::direction(dirDeg);
    CatalogAtom a = atomAt(element, core::Vec2(base.x + d.x * len, base.y + d.y * len));
    a.charge = charge;
    const int idx = static_cast<int>(e->atoms.size());
    e->atoms.push_back(a);
    e->bonds.push_back(bondOf(hostIndex, idx, o));
    return idx;
}

void anchor1(CatalogEntry* e, int atomIndex) {
    CatalogAnchor a;
    a.atomIndex = atomIndex;
    a.rank = 1;
    e->anchors.push_back(a);
}

// Every built-in cites this constant, because every single one is drawn here from
// periodic-table facts and standard valence rather than copied from an asset.
const char* const kProjectLicense = "project-authored";
const char* const kSourceValence  =
    "authored in T13 from the periodic table and standard valence; not copied from any asset";
const char* const kSourceGuide    =
    "authored in T13; semantics cross-checked against ChemDraw User Guide PDF pp.326-354";

} // namespace

// ---------------------------------------------------------------------------
// The built-in library
// ---------------------------------------------------------------------------
// Ordering here is the picker ordering.  Every entry declares an explicit anchor,
// and the ANCHOR ATOM IS ALWAYS A REAL ATOM OF THE TEMPLATE (index 0 for the
// groups below), because the instantiation contract (instantiate.hpp) is: the
// anchor atom is the one that bonds to the existing structure.
void buildBuiltins(std::vector<CatalogEntry>* out) {
    out->clear();
    const double L1 = 14.4;   // one standard bond length, document units

    // ---- rings -------------------------------------------------------------
    {
        const int N = 6;
        std::vector<core::Vec2> pts;
        // Vertex 0 at 90 deg (visually UP) so a benzene sprouts upward by default,
        // matching the chemists' expectation and the guide's ring placement.
        regularPolygon(N, L1 / (2.0 * std::sin(3.14159265358979323846 / N)), 90.0, &pts);
        CatalogEntry e;
        e.templateId = kIdRingBenzene;
        e.name = "Benzene";
        e.group = TemplateGroup::Ring;
        e.license = kProjectLicense;
        e.source = kSourceValence;
        // Deliberately NOT an abbreviation: "Ph" names the PHENYL group (a ring with
        // an attachment), which is its own catalogue entry.  Aliasing benzene to
        // "Ph" would give one nickname two owners and make the abbreviation index
        // ambiguous.
        e.meta["formula"] = "C6H6";
        for (int i = 0; i < N; ++i) {
            CatalogAtom a = atomAt(6, pts[static_cast<size_t>(i)]);
            e.atoms.push_back(a);
        }
        for (int i = 0; i < N; ++i) {
            CatalogBond b = bondOf(i, (i + 1) % N, core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Cyclopropane / cyclobutane / cyclopentane / cyclohexane: one shape, four
        // parameters.  This is the same "N commands are parameters, not N
        // algorithms" argument the bond module makes.
        const char* ids[4] = { kIdRingCyclopropane, kIdRingCyclobutane,
                               kIdRingCyclopentane, kIdRingCyclohexane };
        const char* names[4] = { "Cyclopropane", "Cyclobutane",
                                 "Cyclopentane", "Cyclohexane" };
        for (int k = 0; k < 4; ++k) {
            const int N = 3 + k;
            std::vector<core::Vec2> pts;
            regularPolygon(N, L1 / (2.0 * std::sin(3.14159265358979323846 / N)), 90.0, &pts);
            CatalogEntry e;
            e.templateId = ids[k];
            e.name = names[k];
            e.group = TemplateGroup::Ring;
            e.license = kProjectLicense;
            e.source = kSourceValence;
            for (int i = 0; i < N; ++i) e.atoms.push_back(atomAt(6, pts[static_cast<size_t>(i)]));
            for (int i = 0; i < N; ++i) e.bonds.push_back(bondOf(i, (i + 1) % N, core::BondOrder::Single));
            anchor1(&e, 0);
            out->push_back(e);
        }
    }
    {
        // Pyridine: benzene with one ring nitrogen.  Aromatic system, so the
        // semantics axis is set and the order stays Aromatic (never order==4).
        const int N = 6;
        std::vector<core::Vec2> pts;
        regularPolygon(N, L1 / (2.0 * std::sin(3.14159265358979323846 / N)), 90.0, &pts);
        CatalogEntry e;
        e.templateId = kIdRingPyridine;
        e.name = "Pyridine";
        e.group = TemplateGroup::Ring;
        e.license = kProjectLicense;
        e.source = kSourceValence;
        e.meta["formula"] = "C5H5N";
        for (int i = 0; i < N; ++i) {
            CatalogAtom a = atomAt(i == 0 ? 7 : 6, pts[static_cast<size_t>(i)]);
            a.aromatic = true;
            e.atoms.push_back(a);
        }
        for (int i = 0; i < N; ++i) {
            CatalogBond b = bondOf(i, (i + 1) % N, core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        anchor1(&e, 1);   // anchor on a carbon, not on the nitrogen
        out->push_back(e);
    }
    {
        // Furan and pyrrole: five-membered aromatics with one heteroatom.
        const char* ids[2] = { kIdRingFuran, kIdRingPyrrole };
        const char* names[2] = { "Furan", "Pyrrole" };
        const int hetero[2] = { 8, 7 };
        const char* form[2] = { "C4H4O", "C4H5N" };
        for (int k = 0; k < 2; ++k) {
            const int N = 5;
            std::vector<core::Vec2> pts;
            regularPolygon(N, L1 / (2.0 * std::sin(3.14159265358979323846 / N)), 90.0, &pts);
            CatalogEntry e;
            e.templateId = ids[k];
            e.name = names[k];
            e.group = TemplateGroup::Ring;
            e.license = kProjectLicense;
            e.source = kSourceValence;
            e.meta["formula"] = form[k];
            for (int i = 0; i < N; ++i) {
                CatalogAtom a = atomAt(i == 0 ? hetero[k] : 6, pts[static_cast<size_t>(i)]);
                a.aromatic = true;
                e.atoms.push_back(a);
            }
            for (int i = 0; i < N; ++i) {
                CatalogBond b = bondOf(i, (i + 1) % N, core::BondOrder::Aromatic);
                b.semantics = core::BondSemantics::Aromatic;
                e.bonds.push_back(b);
            }
            anchor1(&e, 1);
            out->push_back(e);
        }
    }

    // ---- functional groups (the ids T06 already uses) -----------------------
    {
        // Methyl: the anchor IS the carbon, so instantiating "Me" onto an existing
        // atom means bonding that atom to this carbon.  hydrogenCount is left at -1
        // (compute) so the valence module, not the template, decides H count.
        CatalogEntry e;
        e.templateId = kIdGroupMethyl;
        e.name = "Methyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "Me";
        e.meta["formula"] = "CH3";
        e.atoms.push_back(atomAt(6, core::Vec2(0.0, 0.0)));
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        CatalogEntry e;
        e.templateId = kIdGroupEthyl;
        e.name = "Ethyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "Et";
        e.meta["formula"] = "C2H5";
        e.atoms.push_back(atomAt(6, core::Vec2(0.0, 0.0)));
        e.atoms.push_back(atomAt(6, core::Vec2(L1, 0.0)));
        e.bonds.push_back(bondOf(0, 1, core::BondOrder::Single));
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        CatalogEntry e;
        e.templateId = kIdGroupHydroxyl;
        e.name = "Hydroxyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "OH";
        e.meta["formula"] = "OH";
        e.atoms.push_back(atomAt(8, core::Vec2(0.0, 0.0)));
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Methoxy: attachment carbon is index 0; the oxygen hangs off it so the
        // group reads -O-CH3 when attached through the O.  The anchor is the
        // OXYGEN (index 1), because "OMe" attaches through O, not through C.
        CatalogEntry e;
        e.templateId = kIdGroupMethoxy;
        e.name = "Methoxy";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "OMe";
        e.meta["formula"] = "OCH3";
        e.atoms.push_back(atomAt(6, core::Vec2(L1, 0.0)));   // the CH3
        e.atoms.push_back(atomAt(8, core::Vec2(0.0, 0.0)));   // the O
        e.bonds.push_back(bondOf(1, 0, core::BondOrder::Single));
        anchor1(&e, 1);
        out->push_back(e);
    }
    {
        // Phenyl: a real benzene ring with its own atoms.  The T06 expander creates
        // the ring itself; this entry exists so the PICKER can insert a phenyl too,
        // and so both paths produce the same geometry.
        const int N = 6;
        std::vector<core::Vec2> pts;
        regularPolygon(N, L1 / (2.0 * std::sin(3.14159265358979323846 / N)), 90.0, &pts);
        CatalogEntry e;
        e.templateId = kIdGroupPhenyl;
        e.name = "Phenyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "Ph";
        e.meta["formula"] = "C6H5";
        for (int i = 0; i < N; ++i) e.atoms.push_back(atomAt(6, pts[static_cast<size_t>(i)]));
        for (int i = 0; i < N; ++i) {
            CatalogBond b = bondOf(i, (i + 1) % N, core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Benzyl: phenyl + one CH2.  Anchor is the CH2 carbon (index 6), so "Bn"
        // attaches through the methylene.
        const int N = 6;
        std::vector<core::Vec2> pts;
        regularPolygon(N, L1 / (2.0 * std::sin(3.14159265358979323846 / N)), 90.0, &pts);
        CatalogEntry e;
        e.templateId = kIdGroupBenzyl;
        e.name = "Benzyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "Bn";
        e.meta["formula"] = "C7H7";
        for (int i = 0; i < N; ++i) e.atoms.push_back(atomAt(6, pts[static_cast<size_t>(i)]));
        for (int i = 0; i < N; ++i) {
            CatalogBond b = bondOf(i, (i + 1) % N, core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        const int ch2 = appendTerminal(&e, 6, 0, -90.0, L1, core::BondOrder::Single);
        anchor1(&e, ch2);
        out->push_back(e);
    }
    {
        // Nitro: N with two double-bonded O.  Anchored on the nitrogen.
        CatalogEntry e;
        e.templateId = kIdGroupNitro;
        e.name = "Nitro";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "NO2";
        e.meta["formula"] = "NO2";
        CatalogAtom n = atomAt(7, core::Vec2(0.0, 0.0));
        n.charge = 1;
        e.atoms.push_back(n);
        int o1 = appendTerminal(&e, 8, 0, 120.0, L1, core::BondOrder::Double);
        int o2 = appendTerminal(&e, 8, 0, 240.0, L1, core::BondOrder::Double);
        e.atoms[static_cast<size_t>(o1)].charge = -1;
        e.atoms[static_cast<size_t>(o2)].charge = -1;
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Acetyl: CH3-C(=O)-, anchored on the CARBONYL carbon, so attaching "Ac"
        // gives the acyl group rather than an acetaldehyde.
        CatalogEntry e;
        e.templateId = kIdGroupAcetyl;
        e.name = "Acetyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "Ac";
        e.meta["formula"] = "C2H3O";
        e.atoms.push_back(atomAt(6, core::Vec2(0.0, 0.0)));   // carbonyl C: anchor
        appendTerminal(&e, 6, 0, -90.0, L1, core::BondOrder::Single);  // CH3
        appendTerminal(&e, 8, 0, 0.0, L1, core::BondOrder::Double);    // =O
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Carboxyl: HO-C(=O)-, anchored on the carbonyl carbon.
        CatalogEntry e;
        e.templateId = kIdGroupCarboxyl;
        e.name = "Carboxyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "CO2H";
        e.meta["formula"] = "CO2H";
        e.atoms.push_back(atomAt(6, core::Vec2(0.0, 0.0)));
        appendTerminal(&e, 8, 0, 0.0, L1, core::BondOrder::Double);    // =O
        int oh = appendTerminal(&e, 8, 0, 180.0, L1, core::BondOrder::Single);
        (void)oh;
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Trifluoromethyl: C with three F.  Anchored on the carbon.
        CatalogEntry e;
        e.templateId = kIdGroupTrifluoromethyl;
        e.name = "Trifluoromethyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "CF3";
        e.meta["formula"] = "CF3";
        e.atoms.push_back(atomAt(6, core::Vec2(0.0, 0.0)));
        for (int k = 0; k < 3; ++k) appendTerminal(&e, 9, 0, 90.0 + 120.0 * k, L1, core::BondOrder::Single);
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // tert-Butyl: central quaternary carbon with three methyls.  The central
        // carbon is the ANCHOR, which is why the template starts at index 0 with an
        // atom that already has three bonds: attaching it is what makes it
        // quaternary, and no extra carbon has to be synthesised.
        CatalogEntry e;
        e.templateId = kIdGroupTertButyl;
        e.name = "tert-Butyl";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "t-Bu";
        e.meta["formula"] = "C4H9";
        e.atoms.push_back(atomAt(6, core::Vec2(0.0, 0.0)));
        for (int k = 0; k < 3; ++k) appendTerminal(&e, 6, 0, 90.0 + 120.0 * k, L1, core::BondOrder::Single);
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Amine: NH2, anchored on the nitrogen.
        CatalogEntry e;
        e.templateId = kIdGroupAmine;
        e.name = "Amine";
        e.group = TemplateGroup::FunctionalGroup;
        e.license = kProjectLicense;
        e.source = kSourceGuide;
        e.meta["abbreviation"] = "NH2";
        e.meta["formula"] = "NH2";
        e.atoms.push_back(atomAt(7, core::Vec2(0.0, 0.0)));
        anchor1(&e, 0);
        out->push_back(e);
    }

    // ---- fused scaffolds ---------------------------------------------------
    {
        // Naphthalene: two fused six-rings sharing an edge.  Built from one
        // regular hexagon plus a second hexagon reflected across the shared edge,
        // so the fusion geometry is derived rather than hand-entered.
        const double r = L1 / (2.0 * std::sin(3.14159265358979323846 / 6));
        CatalogEntry e;
        e.templateId = kIdScaffoldNaphthalene;
        e.name = "Naphthalene";
        e.group = TemplateGroup::Scaffold;
        e.license = kProjectLicense;
        e.source = kSourceValence;
        e.meta["formula"] = "C10H8";
        // Hexagon A: vertices at 90,150,210,270,330,30 degrees.
        std::vector<core::Vec2> a6;
        regularPolygon(6, r, 90.0, &a6);
        // Shared edge is A1-A2 (150 and 210 deg, i.e. the left edge).  Hexagon B is
        // A mirrored across the vertical line x = a6[1].x.
        for (size_t i = 0; i < a6.size(); ++i) e.atoms.push_back(atomAt(6, a6[i]));
        for (size_t i = 0; i < a6.size(); ++i) {
            CatalogBond b = bondOf(static_cast<int>(i), static_cast<int>((i + 1) % a6.size()),
                                   core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        // Ring B shares atoms 1 and 2 of A (the left edge) and adds four new atoms.
        const double x0 = a6[1].x;
        const core::Vec2 b3(x0 + (x0 - a6[0].x), a6[0].y);
        const core::Vec2 b4(x0 + (x0 - a6[5].x), a6[5].y);
        const core::Vec2 b5(x0 + (x0 - a6[4].x), a6[4].y);
        const core::Vec2 b6(x0 + (x0 - a6[3].x), a6[3].y);
        const int i3 = 6, i4 = 7, i5 = 8, i6 = 9;
        e.atoms.push_back(atomAt(6, b3));
        e.atoms.push_back(atomAt(6, b4));
        e.atoms.push_back(atomAt(6, b5));
        e.atoms.push_back(atomAt(6, b6));
        // Ring B walk: A2 -> A1 (shared, reversed) -> b6 -> b5 -> b4 -> b3 -> A2
        const int shared[2] = { 2, 1 };
        const int walk[6] = { shared[0], shared[1], i6, i5, i4, i3 };
        for (int i = 0; i < 6; ++i) {
            const int f = walk[i], t = walk[(i + 1) % 6];
            // Edge 0 (A2-A1) and edge 5 (i3-A2) already exist: A2-A1 is the shared
            // ring bond added above, and i3-A2 is this ring's closing edge that the
            // NEXT loop iteration would add.  Adding either again is a duplicate bond,
            // which Document::addBond would reject mid-instantiation.
            if (i == 0 || i == 5) continue;
            CatalogBond b = bondOf(f, t, core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        // Close the second ring explicitly: i3 -> A2.
        {
            CatalogBond b = bondOf(i3, shared[0], core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        anchor1(&e, 0);
        out->push_back(e);
    }
    {
        // Indole: a benzene fused to a pyrrole.  Kept deliberately simple and
        // DERIVED from the benzene ring, because hand-entering fused coordinates is
        // how a library accumulates templates that overlap when instantiated.
        const double r = L1 / (2.0 * std::sin(3.14159265358979323846 / 6));
        std::vector<core::Vec2> a6;
        regularPolygon(6, r, 90.0, &a6);
        CatalogEntry e;
        e.templateId = kIdScaffoldIndole;
        e.name = "Indole";
        e.group = TemplateGroup::Scaffold;
        e.license = kProjectLicense;
        e.source = kSourceValence;
        e.meta["formula"] = "C8H7N";
        for (size_t i = 0; i < a6.size(); ++i) e.atoms.push_back(atomAt(6, a6[i]));
        for (size_t i = 0; i < a6.size(); ++i) {
            CatalogBond b = bondOf(static_cast<int>(i), static_cast<int>((i + 1) % a6.size()),
                                   core::BondOrder::Aromatic);
            b.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b);
        }
        // Five-membered ring sharing the left edge (A1-A2), with the N at the apex.
        const double x0 = a6[1].x;
        const double yMid = (a6[1].y + a6[2].y) * 0.5;
        const core::Vec2 apex(x0 + r, yMid);
        const core::Vec2 l1(x0 + r * 0.62, a6[1].y - L1 * 0.28);
        const core::Vec2 l2(x0 + r * 0.62, a6[2].y + L1 * 0.28);
        const int iApex = 6, iL1 = 7, iL2 = 8;
        e.atoms.push_back(atomAt(7, apex));
        e.atoms.push_back(atomAt(6, l1));
        e.atoms.push_back(atomAt(6, l2));
        {
            CatalogBond b1 = bondOf(2, iL2, core::BondOrder::Aromatic);
            b1.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b1);
            CatalogBond b2 = bondOf(iL2, iApex, core::BondOrder::Single);
            b2.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b2);
            CatalogBond b3 = bondOf(iApex, iL1, core::BondOrder::Single);
            b3.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b3);
            CatalogBond b4 = bondOf(iL1, 1, core::BondOrder::Aromatic);
            b4.semantics = core::BondSemantics::Aromatic;
            e.bonds.push_back(b4);
        }
        anchor1(&e, 0);
        out->push_back(e);
    }

    // ---- query-flavoured templates ------------------------------------------
    {
        // R1 / R2 sites: a labelled node with NO element.  This is what makes the
        // distinction "R group is a variable site" representable: element stays 0
        // and the label carries the variable, so no code can read it as carbon.
        const char* ids[2] = { kIdQueryRGroup1, kIdQueryRGroup2 };
        const char* names[2] = { "R1 site", "R2 site" };
        const char* labels[2] = { "R1", "R2" };
        const int indexes[2] = { 1, 2 };
        for (int k = 0; k < 2; ++k) {
            CatalogEntry e;
            e.templateId = ids[k];
            e.name = names[k];
            e.group = TemplateGroup::Labelled;
            e.license = kProjectLicense;
            e.source = kSourceGuide;
            // Deliberately NOT registered as an "abbreviation": an R site is a
            // VARIABLE, and listing it would make T06 treat "R1" as a nickname
            // with a fixed expansion, which is exactly the confusion T13 exists
            // to prevent.  The label and index are carried instead.
            e.meta["rgroupIndex"] = (k == 0) ? "1" : "2";
            e.meta["nodeType"] = "RGroup";
            CatalogAtom a = labelAtomAt(labels[k], core::Vec2(0.0, 0.0));
            e.atoms.push_back(a);
            anchor1(&e, 0);
            (void)indexes[k];
            out->push_back(e);
        }
    }
}

// ---------------------------------------------------------------------------
// Registry access
// ---------------------------------------------------------------------------
const TemplateRegistry& builtinRegistry() {
    // One process-wide instance, built once.  A function-local static is used so
    // there is no static-initialisation-order dependency with the tests.
    static TemplateRegistry registry;
    static bool built = false;
    if (!built) {
        std::vector<CatalogEntry> entries;
        buildBuiltins(&entries);
        for (size_t i = 0; i < entries.size(); ++i) registry.addQuiet(entries[i]);
        built = true;
    }
    return registry;
}

std::string builtinCatalogText() {
    return serializeCatalog(builtinRegistry().entries());
}

} // namespace templates_queries
} // namespace r62
