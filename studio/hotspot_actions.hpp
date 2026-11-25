#pragma once

#include "engine/sprout_layout.hpp"
#include "next/chemistry/edit.hpp"
#include "next/integration/session.hpp"
#include "nicknames.hpp"

namespace studio::hotspot_actions {
using namespace r62;
using namespace core;

// Use the two trigonal continuations at a terminal carbon. Score the whole
// segment so a neighbouring atom cannot end up under the new oxygen.
inline double carbonylAngle(const Document& doc, ObjectId carbon, double growthAngle) {
    const sprout_layout::Environment obstacles(doc, carbon);
    const auto env = rings_chains::buildSproutEnvironment(doc, carbon, 25.);
    double best = std::numeric_limits<double>::infinity();
    double angle = std::numeric_limits<double>::quiet_NaN();
    for (double candidate : {growthAngle - 60., growthAngle + 60.}) {
        double score = 0.;
        if (obstacles.score(candidate, env.neighbourDirectionsDeg, &score) && score < best - 1e-8) {
            best = score;
            angle = candidate;
        }
    }
    return angle;
}

// Recipe numbers are the SPROUT values in keymap_data.hpp.
inline Command sprout(ObjectId id, int recipe) {
    using Preset = rings_chains::RingPresetId;
    return Command("添加结构片段", kInvalidateStructure | kInvalidateGeometry, [id, recipe](CommandContext& ctx) {
        auto& doc = ctx.document;
        if (!doc.atoms().alive(id)) return CommandResult::fail(ErrorCode::NotFound, "原子不存在");
        const auto dir = sprout_layout::resolve(doc, id, -30.);
        if (!dir.ok) return CommandResult::fail(ErrorCode::InvalidArgument, dir.rejectReason);
        const double angle = dir.directionDeg;
        const double unit = doc.defaultBondLength();
        const auto env = rings_chains::buildSproutEnvironment(doc, id, 25.);
        Error error;
        const auto add = [&](ObjectId from, int element, double rotation, BondOrder order = BondOrder::Single,
                             BondDisplay display = BondDisplay::Normal) {
            ObjectId to = kInvalidObjectId, bond;
            error = doc.addAtom(doc.atoms().get(from)->position + direction(rotation) * unit, element, &to);
            if (error.ok()) error = doc.addBond(from, to, &bond);
            if (error.ok()) error = doc.setBondOrder(bond, order);
            if (error.ok()) error = doc.setBondDisplay(bond, display);
            return to;
        };
        const auto ring = [&](ObjectId anchor, Preset preset) {
            rings_chains::RingRequest request;
            request.preset = preset;
            request.mode = dir.neighbourCount >= 2 && anchor == id
                ? rings_chains::RingAttachMode::Spiro : rings_chains::RingAttachMode::GrowFromAtom;
            request.anchorAtom = anchor;
            request.pointer = doc.atoms().get(anchor)->position + direction(angle) * unit;
            request.bondLength = unit;
            rings_chains::RingPlacement placement;
            error = rings_chains::resolveRingPlacement(doc, request, &placement);
            if (error.ok()) error = rings_chains::commands::createRingCommand(placement, 0, 0).execute(ctx).error;
        };
        switch (recipe) {
        case 0: case 1: add(id, 6, angle); break;
        case 2: {
            const double oxygenAngle = dir.neighbourCount == 1
                ? carbonylAngle(doc, id, env.neighbourDirectionsDeg[0] + 180.) : angle;
            if (!std::isfinite(oxygenAngle))
                return CommandResult::fail(ErrorCode::InvalidArgument, "附近没有可安全添加羰基的方向，请拖动指定方向");
            add(id, 8, oxygenAngle, BondOrder::Double);
            break;
        }
        case 3: case 15: case 16: {
            const ObjectId anchor = recipe != 3 || dir.neighbourCount > 1 ? add(id, 6, angle) : id;
            if (error.ok()) ring(anchor, recipe == 15 ? Preset::Cyclopentadiene : Preset::Benzene);
            break;
        }
        case 4: add(id, 6, angle, BondOrder::Single, BondDisplay::Wedge); break;
        case 5: add(id, 6, angle, BondOrder::Single, BondDisplay::WedgeHash); break;
        case 6: ring(id, Preset::Cyclohexane); break;
        case 7: ring(id, Preset::Cyclopentane); break;
        case 8: add(id, 6, angle, BondOrder::Double); break;
        case 9:
            add(id, 6, angle - 30.);
            if (error.ok()) add(id, 6, angle + 90.);
            break;
        case 10: {
            // A terminal alkyne grows in line with its existing bond.
            const double linear = dir.neighbourCount == 1 ? env.neighbourDirectionsDeg[0] + 180. : angle;
            add(id, 6, linear, BondOrder::Triple);
            break;
        }
        case 11: ring(id, Preset::Cyclopropane); break;
        case 12: ring(id, Preset::Cyclobutane); break;
        case 13: {
            const auto carbon = add(id, 6, angle);
            for (int i = 0; i < 3 && error.ok(); ++i) add(carbon, 6, angle - 90. + i * 90.);
            break;
        }
        case 14: {
            const auto sulfur = add(id, 16, angle);
            if (error.ok()) add(sulfur, 8, angle + 90., BondOrder::Double);
            if (error.ok()) add(sulfur, 8, angle - 90., BondOrder::Double);
            break;
        }
        default: return CommandResult::fail(ErrorCode::InvalidArgument, "无法识别结构片段");
        }
        return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
    });
}

inline Error fuse(integration::Session& session, ObjectId bond, Vec2 point, int recipe) {
    using Preset = rings_chains::RingPresetId;
    static constexpr Preset presets[] = {Preset::Cyclopentadiene, Preset::Benzene, Preset::Cyclopropane,
        Preset::Cyclobutane, Preset::Cyclopentane, Preset::Cyclohexane, Preset::Cycloheptane,
        Preset::Cyclooctane, Preset::Chair1, Preset::Chair2};
    if (recipe < 1 || recipe > 10) return {ErrorCode::InvalidArgument, "无法识别环模板"};
    rings_chains::RingRequest request;
    request.mode = rings_chains::RingAttachMode::FuseOnBond;
    request.anchorBond = bond;
    request.pointer = point;
    request.preset = presets[recipe - 1];
    rings_chains::RingPlacement placement;
    const auto error = rings_chains::resolveRingPlacement(session.doc, request, &placement);
    if (!error.ok()) return error;
    return session.submit(rings_chains::commands::createRingCommand(placement, 0, 0), "稠合环");
}

inline Command label(ObjectId id, const std::string& value, const AtomRecord& atom) {
    const auto base = nicknames::labelCommand(id, value);
    const auto* element = chemistry::elementInfoBySymbol(value.c_str(), int(value.size()));
    if (!element || !element->number) return base;
    const bool replace = atom.element != element->number || atom.semantics.nodeType != uint8_t(NodeType::Element);
    return Command("原子快捷键", kInvalidateStructure | kInvalidateAppearance | kInvalidateSemantics,
        [id, base, replace](CommandContext& ctx) mutable {
            const auto result = base.execute(ctx);
            if (!result.ok()) return result;
            auto sem = ctx.document.atoms().alive(id)->semantics;
            if (replace) sem.implicitHydrogens = -1;
            auto error = ctx.document.setAtomSemantics(id, sem);
            if (error.ok() && !ctx.document.atomLabel(id).empty()) error = ctx.document.setAtomLabel(id, "");
            auto& bag = ctx.document.extensions(ObjectRef(ObjectKind::Atom, id));
            bag.erase("studio.atom.runs");
            bag.erase("studio.label.text");
            return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
        });
}

inline Command clearLabel(ObjectId id) {
    return Command("清除原子标签", kInvalidateStructure | kInvalidateAppearance | kInvalidateSemantics,
        [id](CommandContext& ctx) {
            auto& doc = ctx.document;
            if (!doc.atoms().alive(id)) return CommandResult::fail(ErrorCode::NotFound, "原子不存在");
            auto& bag = doc.extensions(ObjectRef(ObjectKind::Atom, id));
            bool changed = !doc.atomLabel(id).empty();
            for (const auto* key : {"studio.atom.runs", "studio.label.text", "pst.attr.MoleculeStudioLiteralLabel",
                    "studio.nickname.fragment", "studio.compound.fragment"})
                changed |= bag.erase(key);
            const auto cleared = chemistry::clearLabelCommand({id}).execute(ctx);
            if (!cleared.ok()) return cleared;
            const auto error = doc.setAtomLabel(id, "");
            if (!error.ok()) return CommandResult::fail(error.code, error.message);
            return changed ? CommandResult::success() : cleared;
        });
}

inline Command markTerminalR(ObjectId id) {
    const auto base = nicknames::labelCommand(id, "R");
    return Command("末端 R 基团", kInvalidateStructure | kInvalidateSemantics | kInvalidateAppearance,
        [id, base](CommandContext& ctx) {
            const auto result = base.execute(ctx);
            if (!result.ok()) return result;
            auto sem = ctx.document.atoms().alive(id)->semantics;
            sem.showLabel = true;
            auto error = ctx.document.setAtomSemantics(id, sem);
            if (error.ok()) error = ctx.document.setAtomLabel(id, "R");
            return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
        });
}

inline Command bondDisplay(ObjectId id, const std::string& value) {
    return Command("键显示", kInvalidateAppearance | kInvalidateStructure, [id, value](CommandContext& ctx) {
        const bool dashed = value == "DASHED" || value == "TAUTOMERIC";
        const bool bold = value == "BOLD" || value == "DOUBLEBOLD";
        const auto display = dashed ? BondDisplay::Dashed : bold ? BondDisplay::Bold :
            value == "HASHED" ? BondDisplay::Hash : value == "HASHEDWEDGED" ? BondDisplay::WedgeHash :
            value == "WEDGED" ? BondDisplay::Wedge : BondDisplay::Wavy;
        auto error = ctx.document.setBondDisplay(id, display);
        if (error.ok() && (value == "TAUTOMERIC" || value == "DOUBLEBOLD"))
            error = ctx.document.setBondOrder(id, BondOrder::Double);
        return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
    });
}
}
