// =============================================================================
// re_round62_viewer/next/chemistry/valence.cpp
//
// T06 - the basic valence / implicit-hydrogen / warning rules.
//
// Every rule here is small enough to check by hand, and the test suite asserts the
// exact numbers the table implies.  Where a rule cannot be applied the functions
// say so (valid == false) instead of returning a plausible-looking zero.
// =============================================================================

#include "next/chemistry/valence.hpp"

#include <cstdio>

namespace r62 {
namespace chemistry {
namespace {

std::string intToStr(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

} // namespace

// ---------------------------------------------------------------------------
// Warnings metadata
// ---------------------------------------------------------------------------
const char* warningCodeName(WarningCode code) {
    switch (code) {
        case WarningCode::None:                return "none";
        case WarningCode::OverValence:         return "over-valence";
        case WarningCode::RadicalAndCharge:    return "radical-and-charge";
        case WarningCode::ImplicitHUnavailable:return "implicit-h-unavailable";
        case WarningCode::LabelNotInterpreted: return "label-not-interpreted";
        case WarningCode::ZeroValenceWithBonds:return "zero-valence-with-bonds";
        case WarningCode::IsotopeUnknown:      return "isotope-unknown";
        case WarningCode::AromaticOrderOnLabel:return "aromatic-order-on-label";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Implicit hydrogen
// ---------------------------------------------------------------------------
// The rule, so a reviewer can check it against the table:
//   valence = conventionalValenceFor(element, bondOrderSum)
//   hydrogens = valence + charge - bondOrderSum
// Clamping at zero means an over-coordinated atom reports overCoordination and
// zero hydrogens rather than a negative count that would be rendered as text.
HydrogenResult implicitHydrogens(const AtomEnvironment& env) {
    HydrogenResult r;

    // A label override that is NOT an element means the atom is not a single
    // element any more (it is Ph, R1, a query atom or user text).  Counting
    // hydrogens from a valence would be inventing chemistry, so we do not.
    if (env.hasLabelOverride && env.labelKind != LabelKind::Element) {
        r.note = "atom carries a non-element label; no valence prediction";
        return r;
    }

    const ElementInfo& info = elementInfo(env.element);
    const int valence = conventionalValenceFor(env.element, env.bondOrderSum);
    r.conventionalValence = valence;
    if (valence < 0) {
        r.note = info.valenceNote[0] ? info.valenceNote
                                     : "element has no conventional valence";
        return r;
    }

    // Charged atoms shift the electron count: N+ has one fewer bond-worth, O- has
    // one more.  This is the standard electron-bookkeeping adjustment and is the
    // reason a charged N/O example in the acceptance list behaves the way it does.
    const int expected = valence + env.charge;
    int count = expected - env.bondOrderSum;
    if (count < 0) {
        r.overCoordinated = true;
        count = 0;
    }

    // An explicit hydrogen count on the atom OVERRIDES the prediction.  The caller
    // asked for a specific number; we report it and flag nothing, because an
    // explicit value is a deliberate user decision, not a rule violation.
    if (env.explicitHydrogens >= 0) count = env.explicitHydrogens;

    r.count = count;
    r.valid = true;
    if (env.aromaticBondCount > 0) {
        r.aromaticApproximation = true;
        r.note = "aromatic bonds counted as order 1; hydrogen count may be low";
    }
    if (info.isMetal) {
        // Metals normally reach zero hydrogens; if the table has no valence this
        // branch is unreachable, but a table-valence metal (Zn, Al) is not given
        // hydrogens by convention.
        r.count = 0;
        r.note = "metal: hydrogens not implied";
    }
    return r;
}

HydrogenResult implicitHydrogensSimple(int element, int charge, int bondOrderSum,
                                       bool aromatic) {
    AtomEnvironment env;
    env.element = element;
    env.charge = charge;
    env.bondOrderSum = bondOrderSum;
    env.aromaticBondCount = aromatic ? 1 : 0;
    return implicitHydrogens(env);
}

// ---------------------------------------------------------------------------
// Warnings
// ---------------------------------------------------------------------------
std::vector<ChemicalWarning> warningsForAtom(const AtomEnvironment& env, int atomId) {
    std::vector<ChemicalWarning> out;
    const ElementInfo& info = elementInfo(env.element);

    // 1. Over-valence.  Only possible when the element HAS a conventional valence.
    const int valence = conventionalValenceFor(env.element, env.bondOrderSum);
    if (valence > 0 && env.bondOrderSum > valence) {
        out.push_back(ChemicalWarning(WarningCode::OverValence, atomId,
            std::string("bond order sum ") + intToStr(env.bondOrderSum) +
            " exceeds " + info.symbol + " valence " + intToStr(valence)));
    }

    // 2. An element with no conventional valence that is nevertheless drawn bonded.
    if (valence < 0 && env.bondCount > 0) {
        out.push_back(ChemicalWarning(WarningCode::ZeroValenceWithBonds, atomId,
            std::string(info.symbol) + ": " +
            (info.valenceNote[0] ? info.valenceNote : "no valence rule")));
    }

    // 3. Radical together with a formal charge.  Legal (a radical cation is real),
    //    unusual enough to be worth surfacing.
    if (env.radical != 0 && env.charge != 0) {
        out.push_back(ChemicalWarning(WarningCode::RadicalAndCharge, atomId,
            "atom carries both a radical and a formal charge"));
    }

    // 4. Arbitrary text label: this module did NOT interpret it, and the topology
    //    is whatever it was.  Saying so is the whole point of preserving it.
    if (env.hasLabelOverride && env.labelKind == LabelKind::Arbitrary) {
        out.push_back(ChemicalWarning(WarningCode::LabelNotInterpreted, atomId,
            "label text preserved; no valence or hydrogen prediction applies"));
    }

    // 5. An isotope the module table does not know.  The number is still valid and
    //    persisted; only the mass helper cannot resolve it.
    if (env.isotope > 0) {
        IsotopeInfo iso;
        if (!knownIsotope(env.element, env.isotope, &iso)) {
            out.push_back(ChemicalWarning(WarningCode::IsotopeUnknown, atomId,
                std::string("isotope ") + intToStr(env.isotope) + " of " +
                info.symbol + " is not in this module's isotope table"));
        }
    }

    // 6. An aromatic bond meeting a label we did not interpret.  The aromatic ring
    //    perception is not implemented, so this is a "do not trust the drawing"
    //    notice rather than a chemical claim.
    if (env.aromaticBondCount > 0 && env.hasLabelOverride &&
        env.labelKind != LabelKind::Element) {
        out.push_back(ChemicalWarning(WarningCode::AromaticOrderOnLabel, atomId,
            "aromatic bond meets a non-element label; aromaticity not analysed"));
    }

    // 7. Implicit hydrogens unavailable for a bonded atom of an element the table
    //    cannot count.  Distinct from 2 because it is about the H count, not the
    //    bond count.
    if (valence < 0 && env.bondCount == 0 && env.element != 0) {
        out.push_back(ChemicalWarning(WarningCode::ImplicitHUnavailable, atomId,
            std::string(info.symbol) + ": implicit hydrogen count not available"));
    }

    return out;
}

// ---------------------------------------------------------------------------
// Mass
// ---------------------------------------------------------------------------
MassResult estimateMass(const std::vector<MassAtom>& atoms, bool allowNaturalWeight) {
    MassResult r;
    double total = 0.0;
    bool allExact = true;
    for (size_t i = 0; i < atoms.size(); ++i) {
        const MassAtom& a = atoms[i];
        const ElementInfo& info = elementInfo(a.element);
        if (a.element < 1 || a.element > 118) {
            ++r.unresolvedAtoms;
            allExact = false;
            continue;
        }
        double atomMass = 0.0;
        if (a.isotope > 0) {
            IsotopeInfo iso;
            if (knownIsotope(a.element, a.isotope, &iso)) {
                atomMass = iso.exactMass;
            } else if (allowNaturalWeight) {
                atomMass = info.weight;
                allExact = false;
            } else {
                ++r.unresolvedAtoms;
                allExact = false;
                continue;
            }
        } else {
            // Natural abundance: the standard atomic weight is an AVERAGE mass.
            atomMass = info.weight;
            allExact = false;
        }
        total += atomMass;
        // Hydrogens attached to this atom.  Hydrogen has one isotope (protium) as
        // the default; the caller is expected to have already folded the implicit
        // hydrogens into hydrogenCount, so an unassigned H contributes weight 1.008.
        for (int h = 0; h < a.hydrogenCount; ++h) {
            total += elementInfo(1).weight;
            allExact = false;
        }
    }
    r.value = total;
    r.exact = allExact && r.unresolvedAtoms == 0;
    r.ok = r.unresolvedAtoms == 0;
    if (!r.ok) {
        r.note = "some atoms have no isotope in this module table";
    } else if (!r.exact) {
        r.note = "natural-abundance average weights used; this is NOT an exact mass";
    } else {
        r.note = "exact mass from assigned isotopes";
    }
    return r;
}

// ---------------------------------------------------------------------------
// Sanity helpers
// ---------------------------------------------------------------------------
bool validChargeValue(int charge) { return charge >= -15 && charge <= 15; }

bool validRadicalValue(int radical) { return radical >= 0 && radical <= 3; }

// core::Radical: None / Singlet / Doublet / Triplet.  All three real values are
// supported; a value outside the enum is not a radical this module understands.
bool radicalSupported(int radical) {
    return radical == static_cast<int>(core::Radical::None) ||
           radical == static_cast<int>(core::Radical::Singlet) ||
           radical == static_cast<int>(core::Radical::Doublet) ||
           radical == static_cast<int>(core::Radical::Triplet);
}

// ---------------------------------------------------------------------------
// Evidence summary
// ---------------------------------------------------------------------------
std::string describeValence(int element, int bondOrderSum, int charge) {
    const ElementInfo& info = elementInfo(element);
    std::string s = std::string(info.symbol) + "(" + intToStr(element) + ")";
    if (info.valenceLow < 0) {
        s += " valence=none";
        if (info.valenceNote[0]) { s += " ["; s += info.valenceNote; s += "]"; }
    } else if (info.valenceLow == info.valenceHigh) {
        s += " valence=" + intToStr(info.valenceLow);
    } else {
        s += " valence=" + intToStr(info.valenceLow) + "/" + intToStr(info.valenceHigh);
    }
    s += " bonds=" + intToStr(bondOrderSum);
    s += " charge=" + intToStr(charge);
    HydrogenResult h = implicitHydrogensSimple(element, charge, bondOrderSum, false);
    s += " H=";
    s += h.valid ? intToStr(h.count) : std::string("n/a");
    if (h.overCoordinated) s += " OVER-COORDINATED";
    return s;
}

} // namespace chemistry
} // namespace r62
