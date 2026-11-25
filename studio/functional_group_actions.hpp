#pragma once

#include "explicit_hydrogen_expansion.hpp"
#include "functional_groups.hpp"
#include "engine/catalytic.hpp"
#include "catalytic_metal.hpp"
#include "expansion_cleanup.hpp"

namespace studio_group_actions {
using namespace r62;
using namespace core;
using integration::Session;

inline bool foldCatalytic(Session& session, bool expand, Error& result) {
    const auto& doc = static_cast<const Document&>(session.doc);
    std::vector<ObjectId> ids;
    for (auto ref : studioio::expanded(doc, session.selected)) {
        if (ref.kind != ObjectKind::Atom || !doc.atoms().alive(ref.id)) continue;
        const auto& bag = doc.extensions(ref);
        const bool compact = bag.findValue("studio.compound.fragment") != nullptr;
        if (bag.findValue(CatalyticMetal::attr("Metal")) && compact == expand &&
            (compact || !doc.incidentBonds(ref.id).empty())) ids.push_back(ref.id);
    }
    if (ids.empty()) return false;
    result = session.submit(Command(expand ? "展开金属配合物" : "收起金属配合物",
        kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
        [ids, expand](CommandContext& context) {
            try {
                for (auto id : ids) CatalyticMetal::fold(context.document, id, expand);
                const auto error = studio::catalytic::update(context.document);
                return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
            } catch (const std::exception& error) {
                return CommandResult::fail(ErrorCode::InvalidArgument, error.what());
            }
        }), "配合物显示");
    if (result.ok()) {
        session.selected.clear();
        for (auto id : ids) session.selected.emplace_back(ObjectKind::Atom, id);
        session.status = expand ? "已展开真实配体结构" : "已收起配合物；原结构和颜色保留，可右键展开";
    }
    return true;
}

inline Error collapse(Session& session) {
    try {
        auto chosen = functional_groups::selection(session);
        if (chosen.empty()) return {ErrorCode::InvalidArgument, "请先选中要收起的结构"};
        const auto matches = functional_groups::findMatches(session.doc, chosen);
        if (matches.empty()) return {ErrorCode::InvalidArgument, "选区内没有与库定义及连接关系匹配的官能团"};
        auto result = session.submit(Command("官能团收起", kInvalidateStructure | kInvalidateGeometry | kInvalidateSemantics,
            [matches](CommandContext& context) {
                try {
                    for (const auto& match : matches) functional_groups::collapse(context.document, match);
                    return CommandResult::success();
                } catch (const std::exception& error) {
                    return CommandResult::fail(ErrorCode::InvalidArgument, error.what());
                }
            }), "官能团收起");
        if (result.ok()) {
            session.selected.clear();
            for (const auto& match : matches) session.selected.emplace_back(ObjectKind::Atom, match.anchor);
            session.status = "已收起 " + std::to_string(matches.size()) + " 个官能团 · 可撤销";
        }
        return result;
    } catch (const std::exception& error) {
        return {ErrorCode::InvalidArgument, error.what()};
    }
}

inline Command methylExpansion(const std::vector<ObjectId>& methyls,
                                std::shared_ptr<std::vector<ObjectRef>> inserted) {
    return Command("甲基分级展开", kInvalidateStructure | kInvalidateGeometry | kInvalidateSemantics | kInvalidateAppearance,
        [methyls, inserted](CommandContext& context) {
            auto& doc = context.document;
            auto fail = [](const Error& error) { return CommandResult::fail(error.code, error.message); };
            inserted->clear();
            for (auto id : methyls) {
                auto* atom = doc.atoms().alive(id);
                auto links = doc.incidentBonds(id);
                if (!atom || links.size() != 1)
                    return CommandResult::fail(ErrorCode::RevisionConflict, "结构已改变，请重新展开");
                auto sem = atom->semantics;
                const auto origin = atom->position;
                const auto label = doc.atomLabel(id);
                const bool explicitH = sem.showLabel && sem.nodeType == uint8_t(NodeType::Element) &&
                    (label == "C" || label == "CH3" || label == "H3C");
                sem.nodeType = uint8_t(NodeType::Element);
                sem.showLabel = true;
                sem.implicitHydrogens = explicitH ? 0 : 3;
                auto error = doc.setAtomSemantics(id, sem);
                if (error.ok()) error = doc.setAtomLabel(id, "C");
                if (!error.ok()) return fail(error);
                studioatom::set(doc, id, {});
                auto& bag = doc.extensions({ObjectKind::Atom, id});
                bag.erase("studio.nickname.fragment");
                bag.erase("studio.compound.fragment");
                bag.set("studio.hydrogens.manual", "1");
                if (!explicitH) continue;
                const auto* link = doc.bonds().alive(links.front());
                const auto* other = doc.atoms().alive(link->beginAtom == id ? link->endAtom : link->beginAtom);
                const double angle = core::angleOf(other->position - origin);
                for (double turn : {90., 180., 270.}) {
                    ObjectId hydrogen, bond;
                    error = doc.addAtom(origin + core::direction(angle + turn) * doc.defaultBondLength(), 1, &hydrogen);
                    if (!error.ok()) return fail(error);
                    auto hs = doc.atoms().alive(hydrogen)->semantics;
                    hs.implicitHydrogens = 0;
                    hs.showLabel = true;
                    error = doc.setAtomSemantics(hydrogen, hs);
                    if (error.ok()) error = doc.addBond(id, hydrogen, &bond);
                    if (!error.ok()) return fail(error);
                    inserted->emplace_back(ObjectKind::Atom, hydrogen);
                    inserted->emplace_back(ObjectKind::Bond, bond);
                }
            }
            return CommandResult::success();
        });
}

enum class NextExpansion { None, CleanSelection, AllowRepeatCleanup };

inline Error expand(Session& session, bool repeated, const studio_local_cleanup::Service& service,
                    NextExpansion& next) {
    next = NextExpansion::None;
    if (session.selected.empty()) return {ErrorCode::InvalidArgument, "请先选中要展开的结构"};
    try {
        const auto chosen = functional_groups::selection(session);
        std::vector<ObjectId> methyls;
        std::vector<studio::explicit_hydrogens::Plan> hydrogens;
        bool otherGroups = false, otherHydrides = false;
        for (auto id : chosen) {
            const auto* atom = session.doc.atoms().alive(id);
            const auto label = session.doc.atomLabel(id);
            const auto links = session.doc.incidentBonds(id);
            const bool named = label == "Me" || label == "CH3" || label == "H3C";
            const auto& sem = atom->semantics;
            const bool methyl = atom->element == 6 && !sem.charge && !sem.radical && !sem.isotope &&
                links.size() == 1 && session.doc.bonds().alive(links.front())->order == BondOrder::Single &&
                (named || ((label.empty() || label == "C") && (sem.implicitHydrogens < 0 || sem.implicitHydrogens == 3)));
            studio::explicit_hydrogens::Plan plan;
            const bool hydride = studio::explicit_hydrogens::plan(session.doc, id, &plan);
            if (hydride) hydrogens.push_back(std::move(plan));
            if (methyl) methyls.push_back(id);
            else if (hydride) otherHydrides = true;
            else if (!functional_groups::definition(session.doc, id).empty()) otherGroups = true;
        }
        if (!otherGroups && ((!repeated && otherHydrides) || !methyls.empty())) {
            auto selected = session.selected;
            auto inserted = std::make_shared<std::vector<ObjectRef>>();
            const bool explicitH = !repeated && otherHydrides;
            auto result = session.submit(explicitH ? studio::explicit_hydrogens::command(hydrogens, inserted)
                : methylExpansion(methyls, inserted), explicitH ? "显式氢展开" : "甲基分级展开");
            if (result.ok()) {
                selected.insert(selected.end(), inserted->begin(), inserted->end());
                session.selected = std::move(selected);
                session.status = explicitH ? "已将隐式氢展开为 " + std::to_string(inserted->size() / 2) +
                    " 个独立 H 原子 · 可撤销" : "甲基已分级展开：线端 → CH3 → 显式 C—H";
            }
            return result;
        }
        if (repeated && !otherGroups) {
            next = NextExpansion::CleanSelection;
            return Error();
        }

        const auto retained = studioio::expanded(session.doc, session.selected);
        std::vector<functional_groups::Expansion> plans;
        for (auto id : chosen)
            if (!functional_groups::definition(session.doc, id).empty())
                plans.push_back(functional_groups::prepareExpansion(session.doc, id));
        for (auto ref : retained) {
            const auto* text = ref.kind == ObjectKind::Text ? session.doc.texts().alive(ref.id) : nullptr;
            if (text)
                if (const auto* compound = compounddata::find(session.doc.plainText(ref.id)))
                    plans.push_back(functional_groups::prepareCompound(session.doc, compound->fragment,
                        text->position + Vec2(text->size * .35, -text->size * .35), kInvalidObjectId, ref.id));
        }
        if (plans.empty()) return {ErrorCode::InvalidArgument, "选中结构没有可展开的官能团缩写"};

        const auto original = session.doc.digest();
        const auto document = session.doc.id();
        const auto serial = session.serial;
        Document prepared = session.doc;
        std::vector<ObjectRef> preview;
        std::set<ObjectId> metals;
        for (const auto& plan : plans) {
            const auto start = preview.size();
            functional_groups::applyExpansion(prepared, plan, preview);
            if (plan.metalLigands && preview.size() > start) metals.insert(preview[start].id);
        }
        auto result = studio_expansion_cleanup::run(prepared, preview, metals, service);
        if (!result.ok()) return result;
        if (session.doc.id() != document || session.serial != serial)
            return {ErrorCode::RevisionConflict, "结构已改变，请重新展开"};
        std::vector<Vec2> positions;
        for (auto ref : preview) if (ref.kind == ObjectKind::Atom)
            positions.push_back(prepared.atoms().alive(ref.id)->position);
        auto inserted = std::make_shared<std::vector<ObjectRef>>();
        result = session.submit(Command("官能团展开", kInvalidateStructure | kInvalidateGeometry | kInvalidateSemantics,
            [plans, positions, inserted, original](CommandContext& context) {
                auto& doc = context.document;
                if (doc.digest() != original)
                    return CommandResult::fail(ErrorCode::RevisionConflict, "结构已改变，请重新展开");
                try {
                    inserted->clear();
                    for (const auto& plan : plans) functional_groups::applyExpansion(doc, plan, *inserted);
                    size_t index = 0;
                    for (auto ref : *inserted) if (ref.kind == ObjectKind::Atom) {
                        if (index >= positions.size())
                            return CommandResult::fail(ErrorCode::InvalidGeometry, "展开原子映射不完整");
                        auto error = doc.setAtomPosition(ref.id, positions[index++]);
                        if (!error.ok()) return CommandResult::fail(error.code, error.message);
                    }
                    if (index != positions.size())
                        return CommandResult::fail(ErrorCode::InvalidGeometry, "展开原子映射不完整");
                    return CommandResult::success();
                } catch (const std::exception& error) {
                    return CommandResult::fail(ErrorCode::InvalidArgument, error.what());
                }
            }), "官能团展开");
        if (result.ok()) {
            session.selected = *inserted;
            for (auto ref : retained)
                if (((ref.kind == ObjectKind::Atom && session.doc.atoms().alive(ref.id)) ||
                     (ref.kind == ObjectKind::Bond && session.doc.bonds().alive(ref.id))) &&
                    std::find(session.selected.begin(), session.selected.end(), ref) == session.selected.end())
                    session.selected.push_back(ref);
            if (std::none_of(plans.begin(), plans.end(), [](const functional_groups::Expansion& plan) { return plan.intermediate; }))
                next = NextExpansion::AllowRepeatCleanup;
            session.status = "所选官能团已分级展开并自动整理 · 可撤销";
        }
        return result;
    } catch (const std::exception& error) {
        return {ErrorCode::InvalidArgument, error.what()};
    }
}
}
