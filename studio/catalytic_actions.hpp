#pragma once

#include "functional_group_actions.hpp"
#include "next/graphics/model.hpp"

namespace studio::catalytic {
using integration::Session;

struct CycleSelection {
    std::vector<ObjectId> nodes;
    std::vector<ObjectId> arrows;
};

inline CycleSelection selected(const Session& session) {
    CycleSelection result;
    std::set<ObjectId> seen;
    for (auto ref : studioio::expanded(session.doc, session.selected)) {
        if (ref.kind == ObjectKind::Arrow) result.arrows.push_back(ref.id);
        if (ref.kind != ObjectKind::Atom || seen.count(ref.id)) continue;
        auto structure = component(session.doc, ref.id);
        auto metal = std::find_if(structure.begin(), structure.end(), [&](ObjectRef atom) {
            return atom.kind == ObjectKind::Atom && session.doc.extensions(atom).findValue(CatalyticMetal::attr("Metal"));
        });
        result.nodes.push_back(metal == structure.end() ? ref.id : metal->id);
        for (auto member : structure)
            if (member.kind == ObjectKind::Atom) seen.insert(member.id);
    }
    return result;
}

inline bool ellipse(const Document& document, ObjectRef ref, graphics::ShapeParams& params) {
    bool present = false;
    return ref.kind == ObjectKind::Arrow && graphics::readShape(document, ref.id, &params, &present).ok() &&
        present && (params.form == graphics::ShapeForm::Circle || params.form == graphics::ShapeForm::Oval);
}

inline std::vector<ObjectId> order(const Document& doc, std::vector<ObjectId> nodes, Vec2 origin) {
    const auto angle = [&](ObjectId id) {
        const auto point = center(doc, id) - origin;
        const double value = std::atan2(point.y, point.x) + 1.5707963267948966;
        return value < 0 ? value + 6.283185307179586 : value;
    };
    std::stable_sort(nodes.begin(), nodes.end(), [&](auto a, auto b) { return angle(a) < angle(b); });
    if (nodes.empty()) return nodes;
    std::map<ObjectId, ObjectId> roots, next;
    for (auto id : nodes)
        for (auto member : component(doc, id))
            if (member.kind == ObjectKind::Atom) roots.emplace(member.id, id);
    for (const auto& arrow : doc.arrows().records()) {
        if (!arrow.isAlive()) continue;
        const auto* mode = doc.extensions(ObjectRef(ObjectKind::Arrow, arrow.id)).findValue(key());
        if (!mode || *mode != "cycle") continue;
        arrows::ArrowParams params;
        bool present = false;
        if (!arrows::readArrow(doc, arrow.id, &params, &present).ok() || !present) continue;
        if (params.electronSource.kind != ObjectKind::Atom || params.electronTarget.kind != ObjectKind::Atom) continue;
        const auto source = roots.find(params.electronSource.id), target = roots.find(params.electronTarget.id);
        if (source != roots.end() && target != roots.end()) next[source->second] = target->second;
    }
    if (next.size() == nodes.size()) {
        std::vector<ObjectId> chain;
        auto id = nodes.front();
        while (next.count(id) && std::find(chain.begin(), chain.end(), id) == chain.end()) {
            chain.push_back(id);
            id = next.at(id);
        }
        if (chain.size() == nodes.size() && id == chain.front()) return chain;
    }
    return nodes;
}

struct CycleLayout {
    Vec2 origin;
    double horizontal = 130, vertical = 110, rotation = 0;
    bool clockwise = true;
    bool fixedSize = false;
    ObjectRef outline;
};

inline Error layout(Document& doc, const CycleLayout& layout, std::vector<ObjectId>& nodes,
        const std::vector<CatalyticMetal>& metals, const std::set<size_t>& edited, std::vector<ObjectRef>* made) {
    try {
        if (nodes.empty()) {
            for (const auto& metal : metals)
                nodes.push_back(metal.apply(doc, kInvalidObjectId, layout.origin + Vec2(nodes.size() * 40., 0)));
        } else {
            for (auto index : edited)
                metals.at(index).apply(doc, nodes.at(index), layout.origin);
        }
        auto result = arrange(doc, nodes, layout.origin, layout.horizontal, layout.vertical, layout.clockwise,
            made, layout.fixedSize, layout.rotation);
        if (result.ok() && layout.outline.valid()) result = doc.removeObject(layout.outline);
        if (result.ok() && made)
            for (auto id : nodes) {
                const auto structure = component(doc, id);
                made->insert(made->end(), structure.begin(), structure.end());
            }
        return result;
    } catch (const std::exception& error) {
        return {ErrorCode::InvalidArgument, error.what()};
    }
}

inline Error branch(Session& session, const CycleSelection& selection, bool input) {
    if (selection.arrows.size() != 1 || selection.nodes.size() > 1)
        return {ErrorCode::InvalidArgument, "请选择一根循环箭头，可同时选中一个独立试剂结构"};
    const auto edge = ObjectRef(ObjectKind::Arrow, selection.arrows.front());
    const auto* mode = static_cast<const Document&>(session.doc).extensions(edge).findValue(key());
    if (!mode || *mode != "cycle") return {ErrorCode::InvalidArgument, "请选择催化循环的主路径箭头"};
    arrows::ArrowParams params;
    bool present = false;
    auto error = arrows::readArrow(session.doc, edge.id, &params, &present);
    if (!error.ok()) return error;
    if (!present) return {ErrorCode::InvalidArgument, "找不到循环箭头"};
    const auto reagent = selection.nodes.empty() ? kInvalidObjectId : selection.nodes.front();
    if (reagent != kInvalidObjectId) {
        const auto structure = component(session.doc, reagent);
        if (std::find(structure.begin(), structure.end(), params.electronSource) != structure.end() ||
                std::find(structure.begin(), structure.end(), params.electronTarget) != structure.end())
            return {ErrorCode::InvalidArgument, "请选择循环外独立的试剂或产物"};
    }
    const auto delta = params.end - params.begin;
    const auto normal = Vec2(delta.y, -delta.x) * (1. / std::max(1., length(delta)));
    const auto position = point(session.doc, edge) + normal * (input ? 65. : 85.);
    const auto made = std::make_shared<ObjectId>(kInvalidObjectId);
    const auto* title = input ? "加入循环试剂" : "放出循环产物";
    error = session.submit(Command(title, kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
        [=](CommandContext& context) {
            auto& doc = context.document;
            auto id = reagent;
            Error result;
            if (id == kInvalidObjectId) {
                result = doc.addAtom(position, 6, &id);
                if (result.ok()) {
                    auto semantics = doc.atoms().alive(id)->semantics;
                    semantics.nodeType = uint8_t(NodeType::Query);
                    semantics.implicitHydrogens = 0;
                    semantics.showLabel = true;
                    result = doc.setAtomSemantics(id, semantics);
                }
                if (result.ok()) result = doc.setAtomLabel(id, "R");
            }
            if (result.ok()) {
                const auto atom = ObjectRef(ObjectKind::Atom, id);
                result = link(doc, input ? atom : edge, input ? edge : atom, input ? "in" : "out",
                    input ? .3 : -.3, made.get());
            }
            return result.ok() ? CommandResult::success() : CommandResult::fail(result.code, result.message);
        }), title);
    if (error.ok()) {
        session.setSelection({ObjectRef(ObjectKind::Arrow, *made)});
        session.status = "支线已连接，拖动试剂或中间体时连接会跟随";
    }
    return error;
}
}
