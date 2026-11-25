#pragma once

#include "engine/projection_molecule.hpp"
#include "interop.hpp"
#include "stereo_annotation.hpp"
#include <limits>

namespace studio_projection {
using namespace r62;
using namespace r62::core;

inline ObjectId axisAt(integration::Session& session, Vec2 point) {
    const auto& document = session.doc;
    const auto eligible = [&](const BondRecord& bond) {
        const auto* begin = document.atoms().alive(bond.beginAtom);
        const auto* end = document.atoms().alive(bond.endAtom);
        return bond.order == BondOrder::Single && begin && end && begin->element == 6 && end->element == 6 &&
            (projection_molecule::marked(document, begin->id) || projection_molecule::marked(document, end->id));
    };
    const auto picked = session.pick(point);
    if (picked.kind == ObjectKind::Bond) {
        const auto* bond = document.bonds().alive(picked.id);
        if (bond && eligible(*bond)) return bond->id;
    }

    std::set<ObjectId> scope;
    for (const auto ref : studioio::expanded(document, session.selected))
        if (ref.kind == ObjectKind::Atom) scope.insert(ref.id);
    if (picked.kind == ObjectKind::Atom) {
        const auto atoms = projection_molecule::component(document, picked.id);
        scope.insert(atoms.begin(), atoms.end());
    }
    ObjectId axis = kInvalidObjectId;
    double best = std::numeric_limits<double>::infinity();
    for (const auto& bond : document.bonds().records()) {
        if (!bond.isAlive() || (!scope.count(bond.beginAtom) && !scope.count(bond.endAtom)) || !eligible(bond)) continue;
        const auto midpoint = (document.atoms().alive(bond.beginAtom)->position +
            document.atoms().alive(bond.endAtom)->position) * .5;
        const auto score = length(midpoint - point) + (projection_molecule::axis(document, bond) ? 0 : 10000);
        if (score < best) {
            best = score;
            axis = bond.id;
        }
    }
    return axis;
}

inline Error convert(integration::Session& session, ObjectId axis, int kind, int variant) {
    const auto* label = "转换立体投影";
    return session.submit(Command(label, kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance | kInvalidateSemantics,
        [axis, kind, variant](CommandContext& context) {
            const auto result = projection_molecule::convert(context.document, axis, kind, variant);
            return result.ok() ? CommandResult::success() : CommandResult::fail(result.code, result.message);
        }), label);
}

struct Endpoint {
    Vec2 capture;
    Vec2 caption;
    ObjectId atom = kInvalidObjectId;
    ObjectId text = kInvalidObjectId;
    ObjectId group = kInvalidObjectId;
    TextAlign align = TextAlign::Left;
    bool found() const { return atom != kInvalidObjectId || text != kInvalidObjectId || group != kInvalidObjectId; }
};

inline Endpoint endpointAt(integration::Session& session, Vec2 point) {
    const auto& document = session.doc;
    Endpoint result;
    const auto atom = session.nearestAtom(point, session.snapRadiusDevice());
    if (atom != kInvalidObjectId && projection_molecule::coreFor(document, atom) != kInvalidObjectId) {
        result.atom = atom;
        result.capture = document.atoms().alive(atom)->position;
        return result;
    }

    const double padding = session.view().deviceRadiusToDocument(2);
    for (const auto& group : document.groups().records()) {
        if (!group.isAlive() || !studio_stereo::isProjectionGroupName(group.label)) continue;
        for (const auto ref : group.members) {
            if (ref.kind != ObjectKind::Text || !document.texts().alive(ref.id)) continue;
            Box bounds(Vec2(1, 1), Vec2(-1, -1));
            for (const auto& primitive : session.scene.primitives()) {
                if (primitive.source != ref || primitive.bounds.empty()) continue;
                if (bounds.empty()) bounds = primitive.bounds;
                else { bounds.add(primitive.bounds.min); bounds.add(primitive.bounds.max); }
            }
            if (bounds.empty()) bounds = selection::objectBounds(document, ref);
            if (!studioio::inside(bounds, point, padding)) continue;
            result.text = ref.id;
            result.capture = (bounds.min + bounds.max) * .5;
            return result;
        }
    }

    Vec2 ray;
    double nearest = session.view().deviceRadiusToDocument(12);
    std::vector<Vec2> tips;
    for (const auto& group : document.groups().records()) {
        if (!group.isAlive() || !studio_stereo::isProjectionGroupName(group.label)) continue;
        std::vector<std::pair<Vec2, Vec2>> lines;
        Vec2 circle;
        double radius = 0;
        for (const auto ref : group.members) {
            if (ref.kind != ObjectKind::Path) continue;
            const auto* path = document.paths().alive(ref.id);
            if (!path || path->points.empty()) continue;
            if (!path->closed && path->points.size() == 2) lines.emplace_back(path->points.front(), path->points.back());
            else if (path->closed) {
                Box bounds(path->points.front(), path->points.front());
                for (const auto p : path->points) bounds.add(p);
                const auto candidate = std::max(bounds.width(), bounds.height()) * .5;
                if (candidate > radius) { radius = candidate; circle = (bounds.min + bounds.max) * .5; }
            }
        }
        std::vector<Vec2> groupTips;
        bool selected = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            for (const auto left : {true, false}) {
                const auto tip = left ? lines[i].first : lines[i].second;
                const auto from = left ? lines[i].second : lines[i].first;
                if (radius > 0 && length(tip - circle) <= radius * 1.12) continue;
                bool terminal = true;
                for (size_t j = 0; j < lines.size(); ++j) {
                    if (i == j) continue;
                    const auto start = lines[j].first, vector = lines[j].second - start;
                    const auto t = std::max(0., std::min(1., dot(tip - start, vector) / std::max(1e-9, lengthSq(vector))));
                    if (length(tip - start - vector * t) < document.defaultBondLength() * .025) { terminal = false; break; }
                }
                if (!terminal) continue;
                groupTips.push_back(tip);
                const auto distance = length(tip - point);
                if (distance >= nearest) continue;
                nearest = distance;
                result.group = group.id;
                result.capture = tip;
                ray = (tip - from) * (1. / std::max(1e-9, length(tip - from)));
                selected = true;
            }
        }
        if (selected) tips = std::move(groupTips);
    }
    if (!result.found()) return result;

    const auto* group = document.groups().alive(result.group);
    const auto endpoint = result.capture;
    double gap = document.defaultBondLength() * .8;
    for (const auto ref : group->members) {
        if (ref.kind != ObjectKind::Text || !document.texts().alive(ref.id)) continue;
        const auto bounds = selection::objectBounds(document, ref);
        const auto center = (bounds.min + bounds.max) * .5;
        const auto distance = length(center - endpoint);
        if (distance >= gap || std::any_of(tips.begin(), tips.end(), [&](Vec2 tip) {
            return length(center - tip) + .001 < distance;
        })) continue;
        gap = distance;
        result.text = ref.id;
    }

    const auto em = document.defaultTextSize();
    result.caption = endpoint + ray * (document.defaultBondLength() * .2);
    result.caption.y += ray.y < -.65 ? 0 : ray.y > .65 ? em * .72 : em * .32;
    if (group->label == "Newman projection") {
        double closest = document.defaultBondLength() * .5;
        Vec2 partner;
        bool paired = false;
        for (const auto tip : tips) {
            const auto distance = length(tip - endpoint);
            if (distance <= .001 || distance >= closest) continue;
            closest = distance;
            partner = tip;
            paired = true;
        }
        if (paired) result.caption = result.caption + (endpoint - partner) * (em * .5 / closest);
    }
    result.align = ray.x > .3 ? TextAlign::Left : ray.x < -.3 ? TextAlign::Right : TextAlign::Center;
    return result;
}

inline Command addEndpointCaption(Endpoint endpoint, std::string text, std::shared_ptr<ObjectId> created) {
    return Command("投影取代基", kInvalidateGeometry | kInvalidateAppearance,
        [endpoint, text, created](CommandContext& context) {
            auto& doc = context.document;
            if (!doc.groups().alive(endpoint.group)) return CommandResult::fail(ErrorCode::NotFound, "投影已改变");
            const auto result = doc.addText(endpoint.caption, doc.defaultTextSize(), text, created.get());
            if (!result.ok()) return CommandResult::fail(result.code, result.message);
            TextFormat format;
            format.fontName = doc.defaultFont();
            format.size = doc.defaultTextSize();
            format.align = uint8_t(endpoint.align);
            const auto formatted = doc.setTextFormat(*created, format);
            if (!formatted.ok()) return CommandResult::fail(formatted.code, formatted.message);
            const ObjectRef ref(ObjectKind::Text, *created);
            doc.extensions(ref).set("pst.attr.InterpretChemically", "no");
            doc.groups().mutableGet(endpoint.group)->members.push_back(ref);
            doc.noteTouchedGroup(endpoint.group);
            return CommandResult::success();
        });
}

inline void highlightEndpoint(render::OverlayScene& overlay, const ViewTransform& view, Vec2 point) {
    render::Primitive ring;
    ring.kind = render::PrimKind::Polyline;
    ring.style.stroke = render::Paint(0x2563EBFF, true);
    ring.style.fill.enabled = false;
    ring.style.strokeWidth = view.deviceRadiusToDocument(2.5);
    ring.layer = render::LayerId(900);
    ring.z = 100;
    const double radius = view.deviceRadiusToDocument(9);
    for (int i = 0; i <= 40; ++i) ring.points.push_back(point + direction(i * 9.) * radius);
    ring.bounds = render::geom::computeBounds(ring, .6);
    auto halo = ring;
    halo.style.stroke.rgba = 0xFFFFFFFF;
    halo.style.strokeWidth = view.deviceRadiusToDocument(5);
    halo.bounds = render::geom::computeBounds(halo, .6);
    overlay.add(halo);
    overlay.add(ring);
}
}
