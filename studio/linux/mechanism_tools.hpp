#pragma once

#include "ai_mechanism.hpp"
#include "engine/virtual_bonds.hpp"
#include "engine/ion_labels.hpp"
#include "reactive_center_model.hpp"

namespace linux_mechanism {
using namespace r62;
using namespace r62::core;
using integration::Session;

enum class Reagent { Base, Nucleophile, Proton };
enum class Placement { None, Equivalence, PartialPositive, PartialNegative, VirtualBond };

inline Error insertCommonIon(Session& session, studioions::Entry ion, int charge) {
    session.cancel();
    const auto position = session.view().center;
    auto id = std::make_shared<ObjectId>(kInvalidObjectId);
    auto error = session.submit(Command("插入常见离子", kInvalidateStructure | kInvalidateSemantics |
            kInvalidateGeometry | kInvalidateAppearance, [ion, charge, position, id](CommandContext& context) {
        auto& doc = context.document;
        auto result = doc.addAtom(position, ion.element, id.get());
        if (result.ok()) result = studioions::apply(doc, *id, ion, charge);
        return result.ok() ? CommandResult::success() : CommandResult::fail(result.code, result.message);
    }), "插入常见离子");
    if (error.ok()) {
        session.activateCommand("chooseSelectionTool_Marquee");
        session.setSelection({ObjectRef(ObjectKind::Atom, *id)});
    }
    return error;
}

inline Error insertReagent(Session& session, Reagent reagent) {
    session.cancel();
    const bool proton = reagent == Reagent::Proton;
    const bool base = reagent == Reagent::Base;
    const auto* title = proton ? "插入质子" : base ? "插入通用碱" : "插入通用亲核试剂";
    auto position = session.view().center;
    ObjectId existing = kInvalidObjectId;
    std::vector<ObjectRef> captions;
    if (!session.selected.empty()) {
        const auto selected = session.selected.front();
        if (!proton && session.selected.size() == 1 && selected.kind == ObjectKind::Atom) {
            existing = selected.id;
        } else if (proton) {
            if (session.selected.size() == 1 && selected.kind == ObjectKind::Text) {
                const auto* record = session.doc.texts().alive(selected.id);
                if (record && studioions::ascii(session.doc.plainText(selected.id)) == "H+") {
                    const auto size = record->size > 0 ? record->size : session.doc.defaultTextSize();
                    position = record->position + Vec2(size * .35, -size * .35);
                    captions.push_back(selected);
                }
            }
        } else {
            const TextRecord* label = nullptr;
            const TextRecord* charge = nullptr;
            for (const auto ref : session.selected) {
                const auto* text = ref.kind == ObjectKind::Text ? session.doc.texts().alive(ref.id) : nullptr;
                const auto value = text ? session.doc.plainText(ref.id) : std::string();
                if (!label && (base ? value == "B" : value == "Nu" || value == "Nu:")) label = text;
                else if (!charge && (value == "-" || value == "−")) charge = text;
                else return {ErrorCode::InvalidArgument, "请选择单独的 B 或 Nu 文字，可同时选中旁边的负号"};
            }
            if (!label) return {ErrorCode::InvalidArgument, "请选择单独的 B 或 Nu 文字"};
            const auto size = label->size > 0 ? label->size : session.doc.defaultTextSize();
            if (charge && length(charge->position - label->position) > size * 2)
                return {ErrorCode::InvalidArgument, "选中的负号离试剂文字太远"};
            position = label->position + Vec2(size * (base ? .3 : .6), -size * .35);
            captions = session.selected;
        }
    }
    auto created = std::make_shared<ObjectId>(existing);
    const auto result = session.submit(Command(title, kInvalidateStructure | kInvalidateGeometry |
            kInvalidateSemantics | kInvalidateAppearance,
        [position, captions, proton, base, created](CommandContext& context) {
            auto& doc = context.document;
            Error error;
            if (*created == kInvalidObjectId) error = doc.addAtom(position, proton ? 1 : 6, created.get());
            if (error.ok() && proton) {
                auto semantics = doc.atoms().alive(*created)->semantics;
                semantics.charge = 1;
                semantics.implicitHydrogens = 0;
                semantics.showLabel = true;
                error = doc.setAtomSemantics(*created, semantics);
            } else if (error.ok()) {
                error = mechanism::setGenericRole(doc, *created, base ? "base" : "nucleophile");
                if (error.ok() && !base)
                    doc.extensions(ObjectRef(ObjectKind::Atom, *created)).set(
                        integration::molecular::electronMarkerKey(false), "auto");
            }
            for (const auto ref : captions) {
                if (!error.ok()) break;
                error = doc.removeObject(ref);
            }
            return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
        }), title);
    if (result.ok()) {
        session.activateCommand("chooseSelectionTool_Marquee");
        session.setSelection({ObjectRef(ObjectKind::Atom, *created)});
        session.status = proton ? "已插入 H⁺" : base ? "已插入通用碱 B⁻" : "已插入 Nu⁻，可从孤对电子起笔";
    }
    return result;
}

inline bool canAttachHydrogen(const Document& doc, ObjectRef ref) {
    const auto* atom = ref.kind == ObjectKind::Atom ? doc.atoms().alive(ref.id) : nullptr;
    return atom && atom->semantics.charge == -1 && mechanism::genericRole(doc, ref.id) == "base" &&
        doc.incidentBonds(ref.id).empty();
}

inline Error attachHydrogen(Session& session, ObjectRef ref) {
    return session.submit(Command("添加 B–H 化学键", kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
        [ref](CommandContext& context) {
            auto& doc = context.document;
            if (!canAttachHydrogen(doc, ref)) return CommandResult::fail(ErrorCode::InvalidArgument, "请选择尚未成键的 B⁻");
            const auto* atom = doc.atoms().alive(ref.id);
            auto semantics = atom->semantics;
            semantics.charge = 0;
            ObjectId hydrogen, bond;
            auto error = doc.addAtom(atom->position + Vec2(doc.defaultBondLength(), 0), 1, &hydrogen);
            if (error.ok()) {
                auto explicitHydrogen = doc.atoms().alive(hydrogen)->semantics;
                explicitHydrogen.showLabel = true;
                explicitHydrogen.implicitHydrogens = 0;
                error = doc.setAtomSemantics(hydrogen, explicitHydrogen);
            }
            if (error.ok()) error = doc.addBond(ref.id, hydrogen, &bond);
            if (error.ok()) error = doc.setAtomSemantics(ref.id, semantics);
            return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
        }), "添加 B–H 化学键");
}

inline Error captionElectronState(Session& session, ObjectRef ref, int state, bool circled) {
    const auto* caption = session.doc.texts().alive(ref.id);
    if (!caption) return {ErrorCode::NotFound, "文字不存在"};
    const auto label = session.doc.plainText(ref.id);
    const auto* ion = state == 0 ? mechanism::lonePairGroup(label) : studioions::find(label);
    const auto* compact = state == 0 ? mechanism::compactLonePairGroup(label) : nullptr;
    const bool phosphine = state == 0 && mechanism::triphenylphosphineLabel(label);
    if (!ion && !compact && !phosphine) return {ErrorCode::InvalidArgument, "请选择可识别的离子缩写或原子"};
    auto point = caption->position;
    const double size = caption->size > 0 ? caption->size : session.doc.defaultTextSize();
    auto runs = session.doc.runs(ref.id);
    std::string offset;
    if (compact || phosphine) {
        text::LayoutOptions options;
        options.defaultFont = caption->format.fontName.empty() ? session.doc.defaultFont() : caption->format.fontName;
        options.defaultSize = size;
        options.defaultFlags = caption->format.flags;
        for (auto& run : runs) {
            if (run.fontName.empty()) run.fontName = options.defaultFont;
            if (run.size <= 0) run.size = options.defaultSize;
        }
        text::RichLayout layout;
        const auto measured = text::layoutRichText(runs, Vec2(), options, &session.glyphs, &layout);
        if (!measured.ok()) return measured;
        auto baseline = point;
        if (caption->format.align == 1) baseline.x -= layout.metrics.width * .5;
        else if (caption->format.align == 2) baseline.x -= layout.metrics.width;
        layout.anchor = baseline - Vec2(0, layout.metrics.firstBaseline);
        point = compact ? mechanism::compactLonePairCenter(label, layout, baseline) :
            mechanism::triphenylphosphineCenter(label, &layout, baseline);
        std::ostringstream encoded;
        encoded.precision(16);
        encoded << baseline.x - point.x << ' ' << baseline.y - point.y;
        offset = encoded.str();
    } else point = point + Vec2(size * .35, -size * .35);

    auto created = std::make_shared<ObjectId>(kInvalidObjectId);
    const auto color = caption->format.colorRGBA;
    const auto result = session.submit(Command("将文字转换为带电子标记的结构",
        kInvalidateStructure | kInvalidateSemantics | kInvalidateGeometry | kInvalidateAppearance,
        [ref, point, label, color, runs, offset, ion, compact, phosphine, state, circled, created](CommandContext& context) {
            auto& doc = context.document;
            auto error = doc.addAtom(point, compact ? compact->element : phosphine ? 15 : ion->element, created.get());
            if (error.ok() && (compact || phosphine)) {
                if (compact && compounddata::find(label)) error = nicknames::apply(doc, *created, label);
                if (error.ok() && compact) error = doc.setElement(*created, compact->element);
                if (error.ok()) {
                    auto semantics = doc.atoms().alive(*created)->semantics;
                    semantics.nodeType = uint8_t(compact ? NodeType::Element : NodeType::Query);
                    semantics.showLabel = true;
                    semantics.implicitHydrogens = compact ? compact->hydrogens : 0;
                    error = doc.setAtomSemantics(*created, semantics);
                }
                if (error.ok()) error = doc.setAtomLabel(*created, label);
                if (error.ok()) {
                    studioatom::set(doc, *created, runs);
                    auto& extensions = doc.extensions(ObjectRef(ObjectKind::Atom, *created));
                    extensions.set("studio.label.offset", offset);
                    extensions.set("studio.label.text", label);
                    if (color & 255) extensions.set("studio.color", std::to_string(color));
                    if (compact) {
                        extensions.set("studio.mechanism.compact.valence", std::to_string(compact->hidden));
                        extensions.set("studio.mechanism.ion.base", compact->label);
                    }
                }
            } else if (error.ok()) error = studioions::apply(doc, *created, *ion, state);
            if (error.ok()) {
                auto& extensions = doc.extensions(ObjectRef(ObjectKind::Atom, *created));
                if (state == 0) extensions.set(mechanism::lonePairKey(), "1");
                if (circled) extensions.set("pst.attr.MoleculeStudioChargeCircle", "1");
                error = doc.removeObject(ref);
            }
            return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
        }), "将文字转换为带电子标记的结构");
    if (result.ok()) session.setSelection({ObjectRef(ObjectKind::Atom, *created)});
    return result;
}

inline Error setElectronState(Session& session, int state, bool circled) {
    if (state != 2 && session.selected.size() == 1 && session.selected.front().kind == ObjectKind::Text)
        return captionElectronState(session, session.selected.front(), state, circled);
    std::set<ObjectId> selected;
    for (const auto ref : studioio::expanded(session.doc, session.selected)) {
        if (ref.kind == ObjectKind::Atom && session.doc.atoms().alive(ref.id)) selected.insert(ref.id);
        else if (state == 0 && ref.kind == ObjectKind::Bond) {
            if (const auto* bond = session.doc.bonds().alive(ref.id)) {
                selected.insert(bond->beginAtom);
                selected.insert(bond->endAtom);
            }
        }
    }
    if (selected.empty()) return {ErrorCode::InvalidArgument, "请先选择要标注电子状态的原子"};
    std::vector<ObjectId> ids(selected.begin(), selected.end());
    if (state == 0) {
        ids = mechanism::lonePairTargets(session.doc, ids);
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](ObjectId id) {
            return mechanism::lonePairCount(session.doc, id) >= mechanism::lonePairCapacity(session.doc, id);
        }), ids.end());
        if (ids.empty()) return {ErrorCode::InvalidArgument, "选区中没有可添加的孤对电子"};
    }
    const auto command = atomElectronStateCommand(ids, state, circled);
    return session.submit(command, command.name());
}
}
