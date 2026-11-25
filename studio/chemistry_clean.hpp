#pragma once

#include "chemistry_document.hpp"
#include "clean_component_spacing.hpp"
#include "next/advanced/arrange.hpp"

namespace studio_chemistry {
namespace advanced = r62::advanced;
namespace persistence = r62::persistence;

struct CleanupRequest {
    std::vector<ObjectId> atoms;
    std::map<int, ObjectId> fileAtoms;
    std::set<ObjectId> movable;
    Box bounds;
    std::string xml;
    bool reaction = false;
};

inline Error prepareCleanup(const Document& doc, const std::vector<ObjectRef>& selection,
                            bool reaction, CleanupRequest& request) {
    auto scopeRefs = scope(doc, selection);
    if (scopeRefs.empty()) return {ErrorCode::InvalidArgument, "请先绘制或选中结构"};
    request = CleanupRequest();
    request.reaction = reaction;
    const auto first = doc.atoms().alive(scopeRefs.front().id)->position;
    request.bounds = Box(first, first);
    for (auto ref : scopeRefs) if (ref.kind == ObjectKind::Atom) {
        request.atoms.push_back(ref.id);
        request.bounds.add(doc.atoms().alive(ref.id)->position);
    }
    if (reaction || selection.empty()) request.movable.insert(request.atoms.begin(), request.atoms.end());
    else {
        for (auto ref : studioio::expanded(doc, selection))
            if (ref.kind == ObjectKind::Atom && doc.atoms().alive(ref.id)) request.movable.insert(ref.id);
        if (request.movable.empty()) return {ErrorCode::InvalidArgument, "请先选择要整理的原子或化学键"};
    }

    Document subset;
    if (!reaction) subset.setDefaultBondLength(doc.defaultBondLength());
    std::vector<ObjectRef> inserted;
    auto result = studioio::append(doc, subset, scopeRefs, Vec2(), &inserted);
    if (!result.ok()) return result;
    size_t index = 0;
    for (auto ref : inserted) if (ref.kind == ObjectKind::Atom) {
        auto& bag = subset.extensions(ref);
        bag.set("pst.attr.MoleculeStudioCleanOriginal", std::to_string(request.atoms.at(index++)));
        bag.erase("studio.atom.z");
        bag.erase("pst.attr.xyz");
    }
    std::ostringstream encoded;
    result = persistence::writeCdxml(subset, encoded);
    if (!result.ok()) return result;
    tinyxml2::XMLDocument xml;
    const auto unmapped = Error(ErrorCode::ParseError, "整理结构无法建立完整原子对应");
    if (xml.Parse(encoded.str().c_str()) != tinyxml2::XML_SUCCESS || !xml.RootElement()) return unmapped;
    auto* page = xml.RootElement()->FirstChildElement("page");
    if (!page) return unmapped;
    std::set<ObjectId> originals;
    std::string fixed;
    for (auto* f = page->FirstChildElement("fragment"); f; f = f->NextSiblingElement("fragment")) {
        for (auto* node = f->FirstChildElement("n"); node; node = node->NextSiblingElement("n")) {
            if (!node->Attribute("MoleculeStudioCleanOriginal")) continue;
            auto original = ObjectId(node->UnsignedAttribute("MoleculeStudioCleanOriginal", kInvalidObjectId));
            int id = node->IntAttribute("id", -1);
            if (id < 0 || !std::binary_search(request.atoms.begin(), request.atoms.end(), original) ||
                !originals.insert(original).second || !request.fileAtoms.emplace(id, original).second) return unmapped;
            if (!request.movable.count(original)) {
                if (!fixed.empty()) fixed += ' ';
                fixed += std::to_string(id);
            }
        }
    }
    if (originals.size() != request.atoms.size()) return unmapped;
    if (!fixed.empty()) xml.RootElement()->SetAttribute("MoleculeStudioCleanFixedAtoms", fixed.c_str());
    tinyxml2::XMLPrinter wire;
    xml.Print(&wire);
    request.xml = wire.CStr();
    return Error();
}

// Re-anchor components independently after CDXML export translates the coordinates.
// A partially selected component uses an unchanged atom as the origin.
inline std::map<ObjectId, Vec2> anchorCleanup(const Document& doc,
        const std::map<ObjectId, Vec2>& returned, const std::set<ObjectId>& movable, double scale) {
    std::map<ObjectId, Vec2> positions;
    std::set<ObjectId> seen;
    for (const auto& entry : returned) {
        const auto id = entry.first;
        if (!seen.insert(id).second) continue;
        std::vector<ObjectId> component{id};
        Box before(doc.atoms().alive(id)->position, doc.atoms().alive(id)->position);
        Box after(entry.second, entry.second);
        auto anchor = kInvalidObjectId;
        for (size_t i = 0; i < component.size(); ++i) {
            const auto atom = component[i];
            before.add(doc.atoms().alive(atom)->position);
            after.add(returned.at(atom));
            if (anchor == kInvalidObjectId && !movable.count(atom)) anchor = atom;
            for (auto neighbor : doc.neighbours(atom))
                if (returned.count(neighbor) && seen.insert(neighbor).second) component.push_back(neighbor);
        }
        const auto oldOrigin = anchor == kInvalidObjectId ? (before.min + before.max) * .5
                                                          : doc.atoms().alive(anchor)->position;
        const auto newOrigin = anchor == kInvalidObjectId ? (after.min + after.max) * .5 : returned.at(anchor);
        for (auto atom : component)
            if (movable.count(atom)) positions.emplace(atom, oldOrigin + (returned.at(atom) - newOrigin) * scale);
    }
    return positions;
}

inline Error prepareCleanupCommand(const Document& doc, const CleanupRequest& request,
                                   const std::string& response, Command& command) {
    Document generated;
    auto result = persistence::readCdxml(response, generated);
    if (!result.ok()) return result;
    std::map<ObjectId, Vec2> returned;
    std::map<ObjectId, std::string> fragments;
    for (const auto& atom : generated.atoms().records()) if (atom.isAlive()) {
        auto id = request.fileAtoms.find(atom.sourceId);
        if (id == request.fileAtoms.end() || !atom.position.finite() ||
            !returned.emplace(id->second, atom.position).second)
            return {ErrorCode::ParseError, "整理结果的原子映射不完整"};
        const ObjectRef original(ObjectKind::Atom, id->second);
        if (request.movable.count(original.id) && doc.extensions(original).findValue("studio.nickname.fragment")) {
            const auto& bag = static_cast<const Document&>(generated).extensions({ObjectKind::Atom, atom.id});
            if (auto fragment = bag.findValue("studio.nickname.fragment")) fragments.emplace(original.id, *fragment);
        }
    }
    if (returned.size() != request.atoms.size())
        return {ErrorCode::ParseError, "整理结果改变了原子数量"};

    struct StereoUpdate { ObjectId id, begin, end; BondDisplay display; BondStereo stereo; };
    std::vector<StereoUpdate> wedges;
    auto isWedge = [](BondDisplay display) { return display == BondDisplay::Wedge || display == BondDisplay::WedgeHash; };
    for (const auto& bond : generated.bonds().records()) if (bond.isAlive()) {
        auto a = request.fileAtoms.find(generated.atoms().alive(bond.beginAtom)->sourceId);
        auto b = request.fileAtoms.find(generated.atoms().alive(bond.endAtom)->sourceId);
        if (a == request.fileAtoms.end() || b == request.fileAtoms.end())
            return {ErrorCode::ParseError, "整理结果的化学键映射不完整"};
        auto* original = doc.bonds().alive(doc.bondBetween(a->second, b->second));
        if (!original) return {ErrorCode::ParseError, "整理结果改变了连接关系"};
        if (!request.movable.count(a->second) && !request.movable.count(b->second)) continue;
        if (isWedge(bond.display) || isWedge(original->display))
            wedges.push_back({original->id, a->second, b->second, bond.display, bond.stereo});
    }
    const auto positions = anchorCleanup(doc, returned, request.movable,
        doc.defaultBondLength() / std::max(.1, generated.defaultBondLength()));
    const auto atoms = request.atoms;
    const auto movable = request.movable;
    const auto bounds = request.bounds;
    const bool reaction = request.reaction;
    command = Command("整理结构", kInvalidateGeometry,
        [positions, fragments, wedges, atoms, movable, bounds, reaction](CommandContext& context) {
            auto& doc = context.document;
            auto fail = [](const Error& error) { return CommandResult::fail(error.code, error.message); };
            for (const auto& atom : positions) {
                auto e = doc.setAtomPosition(atom.first, atom.second);
                if (!e.ok()) return fail(e);
                auto& bag = doc.extensions({ObjectKind::Atom, atom.first});
                bag.erase("studio.atom.z");
                bag.erase("pst.attr.xyz");
            }
            for (const auto& fragment : fragments)
                doc.extensions({ObjectKind::Atom, fragment.first}).set("studio.nickname.fragment", fragment.second);
            for (auto wedge : wedges) {
                auto e = doc.setBondEndpoints(wedge.id, wedge.begin, wedge.end);
                if (e.ok()) e = doc.setBondDisplay(wedge.id, wedge.display);
                if (e.ok()) e = doc.setBondStereo(wedge.id, wedge.stereo);
                if (!e.ok()) return fail(e);
            }
            if (reaction) {
                advanced::LayoutInput input;
                auto e = advanced::captureLayoutInput(doc, &input);
                if (!e.ok()) return fail(e);
                auto chosen = [&movable](ObjectId id) { return movable.count(id) != 0; };
                input.atoms.erase(std::remove_if(input.atoms.begin(), input.atoms.end(),
                    [&](const advanced::LayoutAtom& atom) { return !chosen(atom.id); }), input.atoms.end());
                input.bonds.erase(std::remove_if(input.bonds.begin(), input.bonds.end(),
                    [&](const advanced::LayoutBond& bond) { return !chosen(bond.a) || !chosen(bond.b); }), input.bonds.end());
                advanced::ArrangeOptions options;
                options.origin = Vec2(bounds.min.x, (bounds.min.y + bounds.max.y) * .5);
                advanced::ArrangePlan plan;
                e = advanced::planArrange(input, options, &plan);
                if (!e.ok()) return fail(e);
                auto arranged = advanced::applyArrangeCommand(plan, false).execute(context);
                if (!arranged.ok()) return arranged;
            }
            auto separated = studio::clean_spacing::separate(doc, atoms, reaction ? nullptr : &movable);
            return separated.ok() ? CommandResult::success() : fail(separated);
        });
    return Error();
}

inline const char* cleanupStatus(const CleanupRequest& request) {
    if (request.reaction) return "已用 RDKit 整理结构，保留原子、键级、立体化学和标注";
    if (request.movable.size() < request.atoms.size()) return "已局部整理结构，未选中部分保持不动";
    return "已整理结构，并优先保留原骨架朝向";
}
}
