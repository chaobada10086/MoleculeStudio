#pragma once

#include "chemistry_document.hpp"

namespace studio_stereo {
using namespace r62;
using namespace core;
using integration::Session;

inline bool isProjectionGroupName(const std::string& name) {
    return name == "Fischer projection" || name == "Sawhorse projection" || name == "Newman projection";
}

inline Error writeInput(Session& session, std::string& bytes) {
    auto refs = studio_chemistry::scope(session.doc, session.selected);
    std::set<ObjectRef> selected(session.selected.begin(), session.selected.end());
    for (auto ref : session.selected) {
        auto top = session.topLevelRef(ref);
        if (top.kind == ObjectKind::Group) selected.insert(top);
    }
    for (const auto& group : session.doc.groups().records()) {
        ObjectRef ref(ObjectKind::Group, group.id);
        if (group.isAlive() && isProjectionGroupName(group.label) &&
            (session.selected.empty() || selected.count(ref))) refs.push_back(ref);
    }
    if (refs.empty()) return {ErrorCode::InvalidArgument, "请先选择包含完整取代基的投影或分子结构"};
    Document subset;
    auto error = studioio::append(session.doc, subset, refs, Vec2(), nullptr);
    if (!error.ok()) return error;
    std::ostringstream xml;
    error = persistence::writeCdxml(subset, xml);
    if (error.ok()) bytes = xml.str();
    return error;
}

struct Label {
    bool bond = false;
    std::string cip;
    Vec2 center;
    ObjectId group = kInvalidObjectId;
};

inline ObjectId projectionAt(const Document& doc, Vec2 origin) {
    double best = 1e100;
    ObjectId found = kInvalidObjectId;
    for (const auto& group : doc.groups().records()) if (group.isAlive() && isProjectionGroupName(group.label)) {
        const auto bounds = selection::objectBounds(doc, {ObjectKind::Group, group.id});
        if (!studioio::inside(bounds, origin, doc.defaultBondLength())) continue;
        const auto distance = length((bounds.min + bounds.max) * .5 - origin);
        if (distance < best) { best = distance; found = group.id; }
    }
    return found;
}

inline Error apply(Session& session, const std::string& output) {
    std::istringstream stream(output);
    std::string line;
    std::getline(stream, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "stereolabels\t1")
        return {ErrorCode::ParseError, "无法读取构型标注结果"};
    std::vector<Label> labels;
    int skipped = 0;
    std::map<std::string, int> counts;
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        std::string type;
        fields >> type;
        if (type == "skipped") { fields >> skipped; continue; }
        if (type != "atom" && type != "bond" && type != "projection") continue;
        // File ids identify objects within the service's scratch CDXML.
        // Code and coordinates are expressed in the original document frame.
        Label label;
        int fileId;
        if (!(fields >> fileId >> label.cip >> label.center.x >> label.center.y) || !label.center.finite()) continue;
        if (label.cip.size() != 1 || std::string("RSrsEZ").find(label.cip[0]) == std::string::npos) continue;
        label.bond = type == "bond";
        if (type == "projection") {
            Vec2 origin;
            if (fields >> origin.x >> origin.y) label.group = projectionAt(session.doc, origin);
        }
        ++counts[label.cip];
        labels.push_back(std::move(label));
    }
    if (labels.empty()) {
        session.status = skipped ?
            "选中结构中没有可标注的 R/S/E/Z 构型（" + std::to_string(skipped) + " 处构型未定）" :
            "选中结构中没有手性中心或可指认的 E/Z 双键";
        return Error();
    }

    const double size = session.doc.defaultTextSize() > 0 ? session.doc.defaultTextSize() : 10.;
    const Vec2 halfExtent(size * .9, size * .525);
    auto result = session.submit(Command("构型标注", kInvalidateAppearance | kInvalidateGeometry,
        [labels, size, halfExtent](CommandContext& context) {
            auto& doc = context.document;
            auto fail = [](const Error& error) { return CommandResult::fail(error.code, error.message); };
            std::vector<ObjectId> previous;
            for (const auto& text : doc.texts().records()) {
                if (!text.isAlive()) continue;
                ObjectRef ref(ObjectKind::Text, text.id);
                const bool tagged = static_cast<const Document&>(doc).extensions(ref).findValue("studio.stereo.label") != nullptr;
                // Older or external CDXML may omit the tag. The previous CIP
                // caption at the same computed position is still reusable.
                const auto plain = doc.plainText(text.id);
                const bool cipCaption = plain.size() == 3 && plain.front() == '(' && plain.back() == ')' &&
                    std::string("RSrsEZ").find(plain[1]) != std::string::npos;
                if (!tagged && !cipCaption) continue;
                for (const auto& label : labels) {
                    const auto* group = tagged && label.group != kInvalidObjectId ? doc.groups().alive(label.group) : nullptr;
                    if (length(text.position + halfExtent - label.center) < 9 ||
                        (group && std::find(group->members.begin(), group->members.end(), ref) != group->members.end())) {
                        previous.push_back(text.id);
                        break;
                    }
                }
            }
            for (auto id : previous) {
                auto error = doc.removeObject({ObjectKind::Text, id});
                if (!error.ok()) return fail(error);
            }
            for (const auto& label : labels) {
                ObjectId id;
                auto error = doc.addText(label.center - halfExtent, size, "(" + label.cip + ")", &id);
                if (!error.ok()) return fail(error);
                TextFormat format;
                format.fontName = doc.defaultFont();
                format.size = size;
                error = doc.setTextFormat(id, format);
                if (!error.ok()) return fail(error);
                ObjectRef ref(ObjectKind::Text, id);
                doc.extensions(ref).set("studio.stereo.label", label.bond ? "bond" : "atom");
                doc.extensions(ref).set("pst.attr.InterpretChemically", "no");
                if (label.group != kInvalidObjectId) {
                    auto* group = doc.groups().mutableGet(label.group);
                    if (group && group->isAlive()) {
                        group->members.push_back(ref);
                        doc.noteTouchedGroup(label.group);
                    }
                }
            }
            return CommandResult::success();
        }), "构型标注");
    if (!result.ok()) return result;
    std::string detail;
    for (const auto* cip : {"R", "S", "E", "Z", "r", "s"}) if (counts.count(cip)) {
        if (!detail.empty()) detail += "、";
        detail += std::string(cip) + " " + std::to_string(counts.at(cip));
    }
    session.status = "已标注 " + std::to_string(labels.size()) + " 处构型（" + detail + "）";
    if (skipped) session.status += "；" + std::to_string(skipped) + " 处构型未定已跳过";
    session.status += " · 可按 Ctrl+Z 撤销";
    return result;
}
}
