#include "next/selection/transform.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace r62 {
namespace selection {

namespace {

const double kPi = 3.14159265358979323846;

// A group whose members are transformed: the group record has no geometry, so
// the recursion is what "the group moved" means. Bounded so a corrupt cycle
// cannot recurse forever.
void expandRefs(const core::Document& document, const core::ObjectRef& ref,
                uint32_t depth, std::vector<core::ObjectRef>* out,
                uint64_t* groupsTouched) {
    if (!out) return;
    if (depth > 64) return;
    if (ref.kind == core::ObjectKind::Group) {
        const core::GroupRecord* group = document.groups().alive(ref.id);
        if (!group) return;
        if (groupsTouched) ++(*groupsTouched);
        for (size_t i = 0; i < group->members.size(); ++i) {
            const core::ObjectRef& member = group->members[i];
            if (!member.valid()) continue;
            if (member.kind == core::ObjectKind::Group) {
                expandRefs(document, member, depth + 1, out, groupsTouched);
            } else {
                out->push_back(member);
            }
        }
        return;
    }
    out->push_back(ref);
}

bool inRegion(const TransformOptions& options, const core::Box& box) {
    if (!options.useRegionFilter) return true;
    return boxIntersectsBox(options.region, box);
}

} // namespace

const char* textSizePolicyName(TextSizePolicy policy) {
    switch (policy) {
    case TextSizePolicy::PreservePointSize: return "preserve-point-size";
    case TextSizePolicy::MatchUniformOnly:  return "match-uniform-only";
    case TextSizePolicy::ScaleWithGeometry: return "scale-with-geometry";
    }
    return "unknown";
}

const char* alignModeName(AlignMode mode) {
    switch (mode) {
    case AlignMode::Left:             return "left";
    case AlignMode::HorizontalCenter: return "h-center";
    case AlignMode::Right:            return "right";
    case AlignMode::Top:              return "top";
    case AlignMode::VerticalCenter:   return "v-center";
    case AlignMode::Bottom:           return "bottom";
    }
    return "unknown";
}

const char* distributeModeName(DistributeMode mode) {
    switch (mode) {
    case DistributeMode::HorizontalCenters: return "h-centers";
    case DistributeMode::VerticalCenters:   return "v-centers";
    case DistributeMode::HorizontalGaps:    return "h-gaps";
    case DistributeMode::VerticalGaps:      return "v-gaps";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Affine2
// ---------------------------------------------------------------------------
Affine2 Affine2::translation(const core::Vec2& delta) {
    Affine2 m;
    m.m02 = delta.x;
    m.m12 = delta.y;
    return m;
}

Affine2 Affine2::rotation(const core::Vec2& pivot, double degrees) {
    const double r = degrees * kPi / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    Affine2 m;
    // Document space has y DOWN, and angles are counter-clockwise in MATH
    // orientation (contracts.hpp). direction() is the sanctioned mapping, so the
    // rotation matrix uses the same sign convention: math CCW in a y-down space
    // is x' = x*c + y*s, y' = -x*s + y*c about the origin.
    m.m00 = c;  m.m01 = s;   m.m02 = pivot.x - c * pivot.x - s * pivot.y;
    m.m10 = -s; m.m11 = c;   m.m12 = pivot.y + s * pivot.x - c * pivot.y;
    return m;
}

Affine2 Affine2::scaling(const core::Vec2& pivot, double sx, double sy) {
    Affine2 m;
    m.m00 = sx; m.m11 = sy;
    m.m02 = pivot.x - sx * pivot.x;
    m.m12 = pivot.y - sy * pivot.y;
    return m;
}

Affine2 Affine2::flip(const core::Vec2& pivot, bool horizontal) {
    return horizontal ? scaling(pivot, -1.0, 1.0) : scaling(pivot, 1.0, -1.0);
}

Affine2 Affine2::compose(const Affine2& first, const Affine2& second) {
    Affine2 m;
    m.m00 = second.m00 * first.m00 + second.m01 * first.m10;
    m.m01 = second.m00 * first.m01 + second.m01 * first.m11;
    m.m02 = second.m00 * first.m02 + second.m01 * first.m12 + second.m02;
    m.m10 = second.m10 * first.m00 + second.m11 * first.m10;
    m.m11 = second.m10 * first.m01 + second.m11 * first.m11;
    m.m12 = second.m10 * first.m02 + second.m11 * first.m12 + second.m12;
    return m;
}

core::Vec2 Affine2::apply(const core::Vec2& p) const {
    return core::Vec2(m00 * p.x + m01 * p.y + m02, m10 * p.x + m11 * p.y + m12);
}

bool Affine2::finite() const {
    return std::isfinite(m00) && std::isfinite(m01) && std::isfinite(m02) &&
           std::isfinite(m10) && std::isfinite(m11) && std::isfinite(m12);
}

double Affine2::uniformFactor() const {
    const double det = determinant();
    if (!std::isfinite(det) || det == 0.0) return 0.0;
    const double f = std::sqrt(std::fabs(det));
    return std::isfinite(f) ? f : 0.0;
}

bool Affine2::isUniform() const {
    // Uniform means: equal axis lengths AND no shear. m01/m10 must vanish.
    if (m01 != 0.0 || m10 != 0.0) return false;
    const double ax = std::fabs(m00);
    const double ay = std::fabs(m11);
    if (ax == 0.0 || ay == 0.0) return false;
    return std::fabs(ax - ay) <= 1e-12 * std::max(ax, ay);
}

bool Affine2::isTranslationOnly() const {
    return m00 == 1.0 && m11 == 1.0 && m01 == 0.0 && m10 == 0.0;
}

std::string Affine2::describe() const {
    std::ostringstream out;
    out << "[" << m00 << " " << m01 << " " << m02 << "; "
        << m10 << " " << m11 << " " << m12 << "]";
    return out.str();
}

// ---------------------------------------------------------------------------
// TransformReport
// ---------------------------------------------------------------------------
TransformReport::TransformReport() { clear(); }

void TransformReport::clear() {
    atomsMoved = 0; textsMoved = 0; pathsMoved = 0; annotationsMoved = 0;
    tablesMoved = 0; groupsTouched = 0; bondEndpointsMoved = 0;
    bondsUnaffected = 0; textSizesChanged = 0; refsSkippedDead = 0;
    refsSkippedUnsupported = 0; objectsConsidered = 0; objectsMoved = 0;
}

std::string TransformReport::summary() const {
    std::ostringstream out;
    out << "atoms=" << atomsMoved << " texts=" << textsMoved
        << " paths=" << pathsMoved << " annotations=" << annotationsMoved
        << " tables=" << tablesMoved << " groups=" << groupsTouched
        << " bondEndpoints=" << bondEndpointsMoved
        << " bondsUnaffected=" << bondsUnaffected
        << " textSizes=" << textSizesChanged
        << " dead=" << refsSkippedDead << " unsupported=" << refsSkippedUnsupported;
    return out.str();
}

// ---------------------------------------------------------------------------
// transformObjects
// ---------------------------------------------------------------------------
TransformReport transformObjects(core::Document& document,
                                 const std::vector<core::ObjectRef>& refs,
                                 const Affine2& matrix,
                                 const TransformOptions& options) {
    TransformReport report;
    if (!matrix.finite()) {
        report.refsSkippedUnsupported = refs.size();
        return report;
    }

    // 1. Expand groups and bonds into the set of objects that actually STORE
    //    geometry. A bond contributes its endpoints; the atoms are deduplicated
    //    so a bond and one of its atoms selected together move the atom ONCE.
    std::vector<core::ObjectRef> expanded;
    uint64_t groupsTouched = 0;
    for (size_t i = 0; i < refs.size(); ++i) {
        const core::ObjectRef& ref = refs[i];
        if (!ref.valid()) { ++report.refsSkippedDead; continue; }
        if (ref.kind == core::ObjectKind::Group) {
            expandRefs(document, ref, 0, &expanded, &groupsTouched);
            continue;
        }
        if (ref.kind == core::ObjectKind::Bond) {
            const core::BondRecord* bond = document.bonds().alive(ref.id);
            if (!bond) { ++report.refsSkippedDead; continue; }
            if (!options.moveBondEndpoints) { ++report.bondsUnaffected; continue; }
            if (document.atoms().alive(bond->beginAtom)) {
                expanded.push_back(core::ObjectRef(core::ObjectKind::Atom, bond->beginAtom));
                ++report.bondEndpointsMoved;
            }
            if (document.atoms().alive(bond->endAtom)) {
                expanded.push_back(core::ObjectRef(core::ObjectKind::Atom, bond->endAtom));
                ++report.bondEndpointsMoved;
            }
            continue;
        }
        expanded.push_back(ref);
    }

    std::sort(expanded.begin(), expanded.end());
    expanded.erase(std::unique(expanded.begin(), expanded.end()), expanded.end());

    std::set<core::ObjectId> movedAtoms;
    for (size_t i = 0; i < expanded.size(); ++i) {
        const core::ObjectRef ref = expanded[i];
        switch (ref.kind) {
        case core::ObjectKind::Atom: {
            const core::AtomRecord* atom = document.atoms().alive(ref.id);
            if (!atom) { ++report.refsSkippedDead; break; }
            if (!inRegion(options, core::Box(atom->position, atom->position))) break;
            const core::Vec2 moved = matrix.apply(atom->position);
            if (!moved.finite()) { ++report.refsSkippedUnsupported; break; }
            document.setAtomPosition(ref.id, moved);
            movedAtoms.insert(ref.id);
            ++report.atomsMoved;
            break;
        }
        case core::ObjectKind::Text: {
            const core::TextRecord* text = document.texts().alive(ref.id);
            if (!text) { ++report.refsSkippedDead; break; }
            core::Box box(text->position, text->position);
            if (!inRegion(options, box)) break;
            const core::Vec2 moved = matrix.apply(text->position);
            if (!moved.finite()) { ++report.refsSkippedUnsupported; break; }
            document.setTextPosition(ref.id, moved);
            // Captions rotate; atom element labels remain upright in their own
            // atom branch. Store the same CDXML 16.16-degree field the renderer
            // and reader already use, and track it in the undo transaction.
            const double rotation=std::atan2(matrix.m10,matrix.m00)*180.0/kPi;
            if(matrix.determinant()>0&&std::abs(rotation)>1e-9){
                auto& bag=document.extensions(ref);double before=0;if(auto value=bag.findValue("studio.text.RotationAngle")){std::istringstream input(*value);input>>before;}
                double after=std::fmod(before/65536.0+rotation,360.0);std::ostringstream value;value.precision(17);value<<std::llround(after*65536.0);
                bag.set("studio.text.RotationAngle",value.str());bag.set("studio.text.rotation.edited","1");
            }
            // --- the em-size rule (see transform.hpp) ------------------------
            if (text->size > 0.0) {
                const double factor = matrix.uniformFactor();
                bool scale = false;
                if (options.textSizePolicy == TextSizePolicy::ScaleWithGeometry)
                    scale = factor > 0.0 && factor != 1.0;
                else if (options.textSizePolicy == TextSizePolicy::MatchUniformOnly)
                    scale = matrix.isUniform() && factor > 0.0 && factor != 1.0;
                if (scale) {
                    const double scaled = text->size * factor;
                    if (std::isfinite(scaled) && scaled > 0.0) {
                        document.setTextSize(ref.id, scaled);
                        ++report.textSizesChanged;
                    }
                }
            }
            ++report.textsMoved;
            break;
        }
        case core::ObjectKind::Path: {
            const core::PathRecord* path = document.paths().alive(ref.id);
            if (!path) { ++report.refsSkippedDead; break; }
            core::Box box;
            box.min = core::Vec2(0, 0);
            box.max = core::Vec2(0, 0);
            bool first = true;
            core::Box pathBox;
            for (size_t p = 0; p < path->points.size(); ++p) {
                if (first) { pathBox = core::Box(path->points[p], path->points[p]); first = false; }
                else pathBox.add(path->points[p]);
            }
            (void)box;
            if (!first && !inRegion(options, pathBox)) break;
            core::PathRecord updated = *path;
            bool ok = true;
            for (size_t p = 0; p < updated.points.size(); ++p) {
                const core::Vec2 moved = matrix.apply(updated.points[p]);
                if (!moved.finite()) { ok = false; break; }
                updated.points[p] = moved;
            }
            if (!ok) { ++report.refsSkippedUnsupported; break; }
            // Written through ObjectSlab::mutableGet (public T01 surface) and
            // declared to the transaction with noteTouchedPath, exactly the way
            // Document::setLineStyle writes a path. Going through the slab keeps
            // the object's ID, its alive flag and every other field intact -
            // which is what "undo restores ALL attributes" needs.
            core::PathRecord* mutablePath = document.paths().mutableGet(ref.id);
            if (!mutablePath || !mutablePath->isAlive()) { ++report.refsSkippedDead; break; }
            mutablePath->points.swap(updated.points);
            document.noteTouchedPath(ref.id);
            document.bumpStructure();
            ++report.pathsMoved;
            break;
        }
        case core::ObjectKind::Arrow:
        case core::ObjectKind::Bracket:
        case core::ObjectKind::Orbital:
        case core::ObjectKind::Symbol:
        case core::ObjectKind::BioShape: {
            core::ObjectSlab<core::AnnotationRecord>* slab = 0;
            switch (ref.kind) {
            case core::ObjectKind::Arrow:    slab = &document.arrows(); break;
            case core::ObjectKind::Bracket:  slab = &document.brackets(); break;
            case core::ObjectKind::Orbital:  slab = &document.orbitals(); break;
            case core::ObjectKind::Symbol:   slab = &document.symbols(); break;
            default:                         slab = &document.bioShapes(); break;
            }
            const core::AnnotationRecord* record = slab->alive(ref.id);
            if (!record) { ++report.refsSkippedDead; break; }
            core::AnnotationRecord updated = *record;
            core::Box annBox;
            bool first = true;
            for (int p = 0; p < 4; ++p) {
                if (first) { annBox = core::Box(updated.geometry[p], updated.geometry[p]); first = false; }
                else annBox.add(updated.geometry[p]);
            }
            if (!inRegion(options, annBox)) break;
            bool ok = true;
            for (int p = 0; p < 4; ++p) {
                const core::Vec2 moved = matrix.apply(updated.geometry[p]);
                if (!moved.finite()) { ok = false; break; }
                updated.geometry[p] = moved;
            }
            if (!ok) { ++report.refsSkippedUnsupported; break; }
            // Rotation is a STORED field on an annotation. A pure rotation about
            // the annotation's own centre can be folded into it, but a general
            // affine cannot be, so the geometry points carry the transform and
            // the stored rotation is LEFT ALONE. That keeps "rotate twice and
            // undo restores every attribute" true (the attribute never changed).
            // setAnnotationGeometry is the T01 public mutator for exactly this
            // and it declares the touch itself; using it instead of a raw record
            // write is what keeps the invalidation and history honest.
            const core::Error written =
                document.setAnnotationGeometry(ref.id, updated.geometry, 4);
            if (!written.ok()) { ++report.refsSkippedUnsupported; break; }
            ++report.annotationsMoved;
            break;
        }
        case core::ObjectKind::Table: {
            const core::TableRecord* table = document.tables().alive(ref.id);
            if (!table) { ++report.refsSkippedDead; break; }
            if (!inRegion(options, table->frame)) break;
            core::TableRecord updated = *table;
            const core::Vec2 a = matrix.apply(updated.frame.min);
            const core::Vec2 b = matrix.apply(updated.frame.max);
            if (!a.finite() || !b.finite()) { ++report.refsSkippedUnsupported; break; }
            updated.frame = core::Box(core::Vec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                                      core::Vec2(std::max(a.x, b.x), std::max(a.y, b.y)));
            // The table's CELLS are Text objects and are transformed when they
            // are in the selection; a table that is transformed without its cells
            // being listed would leave the text behind, so the cells are pulled
            // in here - a table is atomic for a transform.
            for (size_t c = 0; c < updated.cells.size(); ++c) {
                const core::ObjectId cell = updated.cells[c];
                if (cell == core::kInvalidObjectId) continue;
                const core::TextRecord* cellText = document.texts().alive(cell);
                if (!cellText) continue;
                const core::Vec2 moved = matrix.apply(cellText->position);
                if (!moved.finite()) continue;
                document.setTextPosition(cell, moved);
                ++report.textsMoved;
            }
            core::TableRecord* mutableTable = document.tables().mutableGet(ref.id);
            if (!mutableTable || !mutableTable->isAlive()) { ++report.refsSkippedDead; break; }
            mutableTable->frame = updated.frame;
            document.noteTouchedTable(ref.id);
            document.bumpStructure();
            ++report.tablesMoved;
            break;
        }
        default:
            ++report.refsSkippedUnsupported;
            break;
        }
    }
    report.groupsTouched = groupsTouched;
    return report;
}

// ---------------------------------------------------------------------------
// rotateObjects / scaleObjects / stretchObjects / flipObjects
// ---------------------------------------------------------------------------
TransformReport rotateObjects(core::Document& document,
                              const std::vector<core::ObjectRef>& refs,
                              const core::Vec2& pivot, double degrees,
                              const TransformOptions& options) {
    if (!pivot.finite() || !std::isfinite(degrees)) {
        TransformReport report;
        report.refsSkippedUnsupported = refs.size();
        return report;
    }
    return transformObjects(document, refs, Affine2::rotation(pivot, degrees), options);
}

TransformReport scaleObjects(core::Document& document,
                             const std::vector<core::ObjectRef>& refs,
                             const core::Vec2& pivot, double factor,
                             const TransformOptions& options) {
    if (!pivot.finite() || !std::isfinite(factor) || factor == 0.0) {
        TransformReport report;
        report.refsSkippedUnsupported = refs.size();
        return report;
    }
    return transformObjects(document, refs, Affine2::scaling(pivot, factor, factor), options);
}

TransformReport stretchObjects(core::Document& document,
                               const std::vector<core::ObjectRef>& refs,
                               const core::Vec2& pivot, double sx, double sy,
                               const TransformOptions& options) {
    if (!pivot.finite() || !std::isfinite(sx) || !std::isfinite(sy) ||
        sx == 0.0 || sy == 0.0) {
        TransformReport report;
        report.refsSkippedUnsupported = refs.size();
        return report;
    }
    return transformObjects(document, refs, Affine2::scaling(pivot, sx, sy), options);
}

TransformReport flipObjects(core::Document& document,
                            const std::vector<core::ObjectRef>& refs,
                            const core::Vec2& pivot, bool horizontal,
                            const TransformOptions& options) {
    if (!pivot.finite()) {
        TransformReport report;
        report.refsSkippedUnsupported = refs.size();
        return report;
    }
    return transformObjects(document, refs, Affine2::flip(pivot, horizontal), options);
}

// ---------------------------------------------------------------------------
// placeAtoms
// ---------------------------------------------------------------------------
TransformReport placeAtoms(core::Document& document,
                           const std::vector<AtomPlacement>& placements,
                           TransformReport* report) {
    TransformReport local;
    for (size_t i = 0; i < placements.size(); ++i) {
        const AtomPlacement& placement = placements[i];
        if (!document.atoms().alive(placement.atom)) { ++local.refsSkippedDead; continue; }
        if (!placement.position.finite()) { ++local.refsSkippedUnsupported; continue; }
        document.setAtomPosition(placement.atom, placement.position);
        ++local.atomsMoved;
    }
    if (report) *report = local;
    return local;
}

// ---------------------------------------------------------------------------
// Layout units
// ---------------------------------------------------------------------------
// Align and distribute must move a whole molecule by ONE delta. Treating every
// selected atom as its own object collapses a ring onto a line; treating the
// whole selection as one object cannot align two molecules. The unit builder
// below is the single answer both operations use.
namespace {

bool layoutRefAlive(const core::Document& document, const core::ObjectRef& ref) {
    switch (ref.kind) {
    case core::ObjectKind::Atom:    return document.atoms().alive(ref.id) != 0;
    case core::ObjectKind::Bond:    return document.bonds().alive(ref.id) != 0;
    case core::ObjectKind::Text:    return document.texts().alive(ref.id) != 0;
    case core::ObjectKind::Path:    return document.paths().alive(ref.id) != 0;
    case core::ObjectKind::Table:   return document.tables().alive(ref.id) != 0;
    case core::ObjectKind::Group:   return document.groups().alive(ref.id) != 0;
    case core::ObjectKind::Arrow:   return document.arrows().alive(ref.id) != 0;
    case core::ObjectKind::Bracket: return document.brackets().alive(ref.id) != 0;
    case core::ObjectKind::Orbital: return document.orbitals().alive(ref.id) != 0;
    case core::ObjectKind::Symbol:  return document.symbols().alive(ref.id) != 0;
    case core::ObjectKind::BioShape:return document.bioShapes().alive(ref.id) != 0;
    default: return false;
    }
}

// Flatten a group into the objects that actually store geometry. A nested group
// is expanded recursively; the path set breaks a corrupt cycle.
void collectGroupLeaves(const core::Document& document, core::ObjectId groupId,
                        uint32_t depth, std::set<core::ObjectId>* path,
                        std::vector<core::ObjectRef>* out) {
    if (!out || depth > 64) return;
    if (!path->insert(groupId).second) return;
    const core::GroupRecord* group = document.groups().alive(groupId);
    if (group) {
        for (size_t i = 0; i < group->members.size(); ++i) {
            const core::ObjectRef& member = group->members[i];
            if (!member.valid()) continue;
            if (member.kind == core::ObjectKind::Group)
                collectGroupLeaves(document, member.id, depth + 1, path, out);
            else if (layoutRefAlive(document, member))
                out->push_back(member);
        }
    }
    path->erase(groupId);
}

struct LayoutUnit {
    std::vector<core::ObjectRef> refs;
    std::vector<core::ObjectRef> boundsRefs;
    std::vector<core::ObjectId>  atoms;
    core::Box box;
    bool fromGroup;
    bool fromBond;
    LayoutUnit() : fromGroup(false), fromBond(false) {}
};

struct UnionFind {
    std::vector<size_t> parent;
    std::vector<size_t> rank;
    explicit UnionFind(size_t n) : parent(n), rank(n, 0) {
        for (size_t i = 0; i < n; ++i) parent[i] = i;
    }
    size_t find(size_t x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    }
    void unite(size_t a, size_t b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (rank[a] < rank[b]) std::swap(a, b);
        parent[b] = a;
        if (rank[a] == rank[b]) ++rank[a];
    }
};

void addAtomId(std::vector<core::ObjectId>* ids, core::ObjectId id) {
    if (!ids || id == core::kInvalidObjectId) return;
    if (std::find(ids->begin(), ids->end(), id) == ids->end()) ids->push_back(id);
}

// Bounds of one layout unit. Text objects are measured by the host's text
// engine when it supplies one: a caption whose box was its anchor point could
// only ever align LEFT, because after a left-align every anchor shares one x
// and a following right/centre-align had nothing left to move. The fallback
// (no provider) stays the documented anchor point.
core::Box layoutUnitBounds(const core::Document& document,
                           const std::vector<core::ObjectRef>& refs,
                           const TextBoundsProvider* textBounds) {
    if (!textBounds) return selectionBounds(document, refs);
    std::vector<core::ObjectRef> pending(refs.rbegin(), refs.rend());
    std::vector<core::ObjectRef> leaves;
    std::set<core::ObjectRef> seen;
    std::set<core::ObjectId> expanded;
    while (!pending.empty()) {
        const core::ObjectRef ref = pending.back();
        pending.pop_back();
        if (!ref.valid() || !seen.insert(ref).second) continue;
        if (ref.kind == core::ObjectKind::Group) {
            const core::GroupRecord* group = document.groups().alive(ref.id);
            if (!group || !expanded.insert(ref.id).second) continue;
            for (size_t i = 0; i < group->members.size(); ++i)
                pending.push_back(group->members[i]);
            continue;
        }
        leaves.push_back(ref);
    }
    core::Box box(core::Vec2(1, 1), core::Vec2(-1, -1));
    bool any = false;
    for (size_t i = 0; i < leaves.size(); ++i) {
        core::Box part;
        bool measured = false;
        if (leaves[i].kind == core::ObjectKind::Text) {
            core::Box ink;
            if (textBounds->textInkBox(document, leaves[i].id, &ink) && !ink.empty() &&
                boxFinite(ink)) {
                part = ink;
                measured = true;
            }
        }
        if (!measured) part = objectBounds(document, leaves[i], BoundsOptions(), 0);
        if (part.empty() || !boxFinite(part)) continue;
        unionBox(&box, part);
        any = true;
    }
    return any ? box : selectionBounds(document, refs);
}

// Split refs into independent layout units. See transform.hpp for the rule.
std::vector<LayoutUnit> buildLayoutUnits(const core::Document& document,
                                         const std::vector<core::ObjectRef>& refs,
                                         const TextBoundsProvider* textBounds) {
    std::vector<LayoutUnit> seeds;
    std::set<core::ObjectRef> seen;
    for (size_t i = 0; i < refs.size(); ++i) {
        const core::ObjectRef& ref = refs[i];
        if (!ref.valid() || !seen.insert(ref).second) continue;
        if (!layoutRefAlive(document, ref)) continue;
        LayoutUnit unit;
        if (ref.kind == core::ObjectKind::Group) {
            std::set<core::ObjectId> path;
            collectGroupLeaves(document, ref.id, 0, &path, &unit.refs);
            if (unit.refs.empty()) continue;
            unit.boundsRefs = unit.refs;
            unit.fromGroup = true;
            for (size_t r = 0; r < unit.refs.size(); ++r) {
                const core::ObjectRef& leaf = unit.refs[r];
                if (leaf.kind == core::ObjectKind::Atom) addAtomId(&unit.atoms, leaf.id);
                else if (leaf.kind == core::ObjectKind::Bond) {
                    const core::BondRecord* bond = document.bonds().alive(leaf.id);
                    if (bond) {
                        addAtomId(&unit.atoms, bond->beginAtom);
                        addAtomId(&unit.atoms, bond->endAtom);
                    }
                }
            }
        } else if (ref.kind == core::ObjectKind::Bond) {
            const core::BondRecord* bond = document.bonds().alive(ref.id);
            if (!bond) continue;
            unit.refs.push_back(ref);
            unit.boundsRefs.push_back(ref);
            unit.fromBond = true;
            addAtomId(&unit.atoms, bond->beginAtom);
            addAtomId(&unit.atoms, bond->endAtom);
        } else {
            unit.refs.push_back(ref);
            unit.boundsRefs.push_back(ref);
            if (ref.kind == core::ObjectKind::Atom) addAtomId(&unit.atoms, ref.id);
        }
        seeds.push_back(unit);
    }
    if (seeds.empty()) return std::vector<LayoutUnit>();

    // Merge units that share an object or an atom. A selected bond and one of
    // its atoms must not move the atom twice.
    UnionFind uf(seeds.size());
    std::map<core::ObjectRef, size_t> ownerByRef;
    std::map<core::ObjectId, size_t> ownerByAtom;
    for (size_t i = 0; i < seeds.size(); ++i) {
        for (size_t r = 0; r < seeds[i].refs.size(); ++r) {
            std::map<core::ObjectRef, size_t>::iterator found =
                ownerByRef.find(seeds[i].refs[r]);
            if (found == ownerByRef.end()) ownerByRef[seeds[i].refs[r]] = i;
            else uf.unite(found->second, i);
        }
        for (size_t a = 0; a < seeds[i].atoms.size(); ++a) {
            std::map<core::ObjectId, size_t>::iterator found =
                ownerByAtom.find(seeds[i].atoms[a]);
            if (found == ownerByAtom.end()) ownerByAtom[seeds[i].atoms[a]] = i;
            else uf.unite(found->second, i);
        }
    }

    // A unit made only of atoms can grow into the whole connected fragment it
    // belongs to. Two selected atoms joined by a document bond are first merged,
    // then the fragment is compared against the selection: a complete fragment
    // is one rigid unit, a partial one stays individually alignable.
    std::vector<bool> atomOnly(seeds.size(), false);
    for (size_t i = 0; i < seeds.size(); ++i) {
        atomOnly[i] = !seeds[i].fromGroup && !seeds[i].fromBond;
        for (size_t r = 0; r < seeds[i].refs.size() && atomOnly[i]; ++r)
            if (seeds[i].refs[r].kind != core::ObjectKind::Atom) atomOnly[i] = false;
    }
    std::map<size_t, bool> rootAtomOnly;
    for (size_t i = 0; i < seeds.size(); ++i) {
        const size_t root = uf.find(i);
        std::map<size_t, bool>::iterator found = rootAtomOnly.find(root);
        if (found == rootAtomOnly.end()) rootAtomOnly[root] = atomOnly[i];
        else found->second = found->second && atomOnly[i];
    }
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    for (size_t b = 0; b < bonds.size(); ++b) {
        if (!bonds[b].isAlive()) continue;
        std::map<core::ObjectId, size_t>::iterator begin = ownerByAtom.find(bonds[b].beginAtom);
        std::map<core::ObjectId, size_t>::iterator end = ownerByAtom.find(bonds[b].endAtom);
        if (begin == ownerByAtom.end() || end == ownerByAtom.end()) continue;
        const size_t ra = uf.find(begin->second), rb = uf.find(end->second);
        if (ra == rb) continue;
        if (rootAtomOnly[ra] && rootAtomOnly[rb]) uf.unite(ra, rb);
    }

    // Connected components of the DOCUMENT, used to decide whether the selected
    // atoms cover a fragment completely. The walk is memoised across all units,
    // so a 10k-atom document is still one pass.
    std::map<core::ObjectId, std::vector<core::ObjectId> > adjacency;
    for (size_t b = 0; b < bonds.size(); ++b) {
        if (!bonds[b].isAlive()) continue;
        adjacency[bonds[b].beginAtom].push_back(bonds[b].endAtom);
        adjacency[bonds[b].endAtom].push_back(bonds[b].beginAtom);
    }
    std::map<core::ObjectId, int> componentOf;
    std::vector<size_t> componentSize;
    for (std::map<core::ObjectId, size_t>::const_iterator it = ownerByAtom.begin();
         it != ownerByAtom.end(); ++it) {
        if (componentOf.count(it->first)) continue;
        const int component = static_cast<int>(componentSize.size());
        size_t count = 0;
        std::vector<core::ObjectId> pending(1, it->first);
        componentOf[it->first] = component;
        while (!pending.empty()) {
            const core::ObjectId atom = pending.back();
            pending.pop_back();
            ++count;
            std::map<core::ObjectId, std::vector<core::ObjectId> >::const_iterator near =
                adjacency.find(atom);
            if (near == adjacency.end()) continue;
            for (size_t n = 0; n < near->second.size(); ++n) {
                const core::ObjectId next = near->second[n];
                if (componentOf.count(next)) continue;
                componentOf[next] = component;
                pending.push_back(next);
            }
        }
        componentSize.push_back(count);
    }

    // Aggregate the union-find roots into the final units.
    std::map<size_t, LayoutUnit> merged;
    for (size_t i = 0; i < seeds.size(); ++i) {
        const size_t root = uf.find(i);
        LayoutUnit& target = merged[root];
        target.refs.insert(target.refs.end(), seeds[i].refs.begin(), seeds[i].refs.end());
        target.boundsRefs.insert(target.boundsRefs.end(), seeds[i].boundsRefs.begin(),
                                 seeds[i].boundsRefs.end());
        target.atoms.insert(target.atoms.end(), seeds[i].atoms.begin(), seeds[i].atoms.end());
        target.fromGroup = target.fromGroup || seeds[i].fromGroup;
        target.fromBond = target.fromBond || seeds[i].fromBond;
    }

    std::vector<LayoutUnit> units;
    for (std::map<size_t, LayoutUnit>::iterator it = merged.begin(); it != merged.end(); ++it) {
        LayoutUnit unit = it->second;
        std::sort(unit.refs.begin(), unit.refs.end());
        unit.refs.erase(std::unique(unit.refs.begin(), unit.refs.end()), unit.refs.end());
        std::sort(unit.boundsRefs.begin(), unit.boundsRefs.end());
        unit.boundsRefs.erase(std::unique(unit.boundsRefs.begin(), unit.boundsRefs.end()),
                              unit.boundsRefs.end());
        std::sort(unit.atoms.begin(), unit.atoms.end());
        unit.atoms.erase(std::unique(unit.atoms.begin(), unit.atoms.end()), unit.atoms.end());

        bool rigid = unit.fromGroup || unit.fromBond;
        if (!rigid) {
            bool onlyAtoms = !unit.refs.empty();
            for (size_t r = 0; r < unit.refs.size(); ++r)
                if (unit.refs[r].kind != core::ObjectKind::Atom) { onlyAtoms = false; break; }
            if (onlyAtoms) {
                bool complete = !unit.atoms.empty();
                int component = -1;
                for (size_t a = 0; a < unit.atoms.size(); ++a) {
                    std::map<core::ObjectId, int>::const_iterator found =
                        componentOf.find(unit.atoms[a]);
                    if (found == componentOf.end()) { complete = false; break; }
                    if (component < 0) component = found->second;
                    else if (component != found->second) { complete = false; break; }
                }
                if (complete && component >= 0 &&
                    componentSize[static_cast<size_t>(component)] == unit.atoms.size())
                    rigid = true;
            } else {
                rigid = true;
            }
        }

        if (rigid) {
            unit.box = layoutUnitBounds(document, unit.boundsRefs, textBounds);
            if (!unit.box.empty()) units.push_back(unit);
            continue;
        }
        // A partial fragment selection: each atom is independently alignable.
        for (size_t a = 0; a < unit.atoms.size(); ++a) {
            LayoutUnit one;
            one.refs.push_back(core::ObjectRef(core::ObjectKind::Atom, unit.atoms[a]));
            one.boundsRefs = one.refs;
            one.atoms.push_back(unit.atoms[a]);
            one.box = layoutUnitBounds(document, one.boundsRefs, textBounds);
            if (!one.box.empty()) units.push_back(one);
        }
    }
    return units;
}

void addTransform(TransformReport* total, const TransformReport& one) {
    if (!total) return;
    total->atomsMoved += one.atomsMoved;
    total->textsMoved += one.textsMoved;
    total->pathsMoved += one.pathsMoved;
    total->annotationsMoved += one.annotationsMoved;
    total->tablesMoved += one.tablesMoved;
    total->groupsTouched += one.groupsTouched;
    total->bondEndpointsMoved += one.bondEndpointsMoved;
    total->bondsUnaffected += one.bondsUnaffected;
    total->textSizesChanged += one.textSizesChanged;
    total->refsSkippedDead += one.refsSkippedDead;
    total->refsSkippedUnsupported += one.refsSkippedUnsupported;
}

const double kLayoutEpsilon = 1e-9;

} // namespace

// ---------------------------------------------------------------------------
// alignObjects
// ---------------------------------------------------------------------------
TransformReport alignObjects(core::Document& document,
                             const std::vector<core::ObjectRef>& refs,
                             AlignMode mode, const AlignOptions& options,
                             TransformReport* report) {
    TransformReport total;
    std::vector<LayoutUnit> units = buildLayoutUnits(document, refs, options.textBounds);
    total.objectsConsidered = units.size();
    if (units.size() < 2) {
        if (report) *report = total;
        return total;
    }

    // Resolve the reference frame from the SAME boxes the units move by, so
    // "align to selection" cannot disagree with the selection rectangle.
    // Start EMPTY. core::Box's default is the valid point (0,0)-(0,0), which
    // would silently drag the reference to the origin.
    core::Box reference(core::Vec2(1, 1), core::Vec2(-1, -1));
    bool haveReference = false;
    size_t anchored = size_t(-1);
    switch (options.reference) {
    case AlignReference::SelectionBounds: {
        for (size_t i = 0; i < units.size(); ++i) unionBox(&reference, units[i].box);
        haveReference = !reference.empty();
        break;
    }
    case AlignReference::Page: {
        if (!options.page.empty()) { reference = options.page; haveReference = true; }
        break;
    }
    case AlignReference::KeyObject: {
        for (size_t i = 0; i < units.size(); ++i) {
            bool contains = false;
            for (size_t r = 0; r < units[i].refs.size() && !contains; ++r)
                if (units[i].refs[r] == options.keyObject) contains = true;
            for (size_t r = 0; r < units[i].boundsRefs.size() && !contains; ++r)
                if (units[i].boundsRefs[r] == options.keyObject) contains = true;
            if (contains) { anchored = i; reference = units[i].box; break; }
        }
        if (anchored == size_t(-1)) {
            reference = objectBounds(document, options.keyObject);
            if (!reference.empty()) haveReference = true;
        } else {
            haveReference = !reference.empty();
        }
        break;
    }
    }
    if (!haveReference) {
        if (report) *report = total;
        return total;
    }

    const core::Vec2 referenceCenter = boxCenter(reference);
    TransformOptions transformOptions;
    transformOptions.textSizePolicy = options.textSizePolicy;
    for (size_t i = 0; i < units.size(); ++i) {
        if (i == anchored) continue;
        const core::Box& box = units[i].box;
        core::Vec2 delta(0, 0);
        switch (mode) {
        case AlignMode::Left:             delta.x = reference.min.x - box.min.x; break;
        case AlignMode::HorizontalCenter: delta.x = referenceCenter.x - boxCenter(box).x; break;
        case AlignMode::Right:            delta.x = reference.max.x - box.max.x; break;
        case AlignMode::Top:              delta.y = reference.min.y - box.min.y; break;
        case AlignMode::VerticalCenter:   delta.y = referenceCenter.y - boxCenter(box).y; break;
        case AlignMode::Bottom:           delta.y = reference.max.y - box.max.y; break;
        }
        if (std::fabs(delta.x) <= kLayoutEpsilon && std::fabs(delta.y) <= kLayoutEpsilon)
            continue;
        const TransformReport one = transformObjects(
            document, units[i].refs, Affine2::translation(delta), transformOptions);
        addTransform(&total, one);
        ++total.objectsMoved;
    }
    if (report) *report = total;
    return total;
}

// ---------------------------------------------------------------------------
// distributeObjects
// ---------------------------------------------------------------------------
DistributeReport distributeObjects(core::Document& document,
                                   const std::vector<core::ObjectRef>& refs,
                                   DistributeMode mode) {
    DistributeOptions options;
    return distributeObjects(document, refs, mode, options);
}

DistributeReport distributeObjects(core::Document& document,
                                   const std::vector<core::ObjectRef>& refs,
                                   DistributeMode mode,
                                   const DistributeOptions& options) {
    DistributeReport report;
    std::vector<LayoutUnit> units = buildLayoutUnits(document, refs, options.textBounds);
    report.objectsConsidered = units.size();
    report.transform.objectsConsidered = units.size();
    if (units.size() < 3) {
        report.applied = false;
        report.why = "分布需要至少三个可独立移动的对象（当前 " +
                     std::to_string(units.size()) + " 个）";
        return report;
    }

    const bool horizontal = (mode == DistributeMode::HorizontalCenters ||
                             mode == DistributeMode::HorizontalGaps);
    const bool gapsMode = (mode == DistributeMode::HorizontalGaps ||
                           mode == DistributeMode::VerticalGaps);

    // Gaps are ordered by leading edge, centres by centre. Ordering gaps by
    // centre mis-sorts overlapping boxes, which is how equal gaps produce
    // visibly unequal gaps.
    std::vector<size_t> order(units.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const double ka = gapsMode
            ? (horizontal ? units[a].box.min.x : units[a].box.min.y)
            : (horizontal ? boxCenter(units[a].box).x : boxCenter(units[a].box).y);
        const double kb = gapsMode
            ? (horizontal ? units[b].box.min.x : units[b].box.min.y)
            : (horizontal ? boxCenter(units[b].box).x : boxCenter(units[b].box).y);
        if (ka != kb) return ka < kb;
        const double ma = gapsMode
            ? (horizontal ? units[a].box.max.x : units[a].box.max.y)
            : (horizontal ? units[a].box.min.x : units[a].box.min.y);
        const double mb = gapsMode
            ? (horizontal ? units[b].box.max.x : units[b].box.max.y)
            : (horizontal ? units[b].box.min.x : units[b].box.min.y);
        if (ma != mb) return ma < mb;
        return units[a].refs.front() < units[b].refs.front();
    });

    TransformOptions transformOptions;
    const size_t first = order.front();
    const size_t last = order.back();
    if (gapsMode) {
        // Keep both extremes fixed and give every gap the same size. Each middle
        // box is placed from the previous box's trailing edge plus that gap.
        double used = 0.0;
        for (size_t i = 0; i < order.size(); ++i)
            used += horizontal ? units[order[i]].box.width() : units[order[i]].box.height();
        const double start = horizontal ? units[first].box.min.x : units[first].box.min.y;
        const double end = horizontal ? units[last].box.max.x : units[last].box.max.y;
        const double gap = (end - start - used) / static_cast<double>(order.size() - 1);
        if (!std::isfinite(gap)) {
            report.applied = false;
            report.why = "所选对象范围无效，无法分布";
            return report;
        }
        double cursor = start;
        for (size_t i = 0; i < order.size(); ++i) {
            const LayoutUnit& unit = units[order[i]];
            const double current = horizontal ? unit.box.min.x : unit.box.min.y;
            const double delta = cursor - current;
            if (i > 0 && i + 1 < order.size() && std::fabs(delta) > kLayoutEpsilon) {
                const core::Vec2 d = horizontal ? core::Vec2(delta, 0.0)
                                                : core::Vec2(0.0, delta);
                const TransformReport one =
                    transformObjects(document, unit.refs, Affine2::translation(d),
                                     transformOptions);
                addTransform(&report.transform, one);
                ++report.objectsMoved;
            }
            cursor += (horizontal ? unit.box.width() : unit.box.height()) + gap;
        }
    } else {
        // Keep the extreme CENTRES fixed; interpolate the middle centres.
        const double start = horizontal ? boxCenter(units[first].box).x
                                        : boxCenter(units[first].box).y;
        const double end = horizontal ? boxCenter(units[last].box).x
                                      : boxCenter(units[last].box).y;
        const double step = (end - start) / static_cast<double>(order.size() - 1);
        if (!std::isfinite(step)) {
            report.applied = false;
            report.why = "所选对象范围无效，无法分布";
            return report;
        }
        for (size_t i = 1; i + 1 < order.size(); ++i) {
            const LayoutUnit& unit = units[order[i]];
            const double desired = start + step * static_cast<double>(i);
            const double current = horizontal ? boxCenter(unit.box).x
                                              : boxCenter(unit.box).y;
            const double delta = desired - current;
            if (std::fabs(delta) <= kLayoutEpsilon) continue;
            const core::Vec2 d = horizontal ? core::Vec2(delta, 0.0)
                                            : core::Vec2(0.0, delta);
            const TransformReport one =
                transformObjects(document, unit.refs, Affine2::translation(d),
                                 transformOptions);
            addTransform(&report.transform, one);
            ++report.objectsMoved;
        }
    }
    report.transform.objectsMoved = report.objectsMoved;
    report.applied = true;
    return report;
}

// ---------------------------------------------------------------------------
// centerSelection
// ---------------------------------------------------------------------------
TransformReport centerSelection(core::Document& document,
                                const std::vector<core::ObjectRef>& refs,
                                const core::Box& page) {
    TransformReport report;
    if (page.empty()) return report;
    const core::Box bounds = selectionBounds(document, refs);
    if (bounds.empty()) return report;
    const core::Vec2 delta = boxCenter(page) - boxCenter(bounds);
    if (delta.x == 0.0 && delta.y == 0.0) return report;
    return transformObjects(document, refs, Affine2::translation(delta), TransformOptions());
}

// ---------------------------------------------------------------------------
// nudgeDelta
// ---------------------------------------------------------------------------
core::Vec2 nudgeDelta(const NudgeVector& direction, bool shift,
                      const NudgeProfile& profile) {
    const double step = shift ? profile.stepShift : profile.stepPlain;
    return core::Vec2(static_cast<double>(direction.dx) * step,
                      static_cast<double>(direction.dy) * step);
}

} // namespace selection
} // namespace r62
