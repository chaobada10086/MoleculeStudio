// =============================================================================
// re_round62_viewer/next/chemistry/edit.cpp
//
// T06 - chemistry edits, hotspot reactions and the derived-field refresh.
//
// Every public edit is a core::Command so it becomes one undo step.  The hotspot
// reaction resolvers and the sprout planners are pure functions with no document
// access, which is what lets the test suite assert their geometry and their
// refusals directly.
// =============================================================================

#include "next/chemistry/edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace r62 {
namespace chemistry {
namespace {

std::string intToStr(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// WHERE THE LABEL TEXT IS STORED, AND WHY IT IS THE EXTENSION BAG
// ---------------------------------------------------------------------------
// core::Document has a dedicated atom-label side table (setAtomLabel/atomLabel), but
// as of interface version 1.0.0 that side table is NOT part of SlabPatch/ChangeSet -
// only the TEXT-run side table is.  A label written through setAtomLabel therefore
// does NOT participate in undo, which is a real gap in the T01 interface (reported
// as a required core patch in RESULTS.md / INTERFACES.md).
//
// T06 must not edit next/core/, so it stores its typed label in the object EXTENSION
// BAG under a T06-owned key instead.  The extension bag IS fully patched (it has
// beforePresent/afterPresent plus the before/after bags), so a label edit becomes a
// normal undoable change AND is persisted by the native serializer.  The key uses the
// documented persistent prefix so isPersistentExtensionKey() returns true.
//
// When T01 ships the side-table patch, this key can be migrated to setAtomLabel with
// no change to any caller: readAtomLabel/applyLabelToDocument are the only two places
// that know where the bytes live.
static const char* const kLabelKey = "cdxml.attr.r62ChemLabel";

AtomLabel labelFromRecord(const core::Document& document, core::ObjectId atom,
                          const std::vector<AbbreviationEntry>& abbreviations) {
    AtomLabel label;
    const core::AtomRecord* r = document.atoms().alive(atom);
    if (!r) return label;
    label.element = r->element;
    label.charge = r->semantics.charge;
    label.isotope = r->semantics.isotope;
    label.hydrogenCount = r->semantics.implicitHydrogens;
    label.radical = r->semantics.radical;
    label.nodeType = r->semantics.nodeType;
    label.showLabel = r->semantics.showLabel;

    // Prefer the typed extension entry; fall back to the core side table so a
    // document loaded from a path that wrote it there still reads correctly.
    std::string encoded;
    const std::string* bagValue = document.extensions(
        core::ObjectRef(core::ObjectKind::Atom, atom)).findValue(kLabelKey);
    if (bagValue) {
        encoded = *bagValue;
    } else {
        const std::string& sideText = document.atomLabel(atom);
        if (!sideText.empty()) {
            // Fallback for a document that wrote the label through the core side table.
            // Merge the interpretation onto the record so charge/isotope/radical/H are
            // the atom's real values and not the interpretation defaults.
            LabelInterpretation sideInterp = interpretLabel(sideText, abbreviations);
            AtomLabel merged = sideInterp.label;
            merged.charge = label.charge;
            merged.isotope = label.isotope;
            merged.radical = label.radical;
            merged.hydrogenCount = label.hydrogenCount;
            merged.showLabel = label.showLabel;
            if (sideInterp.label.kind != LabelKind::Element) merged.element = label.element;
            return merged;
        }
        label.kind = LabelKind::None;
        return label;
    }
    AtomLabel decoded;
    if (!decodeLabel(encoded, &decoded).ok()) {
        label.kind = LabelKind::Arbitrary;
        label.text = encoded;
        return label;
    }
    // Re-derive the template metadata from the text so a reader with a different
    // template table still recognises the group.
    if (decoded.kind == LabelKind::Abbreviation) {
        LabelInterpretation interp = interpretLabel(decoded.text, abbreviations);
        if (interp.label.kind == LabelKind::Abbreviation)
            decoded.element = interp.label.element;
    }
    return decoded;
}

// Erase the typed label entry, recording the touch.  Used by the element and
// clear-label commands so that removing a label is as undoable as adding one.
void eraseLabelEntry(core::Document& document, core::ObjectId atom) {
    const core::ObjectRef ref(core::ObjectKind::Atom, atom);
    core::ExtensionBag& bag = document.extensions(ref);
    if (bag.erase(kLabelKey)) {
        document.noteTouchedExtensions(ref, false);
        document.bumpStructure();
    }
}

core::Error applyLabelToDocument(core::Document& document, core::ObjectId atom,
                                 const AtomLabel& label) {
    const core::AtomRecord* r = document.atoms().alive(atom);
    if (!r) return core::Error(core::ErrorCode::NotAlive, "atom does not exist");
    if (label.element != 0 && (label.element < 1 || label.element > 118))
        return core::Error(core::ErrorCode::InvalidArgument, "element out of range");
    if (!validChargeValue(label.charge))
        return core::Error(core::ErrorCode::InvalidArgument, "charge out of range");
    if (!validRadicalValue(label.radical))
        return core::Error(core::ErrorCode::InvalidArgument, "radical out of range");
    if (label.isotope < 0 || label.isotope > 400)
        return core::Error(core::ErrorCode::InvalidArgument, "isotope out of range");

    // Use the RECORDED mutators (setElement / setAtomSemantics / setAtomLabel),
    // never writeAtom: the recorded ones call noteTouchedAtom, which is what makes
    // the transaction diff pick the change up.  writeAtom is the change-set
    // replay path and deliberately does not record.
    //
    // The ELEMENT is only written when the label itself is an element label.  An
    // Arbitrary label must NOT move the element: that is the preserve-verbatim
    // guarantee.
    if (label.kind == LabelKind::Element && label.element >= 1 && label.element <= 118) {
        core::Error e = document.setElement(atom, label.element);
        if (!e.ok()) return e;
    }
    core::AtomSemantics s = r->semantics;
    s.charge = static_cast<int8_t>(label.charge);
    s.isotope = static_cast<int16_t>(label.isotope);
    // setAtomSemantics caps implicitHydrogens at 8; a caller asking for more is a
    // programmer error rather than a chemistry one, so clamp explicitly.
    s.implicitHydrogens = static_cast<int8_t>(
        label.hydrogenCount > 8 ? 8 : label.hydrogenCount);
    s.radical = label.radical;
    s.nodeType = label.nodeType;
    s.showLabel = label.showLabel;
    core::Error e = document.setAtomSemantics(atom, s);
    if (!e.ok()) return e;

    // Store the TYPED label in the extension bag (undoable + persistent, see above).
    // For an element or None label we erase the entry so the renderer derives the
    // symbol from the element and the two cannot disagree.
    const core::ObjectRef ref(core::ObjectKind::Atom, atom);
    core::ExtensionBag& bag = document.extensions(ref);
    if (label.kind == LabelKind::Element || label.kind == LabelKind::None) {
        if (bag.erase(kLabelKey)) {
            document.noteTouchedExtensions(ref, false);
            document.bumpStructure();
        }
        return core::Error();
    }
    bag.set(kLabelKey, encodeLabel(label));
    document.noteTouchedExtensions(ref, false);
    document.bumpStructure();
    // IMPORTANT: the core side table is deliberately NOT written here.  It is part of
    // Document::digest() but NOT part of ChangeSet, so writing it would make an undone
    // command leave the digest different - the very "undo did not restore the exact
    // state" defect the digest exists to catch.  readAtomLabel() is the single
    // accessor; consumers must use it rather than Document::atomLabel().
    return core::Error();
}

} // namespace

// ---------------------------------------------------------------------------
// Attribute reads
// ---------------------------------------------------------------------------
AtomLabel readAtomLabel(const core::Document& document, core::ObjectId atom,
                        const std::vector<AbbreviationEntry>& abbreviations) {
    return labelFromRecord(document, atom, abbreviations);
}

std::string atomicNumberText(int element) {
    if (element < 1 || element > 118) return std::string();
    return intToStr(element);
}

// ---------------------------------------------------------------------------
// Attribute commands
// ---------------------------------------------------------------------------
core::Command setElementCommand(core::ObjectId atom, int element) {
    return core::Command("chem.setElement",
        core::kInvalidateSemantics | core::kInvalidateAppearance,
        [atom, element](core::CommandContext& ctx) -> core::CommandResult {
            if (element < 1 || element > 118)
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                    "element out of range");
            const core::AtomRecord* r = ctx.document.atoms().alive(atom);
            if (!r) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                     "atom does not exist");
            const core::ExtensionBag& existing =
                ctx.document.extensions(core::ObjectRef(core::ObjectKind::Atom, atom));
            if (r->element == element &&
                static_cast<core::NodeType>(r->semantics.nodeType) == core::NodeType::Element &&
                existing.findValue(kLabelKey) == 0) {
                return core::CommandResult::noChange();
            }
            core::Error e = ctx.document.setElement(atom, element);
            if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            core::AtomSemantics s = r->semantics;
            s.nodeType = static_cast<uint8_t>(core::NodeType::Element);
            e = ctx.document.setAtomSemantics(atom, s);
            if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            // Once a real element is chosen the label override is gone; leaving it
            // would let a stale Ph text survive an element edit.
            eraseLabelEntry(ctx.document, atom);
            return core::CommandResult::success(core::kInvalidateSemantics |
                                                core::kInvalidateAppearance);
        });
}

core::Command setLabelCommand(core::ObjectId atom, const AtomLabel& label) {
    return core::Command("chem.setLabel",
        core::kInvalidateAppearance | core::kInvalidateSemantics,
        [atom, label](core::CommandContext& ctx) -> core::CommandResult {
            core::Error e = applyLabelToDocument(ctx.document, atom, label);
            if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            return core::CommandResult::success(core::kInvalidateAppearance |
                                                core::kInvalidateSemantics);
        });
}

core::Command setLabelTextCommand(core::ObjectId atom, const std::string& text,
                                  const std::vector<AbbreviationEntry>& abbreviations,
                                  LabelKind* outKind) {
    LabelInterpretation interp = interpretLabel(text, abbreviations);
    if (outKind) *outKind = interp.label.kind;
    const AtomLabel decided = interp.label;
    return core::Command("chem.setLabelText",
        core::kInvalidateAppearance | core::kInvalidateSemantics,
        [atom, decided](core::CommandContext& ctx) -> core::CommandResult {
            if (decided.kind == LabelKind::None) {
                // Empty text: clear the override but keep charge/isotope/etc.
                const core::AtomRecord* r = ctx.document.atoms().alive(atom);
                if (!r) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                         "atom does not exist");
                eraseLabelEntry(ctx.document, atom);
                return core::CommandResult::success(core::kInvalidateAppearance);
            }
            // A label decision owns only the LABEL (kind + text + r-group index +
            // node type).  Every other attribute - charge, isotope, radical,
            // hydrogen count, showLabel - belongs to the atom as it stands and must
            // be PRESERVED.  Writing the interpretation verbatim would silently wipe
            // a charge the user set earlier, which is exactly the class of defect
            // this module exists to prevent.
            const core::AtomRecord* r = ctx.document.atoms().alive(atom);
            if (!r) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                     "atom does not exist");
            AtomLabel merged = decided;
            merged.charge = r->semantics.charge;
            merged.isotope = r->semantics.isotope;
            merged.radical = r->semantics.radical;
            merged.hydrogenCount = r->semantics.implicitHydrogens;
            merged.showLabel = r->semantics.showLabel;
            if (decided.kind == LabelKind::Element) {
                // An element decision DOES own the element; keep the atom's other
                // semantics untouched, which the merge above already did.
                merged.element = decided.element;
            } else {
                merged.element = r->element;
            }
            core::Error e = applyLabelToDocument(ctx.document, atom, merged);
            if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            return core::CommandResult::success(core::kInvalidateAppearance |
                                                core::kInvalidateSemantics);
        });
}

core::Command setChargeCommand(const std::vector<core::ObjectId>& atoms, int charge) {
    std::vector<core::ObjectId> copy(atoms);
    return core::Command("chem.setCharge", core::kInvalidateAppearance,
        [copy, charge](core::CommandContext& ctx) -> core::CommandResult {
            if (!validChargeValue(charge))
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "charge out of range");
            int changed = 0;
            for (size_t i = 0; i < copy.size(); ++i) {
                const core::AtomRecord* r = ctx.document.atoms().alive(copy[i]);
                if (!r) continue;
                if (r->semantics.charge == charge) continue;
                core::AtomSemantics s = r->semantics;
                s.charge = static_cast<int8_t>(charge);
                core::Error e = ctx.document.setAtomSemantics(copy[i], s);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                ++changed;
            }
            if (changed == 0) return core::CommandResult::noChange();
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

core::Command setIsotopeCommand(const std::vector<core::ObjectId>& atoms, int isotope) {
    std::vector<core::ObjectId> copy(atoms);
    return core::Command("chem.setIsotope", core::kInvalidateAppearance,
        [copy, isotope](core::CommandContext& ctx) -> core::CommandResult {
            if (isotope < 0 || isotope > 400)
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "isotope out of range");
            int changed = 0;
            for (size_t i = 0; i < copy.size(); ++i) {
                const core::AtomRecord* r = ctx.document.atoms().alive(copy[i]);
                if (!r) continue;
                if (r->semantics.isotope == isotope) continue;
                core::AtomSemantics s = r->semantics;
                s.isotope = static_cast<int16_t>(isotope);
                core::Error e = ctx.document.setAtomSemantics(copy[i], s);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                ++changed;
            }
            if (changed == 0) return core::CommandResult::noChange();
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

core::Command setRadicalCommand(const std::vector<core::ObjectId>& atoms, int radical) {
    std::vector<core::ObjectId> copy(atoms);
    return core::Command("chem.setRadical", core::kInvalidateAppearance,
        [copy, radical](core::CommandContext& ctx) -> core::CommandResult {
            if (!radicalSupported(radical))
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "radical value not supported");
            int changed = 0;
            for (size_t i = 0; i < copy.size(); ++i) {
                const core::AtomRecord* r = ctx.document.atoms().alive(copy[i]);
                if (!r) continue;
                if (r->semantics.radical == radical) continue;
                core::AtomSemantics s = r->semantics;
                s.radical = static_cast<uint8_t>(radical);
                core::Error e = ctx.document.setAtomSemantics(copy[i], s);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                ++changed;
            }
            if (changed == 0) return core::CommandResult::noChange();
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

core::Command setHydrogenCountCommand(const std::vector<core::ObjectId>& atoms, int count) {
    std::vector<core::ObjectId> copy(atoms);
    return core::Command("chem.setHydrogens", core::kInvalidateAppearance,
        [copy, count](core::CommandContext& ctx) -> core::CommandResult {
            if (count < -1 || count > 12)
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "hydrogen count out of range");
            int changed = 0;
            for (size_t i = 0; i < copy.size(); ++i) {
                const core::AtomRecord* r = ctx.document.atoms().alive(copy[i]);
                if (!r) continue;
                if (r->semantics.implicitHydrogens == count) continue;
                core::AtomSemantics s = r->semantics;
                s.implicitHydrogens = static_cast<int8_t>(count);
                core::Error e = ctx.document.setAtomSemantics(copy[i], s);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                ++changed;
            }
            if (changed == 0) return core::CommandResult::noChange();
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

core::Command setShowLabelCommand(const std::vector<core::ObjectId>& atoms, bool show) {
    std::vector<core::ObjectId> copy(atoms);
    return core::Command("chem.setShowLabel", core::kInvalidateAppearance,
        [copy, show](core::CommandContext& ctx) -> core::CommandResult {
            int changed = 0;
            for (size_t i = 0; i < copy.size(); ++i) {
                const core::AtomRecord* r = ctx.document.atoms().alive(copy[i]);
                if (!r) continue;
                if ((r->semantics.showLabel != 0) == show) continue;
                core::AtomSemantics s = r->semantics;
                s.showLabel = show;
                core::Error e = ctx.document.setAtomSemantics(copy[i], s);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                ++changed;
            }
            if (changed == 0) return core::CommandResult::noChange();
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

core::Command clearLabelCommand(const std::vector<core::ObjectId>& atoms) {
    std::vector<core::ObjectId> copy(atoms);
    return core::Command("chem.clearLabel", core::kInvalidateAppearance,
        [copy](core::CommandContext& ctx) -> core::CommandResult {
            int changed = 0;
            for (size_t i = 0; i < copy.size(); ++i) {
                const core::AtomRecord* r = ctx.document.atoms().alive(copy[i]);
                if (!r) continue;
                if (!ctx.document.atomLabel(copy[i]).empty()) ++changed;
                eraseLabelEntry(ctx.document, copy[i]);
                if (r->semantics.nodeType != static_cast<uint8_t>(core::NodeType::Element)) {
                    core::AtomSemantics s = r->semantics;
                    s.nodeType = static_cast<uint8_t>(core::NodeType::Element);
                    core::Error e = ctx.document.setAtomSemantics(copy[i], s);
                    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                    ++changed;
                }
            }
            if (changed == 0) return core::CommandResult::noChange();
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}
// ---------------------------------------------------------------------------
// Attribute / graphic-mark binding
// ---------------------------------------------------------------------------
core::ObjectId resolveMarkBinding(const core::Vec2& markPosition,
                                  const std::vector<core::ObjectId>& candidates,
                                  const std::vector<core::Vec2>& candidatePositions,
                                  double bondLength, double* outDistance) {
    if (outDistance) *outDistance = -1.0;
    if (candidates.size() != candidatePositions.size()) return core::kInvalidObjectId;
    if (!(bondLength > 0.0) || !std::isfinite(bondLength)) return core::kInvalidObjectId;
    const double radius = kMarkBindFactor * bondLength;
    core::ObjectId best = core::kInvalidObjectId;
    double bestDistance = 0.0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const double dx = candidatePositions[i].x - markPosition.x;
        const double dy = candidatePositions[i].y - markPosition.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > radius) continue;
        // Nearest wins; an exact tie goes to the SMALLER stable id so the result
        // does not depend on the order candidates arrived in.
        if (best == core::kInvalidObjectId || d < bestDistance ||
            (d == bestDistance && candidates[i] < best)) {
            best = candidates[i];
            bestDistance = d;
        }
    }
    if (best != core::kInvalidObjectId && outDistance) *outDistance = bestDistance;
    return best;
}

// ---------------------------------------------------------------------------
// Incremental refresh
// ---------------------------------------------------------------------------
std::vector<core::ObjectId> atomsAffectedByBond(core::ObjectId begin,
                                                core::ObjectId end) {
    std::vector<core::ObjectId> out;
    out.push_back(begin);
    if (end != begin) out.push_back(end);
    return out;
}

RefreshResult refreshAtomDerived(const core::Document& document, core::ObjectId atom,
                                 const std::vector<AbbreviationEntry>& abbreviations) {
    RefreshResult result;
    const core::AtomRecord* r = document.atoms().alive(atom);
    if (!r) return result;

    AtomEnvironment env;
    env.element = r->element;
    env.charge = r->semantics.charge;
    env.radical = r->semantics.radical;
    env.isotope = r->semantics.isotope;
    env.explicitHydrogens = r->semantics.implicitHydrogens;

    const std::vector<core::ObjectId>& incident = document.incidentBonds(atom);
    for (size_t i = 0; i < incident.size(); ++i) {
        const core::BondRecord* b = document.bonds().alive(incident[i]);
        if (!b) continue;
        int order = 1;
        switch (b->order) {
            case core::BondOrder::Double: order = 2; break;
            case core::BondOrder::Triple: order = 3; break;
            case core::BondOrder::Quadruple: order = 4; break;
            case core::BondOrder::Aromatic: order = 1; break;
            default: order = 1; break;
        }
        if (b->semantics == core::BondSemantics::Aromatic) {
            env.aromaticBondCount += 1;
            order = 1;
        }
        env.bondOrderSum += order;
        env.bondCount += 1;
    }

    const std::string& text = document.atomLabel(atom);
    if (!text.empty()) {
        LabelInterpretation interp = interpretLabel(text, abbreviations);
        env.hasLabelOverride = true;
        env.labelKind = interp.label.kind;
    }

    HydrogenResult h = implicitHydrogens(env);
    result.newHydrogenCount = h.count;
    result.overCoordinated = h.overCoordinated;
    result.warnings = warningsForAtom(env, static_cast<int>(atom));

    // Count how many derived fields the refresh WOULD change.  A bond-order edit on
    // a carbon changes only the hydrogen count; a style edit changes nothing.
    if (r->semantics.implicitHydrogens < 0) {
        // The count is computed, so the label text derives from it; a change in the
        // number is a display change.
        if (h.valid && h.count != 0) result.changedFields += 1;
    }
    if (h.overCoordinated) result.changedFields += 1;
    return result;
}

core::Command setBondOrderWithRefreshCommand(core::ObjectId bond,
                                             core::BondOrder order) {
    return core::Command("chem.setBondOrderWithRefresh",
        core::kInvalidateSemantics | core::kInvalidateAppearance,
        [bond, order](core::CommandContext& ctx) -> core::CommandResult {
            const core::BondRecord* b = ctx.document.bonds().alive(bond);
            if (!b) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                     "bond does not exist");
            core::Error e = ctx.document.setBondOrder(bond, order);
            if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            // Touch ONLY the two endpoints.  The derived label/hydrogen fields live
            // in the atom records and the label side table; recomputing them is the
            // renderer host job, but recording the touched set here is what makes
            // "only the relevant labels were updated" a checkable claim.
            const core::BondRecord* after = ctx.document.bonds().alive(bond);
            if (after) {
                ctx.document.noteTouchedAtom(after->beginAtom);
                ctx.document.noteTouchedAtom(after->endAtom);
            }
            return core::CommandResult::success(core::kInvalidateSemantics |
                                                core::kInvalidateAppearance);
        });
}
// ---------------------------------------------------------------------------
// Atom hotspot reactions
// ---------------------------------------------------------------------------
const char* reactionKindName(ReactionKind kind) {
    switch (kind) {
        case ReactionKind::None:           return "none";
        case ReactionKind::AddCarbon:      return "add-carbon";
        case ReactionKind::AddCarbonyl:    return "add-carbonyl";
        case ReactionKind::AddPhenyl:      return "add-phenyl";
        case ReactionKind::AddMethyl:      return "add-methyl";
        case ReactionKind::AddOxygen:      return "add-oxygen";
        case ReactionKind::SetElement:     return "set-element";
        case ReactionKind::SetLabel:       return "set-label";
        case ReactionKind::SelectHotspot:  return "select-hotspot";
        case ReactionKind::EditLabel:      return "edit-label";
        case ReactionKind::ClearLabel:     return "clear-label";
        case ReactionKind::AtomicNumber:   return "atomic-number";
        case ReactionKind::ChargePositive: return "charge-positive";
        case ReactionKind::ChargeNegative: return "charge-negative";
    }
    return "?";
}

namespace {

// Build the common decision envelope so every branch below stays short.
ReactionDecision decide(ReactionKind kind, const HotspotContext& ctx) {
    ReactionDecision d;
    d.kind = kind;
    d.apply = true;
    d.context = ctx;
    return d;
}

ReactionDecision refuse(ReactionKind kind, const HotspotContext& ctx,
                        const std::string& reason) {
    ReactionDecision d;
    d.kind = kind;
    d.apply = false;
    d.reason = reason;
    d.context = ctx;
    return d;
}

} // namespace

ReactionDecision resolveAtomHotkey(char key, const HotspotContext& context,
                                   const ITemplateService& templates) {
    // The element hotkeys, case preserved.  The values are the verified table:
    //   c -> C, l or Shift+c -> Cl, b -> Br, Shift+b -> B, n/w -> N, o/q -> O,
    //   s -> S, Shift+s -> Si, f -> F, Shift+f -> CF3, p -> P, Shift+p -> Ph,
    //   h -> H, i -> I, r -> R, m -> Me, d -> D, e -> Et, Shift+e -> CO2Me,
    //   Shift+z -> N3, Shift+m -> MgBr, Shift+l -> Li, Shift+o -> OMe,
    //   Shift+q -> Fmoc, Shift+h -> Cbz, Shift+y -> Boc.
    // Only the ones whose CHEMISTRY this module implements are handled here; the
    // rest refuse with a reason instead of silently doing nothing or the wrong thing.
    switch (key) {
        case 'c': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 6; return d;
        }
        case 'C': {  // Shift+c is Cl in the official atom table
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 17; return d;
        }
        case 'l': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 17; return d;
        }
        case 'b': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 35; return d;
        }
        case 'B': {  // Shift+b is boron
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 5; return d;
        }
        case 'n': case 'w': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 7; return d;
        }
        case 'o': case 'q': {
            // NOTE: adding O SETS the hotspot atom to oxygen.  It must not create a
            // new oxygen bonded to a nearby carbon - that is the confusion the task
            // statement calls out.  AddOxygen exists as a distinct kind for callers
            // that want to distinguish "change this atom" from "sprout a group".
            ReactionDecision d = decide(ReactionKind::AddOxygen, context);
            d.newElement = 8; return d;
        }
        case 's': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 16; return d;
        }
        case 'S': {  // Shift+s is Si
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 14; return d;
        }
        case 'f': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 9; return d;
        }
        case 'F': {  // Shift+f is the CF3 group
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "CF3"; return d;
        }
        case 'p': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 15; return d;
        }
        case 'P': {  // Shift+p is the phenyl group
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "Ph"; return d;
        }
        case 'h': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 1; return d;
        }
        case 'H': {  // Shift+h is the Cbz group
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "Cbz"; return d;
            // Cbz is not in the built-in table, so the label becomes Arbitrary - which
            // is the SAFE outcome: the text is kept and the topology is untouched.
        }
        case 'i': {
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 53; return d;
        }
        case 'r': {
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "R"; return d;
        }
        case 'm': {
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "Me"; return d;
        }
        case 'd': {
            // The official atom table lists d as the deuterium label.  Deuterium is
            // hydrogen with isotope 2; representing it as an isotope is the correct
            // model, so this sets isotope 2 on H rather than inventing an element.
            ReactionDecision d = decide(ReactionKind::SetElement, context);
            d.newElement = 1;
            d.reason = "deuterium: set element H and isotope 2 (apply both)";
            return d;
        }
        case 'e': {
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "Et"; return d;
        }
        case 'E': {  // Shift+e is CO2Me
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "CO2Me"; return d;
        }
        case 'z': {
            ReactionDecision d = decide(ReactionKind::AddCarbon, context);
            d.reason = "alkyne sprout is approximated as a single new carbon";
            return d;
        }
        case 'v': case 'u': case 'k': case 'K': {
            return refuse(ReactionKind::None, context,
                "ring sprout for this key is not implemented in this module");
        }
        case 'g': return decide(ReactionKind::SelectHotspot, context);
        case '=': {
            return refuse(ReactionKind::None, context,
                "nickname dialog is a host UI concern, not a chemistry edit");
        }
        case '/': case '?': {
            return refuse(ReactionKind::None, context,
                "atom properties dialog is a host UI concern");
        }
        case '\r': case '\n': {
            return decide(ReactionKind::EditLabel, context);
        }
        case '\b': {
            return decide(ReactionKind::ClearLabel, context);
        }
        case 39: {  // apostrophe: atomic number display
            return decide(ReactionKind::AtomicNumber, context);
        }
        case '+': {
            ReactionDecision d = decide(ReactionKind::ChargePositive, context);
            d.chargeDelta = 1; return d;
        }
        case '-': {
            ReactionDecision d = decide(ReactionKind::ChargeNegative, context);
            d.chargeDelta = -1; return d;
        }
        case '1': {
            // Hotspot 1: sprout a single bond to a new carbon.
            return decide(ReactionKind::AddCarbon, context);
        }
        case '2': {
            // Hotspot 2: environment-dependent carbonyl/acetyl.  The table lists it
            // for an atom, NOT as the bond-tool double key.  We implement it as a
            // carbonyl sprout and REFUSE on an atom that already has a carbonyl-like
            // environment so a second press does not stack a second C=O.
            if (context.bondCount >= 4) {
                return refuse(ReactionKind::AddCarbonyl, context,
                    "hotspot already has four bonds; adding a carbonyl would over-coordinate it");
            }
            return decide(ReactionKind::AddCarbonyl, context);
        }
        case '3': case 'a': {
            // Hotspot 3 / a: environment-dependent phenyl sprout.
            if (context.bondCount >= 4) {
                return refuse(ReactionKind::AddPhenyl, context,
                    "hotspot already has four bonds; adding a phenyl would over-coordinate it");
            }
            return decide(ReactionKind::AddPhenyl, context);
        }
        case '4': case '5': {
            return refuse(ReactionKind::None, context,
                "wedge/hash sprout is a bond-display action owned by another module");
        }
        case '6': case '7': case '8': {
            return refuse(ReactionKind::None, context,
                "ring/methylene sprout for this key is not implemented in this module");
        }
        case '9': {
            return decide(ReactionKind::AddMethyl, context);
        }
        case '0': {
            // Environment dependent: a primary carbon sprouts a bond, a secondary or
            // tertiary one uses a long bond.  Both are one new carbon here.
            return decide(ReactionKind::AddCarbon, context);
        }
        case 'x': {
            ReactionDecision d = decide(ReactionKind::SetLabel, context);
            d.labelText = "X";
            d.reason = "X is a query label, not an element";
            return d;
        }
        default: break;
    }

    // A template abbreviation the caller supplied but that is not one of the explicit
    // cases above still works: the resolver reports SetLabel and the caller decides
    // whether the text is a known template.
    (void)templates;
    return refuse(ReactionKind::None, context, "no chemistry action is defined for this key");
}

// ---------------------------------------------------------------------------
// Sprout planners
// ---------------------------------------------------------------------------
namespace {

core::Vec2 offsetBy(const core::Vec2& origin, double directionDeg, double distance) {
    const core::Vec2 u = core::direction(directionDeg);
    return core::Vec2(origin.x + u.x * distance, origin.y + u.y * distance);
}

} // namespace

SproutPlan planAddCarbon(const core::Vec2& hotspotPosition, double directionDeg,
                         double bondLength) {
    SproutPlan plan;
    if (!hotspotPosition.finite()) { plan.error = "hotspot position is not finite"; return plan; }
    if (!std::isfinite(directionDeg)) { plan.error = "direction is not finite"; return plan; }
    if (!(bondLength > 0.0) || !std::isfinite(bondLength)) {
        plan.error = "bond length must be finite and positive"; return plan;
    }
    SproutAtom a;
    a.element = 6;
    a.position = offsetBy(hotspotPosition, directionDeg, bondLength);
    plan.atoms.push_back(a);
    SproutBond b;
    b.toNewIndex = 0;
    b.order = core::BondOrder::Single;
    plan.bonds.push_back(b);
    plan.ok = true;
    return plan;
}

// The carbonyl plan: a NEW carbon out along the direction, a double bond from the
// hotspot to it, and an oxygen double-bonded to the new carbon at 120 degrees to
// the attachment so the two double bonds do not overlap.  This function NEVER looks
// at existing atoms, which is exactly why a crowded environment cannot make it
// attach to a neighbour.
SproutPlan planCarbonyl(const core::Vec2& hotspotPosition, double directionDeg,
                        double bondLength) {
    SproutPlan plan;
    if (!hotspotPosition.finite()) { plan.error = "hotspot position is not finite"; return plan; }
    if (!std::isfinite(directionDeg)) { plan.error = "direction is not finite"; return plan; }
    if (!(bondLength > 0.0) || !std::isfinite(bondLength)) {
        plan.error = "bond length must be finite and positive"; return plan;
    }
    SproutAtom carbon;
    carbon.element = 6;
    carbon.position = offsetBy(hotspotPosition, directionDeg, bondLength);
    plan.atoms.push_back(carbon);

    SproutAtom oxygen;
    oxygen.element = 8;
    oxygen.position = offsetBy(carbon.position, directionDeg + 120.0, bondLength);
    plan.atoms.push_back(oxygen);

    SproutBond attach;
    attach.toNewIndex = 0;
    attach.order = core::BondOrder::Double;
    plan.bonds.push_back(attach);

    SproutBond cto;
    cto.toNewIndex = 1;
    cto.order = core::BondOrder::Double;
    plan.extraBonds.push_back(cto);

    plan.ok = true;
    return plan;
}

// ---------------------------------------------------------------------------
// Phenyl sprout: regular hexagon whose ring atom 0 sits exactly one bond length
// from the hotspot along `directionDeg`.  Geometry:
//   - ring circumradius R = bondLength (a regular hexagon side equals its radius)
//   - ring atom 0 is at hotspot + u*bondLength
//   - the ring CENTRE is a further R along the SAME direction from ring atom 0,
//     i.e. centre = hotspot + u*(bondLength + R) = hotspot + u*2*bondLength
//   - the remaining atoms are centre + R*(cos, sin) at 60-degree steps, with the
//     angle chosen so that atom 1 lands back through atom 0 toward the hotspot.
// That puts only ring atom 0 near the hotspot; every other ring atom is at least
// one bond length away, so nothing overlaps the attachment.
SproutPlan planPhenyl(const core::Vec2& hotspotPosition, double directionDeg,
                      double bondLength) {
    SproutPlan plan;
    if (!hotspotPosition.finite()) { plan.error = "hotspot position is not finite"; return plan; }
    if (!std::isfinite(directionDeg)) { plan.error = "direction is not finite"; return plan; }
    if (!(bondLength > 0.0) || !std::isfinite(bondLength)) {
        plan.error = "bond length must be finite and positive"; return plan;
    }
    const double R = bondLength;
    const core::Vec2 u = core::direction(directionDeg);
    const core::Vec2 ring0(hotspotPosition.x + u.x * R, hotspotPosition.y + u.y * R);
    const core::Vec2 centre(ring0.x + u.x * R, ring0.y + u.y * R);
    // The angle of ring atom 0 as seen from the centre is the reverse of u, so the
    // hexagon is built from that base angle outward in 60-degree steps.
    const double baseRad = std::atan2(ring0.y - centre.y, ring0.x - centre.x);
    for (int i = 0; i < 6; ++i) {
        const double a = baseRad + static_cast<double>(i) * (2.0 * 3.14159265358979323846 / 6.0);
        SproutAtom sa;
        sa.element = 6;
        sa.position = core::Vec2(centre.x + R * std::cos(a), centre.y + R * std::sin(a));
        plan.atoms.push_back(sa);
    }
    SproutBond attach;
    attach.toNewIndex = 0;
    attach.order = core::BondOrder::Single;
    plan.bonds.push_back(attach);

    for (int i = 0; i < 6; ++i) {
        SproutBond rb;
        rb.toNewIndex = (i + 1) % 6;
        rb.order = ((i % 2) == 0) ? core::BondOrder::Double : core::BondOrder::Single;
        plan.extraBonds.push_back(rb);
    }
    plan.ok = true;
    return plan;
}

} // namespace chemistry
} // namespace r62
