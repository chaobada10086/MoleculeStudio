#include "next/selection/bounds.hpp"

#include <algorithm>
#include <cmath>
#include <set>

// ===========================================================================
// Per-object bounds. One function per object class, each with the rule written
// next to it. See bounds.hpp for WHY the rule is what it is.
// ===========================================================================
namespace r62 {
namespace selection {

namespace {

core::Box emptyBox() {
    const double inf = std::numeric_limits<double>::infinity();
    return core::Box(core::Vec2(inf, inf), core::Vec2(-inf, -inf));
}

core::Box pointBox(const core::Vec2& p) {
    if (!p.finite()) return emptyBox();
    return core::Box(p, p);
}

// An alive Atom's box is its position. The element label is drawn AT the atom
// and does not shift chemistry geometry; align operates on chemistry geometry.
core::Box atomBounds(const core::Document& document, core::ObjectId id, BoundsReport* report) {
    const core::AtomRecord* atom = document.atoms().alive(id);
    if (!atom) return emptyBox();
    if (report) ++report->objectsMeasured;
    if (!atom->position.finite()) {
        if (report) ++report->emptyObjects;
        return emptyBox();
    }
    return pointBox(atom->position);
}

// A Bond's box is the segment between its ENDS' current positions. Endpoints
// are resolved through the live atoms, so a bond whose atom moved is boxed at
// the NEW place without a stored cache to go stale.
core::Box bondBounds(const core::Document& document, core::ObjectId id, BoundsReport* report) {
    const core::BondRecord* bond = document.bonds().alive(id);
    if (!bond) return emptyBox();
    const core::AtomRecord* a = document.atoms().alive(bond->beginAtom);
    const core::AtomRecord* b = document.atoms().alive(bond->endAtom);
    if (!a || !b) {
        // A bond with a dead endpoint is a document-consistency defect, not a
        // geometry one. Report it as empty rather than inventing a box.
        if (report) ++report->emptyObjects;
        return emptyBox();
    }
    if (report) ++report->objectsMeasured;
    if (!a->position.finite() || !b->position.finite()) {
        if (report) ++report->emptyObjects;
        return emptyBox();
    }
    core::Box box = pointBox(a->position);
    box.add(b->position);
    return box;
}

double effectiveTextSize(const core::Document& document, const core::TextRecord& text,
                         const BoundsOptions& options) {
    if (text.size > 0.0) return text.size;
    if (options.defaultTextSize > 0.0) return options.defaultTextSize;
    return document.defaultTextSize();
}

// Text is an abstract box: anchor + caller-supplied ink extent. With no sink
// the box is the anchor point and the fallback is COUNTED, because "we could
// not measure this" must not look like "this is a point".
core::Box textBounds(const core::Document& document, core::ObjectId id,
                     const BoundsOptions& options, BoundsReport* report) {
    const core::TextRecord* text = document.texts().alive(id);
    if (!text) return emptyBox();
    if (report) ++report->objectsMeasured;
    if (!text->position.finite()) {
        if (report) ++report->emptyObjects;
        return emptyBox();
    }
    if (options.textMetrics) {
        const std::string plain = document.plainText(id);
        const double size = effectiveTextSize(document, *text, options);
        std::string font = text->format.fontName;
        if (font.empty() && !options.defaultFont.empty()) font = options.defaultFont;
        core::Box box;
        if (options.textMetrics->textInkBox(plain, text->position, size, font, &box) &&
            !box.empty() && boxFinite(box)) {
            return box;
        }
        if (report) ++report->textWithoutMetrics;
        return pointBox(text->position);
    }
    if (report) ++report->textWithoutMetrics;
    return pointBox(text->position);
}

core::Box pathBounds(const core::Document& document, core::ObjectId id, BoundsReport* report) {
    const core::PathRecord* path = document.paths().alive(id);
    if (!path) return emptyBox();
    if (report) ++report->objectsMeasured;
    if (path->points.empty()) {
        if (report) ++report->emptyObjects;
        return emptyBox();
    }
    core::Box box = emptyBox();
    for (size_t i = 0; i < path->points.size(); ++i) {
        if (!path->points[i].finite()) continue;
        if (box.empty()) box = pointBox(path->points[i]);
        else box.add(path->points[i]);
    }
    if (box.empty() && report) ++report->emptyObjects;
    return box;
}

const core::ObjectSlab<core::AnnotationRecord>* annotationSlabFor(
        const core::Document& document, core::ObjectKind kind) {
    switch (kind) {
    case core::ObjectKind::Arrow:    return &document.arrows();
    case core::ObjectKind::Bracket:  return &document.brackets();
    case core::ObjectKind::Orbital:  return &document.orbitals();
    case core::ObjectKind::Symbol:   return &document.symbols();
    case core::ObjectKind::BioShape: return &document.bioShapes();
    default: return 0;
    }
}

// Only the geometry[] points the preset ACTUALLY uses contribute: a two-point
// arrow with two stale trailing points must not be boxed by them.
core::Box annotationBounds(const core::Document& document, core::ObjectKind kind,
                           core::ObjectId id, const BoundsOptions& options,
                           BoundsReport* report) {
    const core::ObjectSlab<core::AnnotationRecord>* slab = annotationSlabFor(document, kind);
    if (!slab) return emptyBox();
    const core::AnnotationRecord* record = slab->alive(id);
    if (!record) return emptyBox();
    if (report) ++report->objectsMeasured;

    uint8_t used = 2;
    bool addScale = false;
    if (options.annotationRules) {
        const AnnotationGeomRule rule = options.annotationRules->ruleFor(kind, record->presetId);
        used = rule.usedPoints;
        addScale = rule.addScale;
        if (!options.annotationRules->ruleCount() && report) ++report->unruledPresets;
    } else if (report) {
        ++report->unruledPresets;
    }
    if (used == 0) used = 1;
    if (used > 4) used = 4;

    core::Box box = emptyBox();
    for (uint8_t i = 0; i < used; ++i) {
        const core::Vec2& p = record->geometry[i];
        if (!p.finite()) continue;
        if (box.empty()) box = pointBox(p);
        else box.add(p);
    }
    if (box.empty()) {
        if (report) ++report->emptyObjects;
        return box;
    }
    // A placed glyph (a symbol or bio shape) has an extent of its own scale.
    if (addScale && record->scale != 0.0) {
        const double m = std::fabs(record->scale) * 0.5;
        box = inflateBox(box, m);
    }
    return box;
}

core::Box tableBounds(const core::Document& document, core::ObjectId id, BoundsReport* report) {
    const core::TableRecord* table = document.tables().alive(id);
    if (!table) return emptyBox();
    if (report) ++report->objectsMeasured;
    if (!boxFinite(table->frame) || table->frame.empty()) {
        if (report) ++report->emptyObjects;
        return emptyBox();
    }
    return table->frame;
}

core::Box groupBounds(const core::Document& document, core::ObjectId id,
                      const BoundsOptions& options, BoundsReport* report,
                      uint32_t depth, std::set<core::ObjectId>* visiting);

// Dispatch for one ref, with the group recursion separated out.
core::Box objectBoundsImpl(const core::Document& document, const core::ObjectRef& ref,
                           const BoundsOptions& options, BoundsReport* report,
                           uint32_t depth, std::set<core::ObjectId>* visiting) {
    switch (ref.kind) {
    case core::ObjectKind::Atom:     return atomBounds(document, ref.id, report);
    case core::ObjectKind::Bond:     return bondBounds(document, ref.id, report);
    case core::ObjectKind::Text:     return textBounds(document, ref.id, options, report);
    case core::ObjectKind::Path:     return pathBounds(document, ref.id, report);
    case core::ObjectKind::Table:    return tableBounds(document, ref.id, report);
    case core::ObjectKind::Group:
        return groupBounds(document, ref.id, options, report, depth, visiting);
    case core::ObjectKind::Arrow:
    case core::ObjectKind::Bracket:
    case core::ObjectKind::Orbital:
    case core::ObjectKind::Symbol:
    case core::ObjectKind::BioShape:
        return annotationBounds(document, ref.kind, ref.id, options, report);
    default:
        return emptyBox();
    }
}

core::Box groupBounds(const core::Document& document, core::ObjectId id,
                      const BoundsOptions& options, BoundsReport* report,
                      uint32_t depth, std::set<core::ObjectId>* visiting) {
    const core::GroupRecord* group = document.groups().alive(id);
    if (!group) return emptyBox();
    if (!options.expandGroups) return emptyBox();
    if (depth >= options.maxGroupDepth) {
        // A cycle (or an absurdly deep nest). Refuse to recurse further instead
        // of overflowing the stack.
        if (report) ++report->emptyObjects;
        return emptyBox();
    }
    if (visiting) {
        if (visiting->find(id) != visiting->end()) {
            if (report) ++report->emptyObjects;
            return emptyBox();
        }
        visiting->insert(id);
    }
    if (report) ++report->groupsExpanded;

    core::Box box = emptyBox();
    for (size_t i = 0; i < group->members.size(); ++i) {
        const core::ObjectRef& member = group->members[i];
        const core::Box memberBox = objectBoundsImpl(document, member, options, report,
                                                     depth + 1, visiting);
        unionBox(&box, memberBox);
    }
    if (visiting) visiting->erase(id);
    return box;
}

} // namespace

core::Box objectBounds(const core::Document& document, const core::ObjectRef& ref,
                       const BoundsOptions& options, BoundsReport* report) {
    if (!ref.valid()) {
        if (report) ++report->deadRefsSkipped;
        return emptyBox();
    }
    std::set<core::ObjectId> visiting;
    return objectBoundsImpl(document, ref, options, report, 0, &visiting);
}

core::Box objectBounds(const core::Document& document, const core::ObjectRef& ref) {
    BoundsOptions options;
    options.defaultTextSize = document.defaultTextSize();
    options.defaultFont = document.defaultFont();
    return objectBounds(document, ref, options, 0);
}

core::Box selectionBounds(const core::Document& document,
                          const std::vector<core::ObjectRef>& refs,
                          const BoundsOptions& options, BoundsReport* report) {
    core::Box box = emptyBox();
    for (size_t i = 0; i < refs.size(); ++i) {
        const core::Box one = objectBounds(document, refs[i], options, report);
        unionBox(&box, one);
    }
    return box;
}

core::Box selectionBounds(const core::Document& document,
                          const std::vector<core::ObjectRef>& refs) {
    BoundsOptions options;
    options.defaultTextSize = document.defaultTextSize();
    options.defaultFont = document.defaultFont();
    return selectionBounds(document, refs, options, 0);
}

namespace {

// Every alive object of every slab, in a stable order (kind, then id). A group
// is NOT expanded here: documentBounds is the extent of the objects that exist,
// and a group is one of them.
void collectAllRefs(const core::Document& document, std::vector<core::ObjectRef>* out) {
    if (!out) return;
    const std::vector<core::AtomRecord>& atoms = document.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i)
        if (atoms[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Atom, atoms[i].id));
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i)
        if (bonds[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Bond, bonds[i].id));
    const std::vector<core::TextRecord>& texts = document.texts().records();
    for (size_t i = 0; i < texts.size(); ++i)
        if (texts[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Text, texts[i].id));
    const std::vector<core::PathRecord>& paths = document.paths().records();
    for (size_t i = 0; i < paths.size(); ++i)
        if (paths[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Path, paths[i].id));
    const std::vector<core::TableRecord>& tables = document.tables().records();
    for (size_t i = 0; i < tables.size(); ++i)
        if (tables[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Table, tables[i].id));
    const std::vector<core::GroupRecord>& groups = document.groups().records();
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].isAlive()) out->push_back(core::ObjectRef(core::ObjectKind::Group, groups[i].id));
    const core::ObjectKind annotationKinds[5] = {
        core::ObjectKind::Arrow, core::ObjectKind::Bracket, core::ObjectKind::Orbital,
        core::ObjectKind::Symbol, core::ObjectKind::BioShape
    };
    const core::ObjectSlab<core::AnnotationRecord>* slabs[5] = {
        &document.arrows(), &document.brackets(), &document.orbitals(),
        &document.symbols(), &document.bioShapes()
    };
    for (int s = 0; s < 5; ++s) {
        const std::vector<core::AnnotationRecord>& records = slabs[s]->records();
        for (size_t i = 0; i < records.size(); ++i)
            if (records[i].isAlive())
                out->push_back(core::ObjectRef(annotationKinds[s], records[i].id));
    }
}

} // namespace

core::Box documentBounds(const core::Document& document,
                         const BoundsOptions& options, BoundsReport* report) {
    std::vector<core::ObjectRef> refs;
    collectAllRefs(document, &refs);
    return selectionBounds(document, refs, options, report);
}

core::Box documentBounds(const core::Document& document) {
    BoundsOptions options;
    options.defaultTextSize = document.defaultTextSize();
    options.defaultFont = document.defaultFont();
    return documentBounds(document, options, 0);
}

} // namespace selection
} // namespace r62
