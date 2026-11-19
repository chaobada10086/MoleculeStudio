#include "next/selection/selection_set.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace r62 {
namespace selection {

const char* selectionReasonName(SelectionReason reason) {
    switch (reason) {
    case SelectionReason::Direct:      return "direct";
    case SelectionReason::Dependency:  return "dependency";
    case SelectionReason::Molecule:    return "molecule";
    case SelectionReason::GroupMember: return "group-member";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// SelectionSet
// ---------------------------------------------------------------------------
bool SelectionSet::contains(const core::ObjectRef& ref) const {
    // refs_ is sorted, so this is a binary search rather than a linear scan -
    // which matters because a 50,000-object set is asked this per hotkey.
    return std::binary_search(refs_.begin(), refs_.end(), ref);
}

void SelectionSet::clear() {
    refs_.clear();
    reasons_.clear();
}

void SelectionSet::insertOrdered(const core::ObjectRef& ref, SelectionReason reason) {
    const std::vector<core::ObjectRef>::iterator it =
        std::lower_bound(refs_.begin(), refs_.end(), ref);
    if (it != refs_.end() && *it == ref) return;
    const size_t index = static_cast<size_t>(it - refs_.begin());
    refs_.insert(it, ref);
    reasons_.insert(reasons_.begin() + static_cast<std::ptrdiff_t>(index), reason);
}

bool SelectionSet::add(const core::ObjectRef& ref, SelectionReason reason) {
    if (!ref.valid()) return false;
    if (contains(ref)) return false;
    insertOrdered(ref, reason);
    return true;
}

bool SelectionSet::remove(const core::ObjectRef& ref) {
    const std::vector<core::ObjectRef>::iterator it =
        std::lower_bound(refs_.begin(), refs_.end(), ref);
    if (it == refs_.end() || !(*it == ref)) return false;
    const size_t index = static_cast<size_t>(it - refs_.begin());
    refs_.erase(it);
    reasons_.erase(reasons_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool SelectionSet::toggle(const core::ObjectRef& ref, SelectionReason reason) {
    if (contains(ref)) { remove(ref); return true; }
    return add(ref, reason);
}

size_t SelectionSet::addAll(const std::vector<core::ObjectRef>& refs, SelectionReason reason) {
    if (refs.empty()) return 0;
    const size_t before = refs_.size();
    std::vector<core::ObjectRef> merged;
    merged.reserve(refs_.size() + refs.size());
    // Merge instead of N ordered inserts: a 50,000-object Select All then costs
    // one sort and one merge, not 50,000 memmoves.
    std::vector<core::ObjectRef> incoming(refs);
    std::sort(incoming.begin(), incoming.end());
    incoming.erase(std::unique(incoming.begin(), incoming.end()), incoming.end());
    for (size_t i = 0; i < incoming.size(); ++i)
        if (!incoming[i].valid()) continue;
    merged.insert(merged.end(), refs_.begin(), refs_.end());
    std::vector<core::ObjectRef> added;
    for (size_t i = 0; i < incoming.size(); ++i) {
        if (!incoming[i].valid()) continue;
        if (!contains(incoming[i])) added.push_back(incoming[i]);
    }
    if (added.empty()) return 0;
    merged.insert(merged.end(), added.begin(), added.end());
    std::sort(merged.begin(), merged.end());
    // Rebuild reasons by walking the OLD parallel pair and defaulting the new
    // ones. Cheaper than an ordered insert per element.
    std::vector<SelectionReason> newReasons(merged.size(), reason);
    for (size_t i = 0; i < refs_.size(); ++i) {
        const std::vector<core::ObjectRef>::iterator it =
            std::lower_bound(merged.begin(), merged.end(), refs_[i]);
        if (it == merged.end() || !(*it == refs_[i])) continue;
        newReasons[static_cast<size_t>(it - merged.begin())] = reasons_[i];
    }
    refs_.swap(merged);
    reasons_.swap(newReasons);
    return refs_.size() - before;
}

size_t SelectionSet::removeAll(const std::vector<core::ObjectRef>& refs) {
    if (refs.empty() || refs_.empty()) return 0;
    std::vector<core::ObjectRef> drop(refs);
    std::sort(drop.begin(), drop.end());
    drop.erase(std::unique(drop.begin(), drop.end()), drop.end());
    std::vector<core::ObjectRef> keptRefs;
    std::vector<SelectionReason> keptReasons;
    keptRefs.reserve(refs_.size());
    size_t removed = 0;
    for (size_t i = 0; i < refs_.size(); ++i) {
        if (std::binary_search(drop.begin(), drop.end(), refs_[i])) { ++removed; continue; }
        keptRefs.push_back(refs_[i]);
        keptReasons.push_back(reasons_[i]);
    }
    refs_.swap(keptRefs);
    reasons_.swap(keptReasons);
    return removed;
}

void SelectionSet::setRefs(const std::vector<core::ObjectRef>& refs, SelectionReason reason) {
    refs_.clear();
    reasons_.clear();
    refs_.reserve(refs.size());
    for (size_t i = 0; i < refs.size(); ++i)
        if (refs[i].valid()) refs_.push_back(refs[i]);
    std::sort(refs_.begin(), refs_.end());
    refs_.erase(std::unique(refs_.begin(), refs_.end()), refs_.end());
    reasons_.assign(refs_.size(), reason);
}

size_t SelectionSet::pruneDead(const core::Document& document) {
    std::vector<core::ObjectRef> keptRefs;
    std::vector<SelectionReason> keptReasons;
    keptRefs.reserve(refs_.size());
    for (size_t i = 0; i < refs_.size(); ++i) {
        bool alive = false;
        switch (refs_[i].kind) {
        case core::ObjectKind::Atom:    alive = document.atoms().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Bond:    alive = document.bonds().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Text:    alive = document.texts().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Path:    alive = document.paths().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Table:   alive = document.tables().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Group:   alive = document.groups().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Arrow:   alive = document.arrows().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Bracket: alive = document.brackets().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Orbital: alive = document.orbitals().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::Symbol:  alive = document.symbols().alive(refs_[i].id) != 0; break;
        case core::ObjectKind::BioShape:alive = document.bioShapes().alive(refs_[i].id) != 0; break;
        default: alive = false; break;
        }
        if (!alive) continue;
        keptRefs.push_back(refs_[i]);
        keptReasons.push_back(reasons_[i]);
    }
    const size_t removed = refs_.size() - keptRefs.size();
    refs_.swap(keptRefs);
    reasons_.swap(keptReasons);
    return removed;
}

uint64_t SelectionSet::digest() const {
    // FNV-1a over (kind, id) pairs. Order-independent would be nicer but the
    // ordering IS part of the contract, so an order change must change the
    // digest and be caught.
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < refs_.size(); ++i) {
        const uint64_t v = (static_cast<uint64_t>(refs_[i].kind) << 32) | refs_[i].id;
        hash ^= v;
        hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(reasons_[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// SelectionDelta
// ---------------------------------------------------------------------------
std::string SelectionDelta::summary() const {
    std::ostringstream out;
    out << "entered=" << entered.size() << " left=" << left.size()
        << " added=" << addedCount << " removed=" << removedCount
        << " scanned=" << scanned << " batch=" << batchSize();
    return out.str();
}

SelectionDelta diffSelection(const SelectionSet& before, const SelectionSet& after,
                             uint64_t scanned) {
    SelectionDelta delta;
    delta.scanned = scanned;
    for (size_t i = 0; i < after.size(); ++i)
        if (!before.contains(after.refs()[i])) delta.entered.push_back(after.refs()[i]);
    for (size_t i = 0; i < before.size(); ++i)
        if (!after.contains(before.refs()[i])) delta.left.push_back(before.refs()[i]);
    delta.addedCount = delta.entered.size();
    delta.removedCount = delta.left.size();
    return delta;
}

// ---------------------------------------------------------------------------
// Geometry predicates
// ---------------------------------------------------------------------------
bool boxContainsPoint(const core::Box& box, const core::Vec2& point) {
    if (box.empty()) return false;
    return box.contains(point);
}

bool boxContainsBox(const core::Box& outer, const core::Box& inner) {
    if (outer.empty() || inner.empty()) return false;
    return inner.min.x >= outer.min.x && inner.max.x <= outer.max.x &&
           inner.min.y >= outer.min.y && inner.max.y <= outer.max.y;
}

bool boxIntersectsBox(const core::Box& a, const core::Box& b) {
    core::Box tmp;
    return intersectBox(a, b, &tmp);
}

bool polygonContainsPoint(const std::vector<core::Vec2>& polygon, const core::Vec2& point) {
    if (polygon.size() < 3) {
        // A degenerate polygon (fewer than three points) is a point or a segment.
        // Treat it as a zero-area region that still contains its own vertices,
        // so a two-click lasso is not silently empty.
        for (size_t i = 0; i < polygon.size(); ++i)
            if (polygon[i] == point) return true;
        return false;
    }
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const core::Vec2& a = polygon[i];
        const core::Vec2& b = polygon[j];
        // Boundary rule: a point exactly on a horizontal edge or a vertex counts
        // as inside. Excluding the boundary is what makes edge atoms of a lasso
        // unselectable on a pixel-aligned grid.
        if (a.y == point.y && a.x == point.x) return true;
        if ((a.y > point.y) != (b.y > point.y)) {
            const double t = (point.y - a.y) / (b.y - a.y);
            const double xCross = a.x + t * (b.x - a.x);
            if (xCross == point.x) return true;      // on the edge
            if (point.x < xCross) inside = !inside;
        }
    }
    return inside;
}

bool polygonContainsBox(const std::vector<core::Vec2>& polygon, const core::Box& box) {
    if (polygon.empty() || box.empty()) return false;
    const core::Vec2 corners[4] = {
        box.min, core::Vec2(box.max.x, box.min.y), box.max, core::Vec2(box.min.x, box.max.y)
    };
    for (int i = 0; i < 4; ++i)
        if (!polygonContainsPoint(polygon, corners[i])) return false;
    return true;
}

bool polygonIntersectsBox(const std::vector<core::Vec2>& polygon, const core::Box& box) {
    if (polygon.empty() || box.empty()) return false;
    // Any vertex inside the box -> intersect.
    for (size_t i = 0; i < polygon.size(); ++i)
        if (box.contains(polygon[i])) return true;
    // Any box corner inside the polygon -> intersect.
    const core::Vec2 corners[4] = {
        box.min, core::Vec2(box.max.x, box.min.y), box.max, core::Vec2(box.min.x, box.max.y)
    };
    for (int i = 0; i < 4; ++i)
        if (polygonContainsPoint(polygon, corners[i])) return true;
    // Any polygon EDGE crossing any box EDGE -> intersect. Without this an X
    // shaped lasso whose arms pass through the box misses it entirely.
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const core::Vec2& p0 = polygon[j];
        const core::Vec2& p1 = polygon[i];
        for (int e = 0; e < 4; ++e) {
            const core::Vec2& q0 = corners[e];
            const core::Vec2& q1 = corners[(e + 1) % 4];
            const double d0x = p1.x - p0.x, d0y = p1.y - p0.y;
            const double d1x = q1.x - q0.x, d1y = q1.y - q0.y;
            const double denom = d0x * d1y - d0y * d1x;
            if (denom == 0.0) continue;   // parallel
            const double t = ((q0.x - p0.x) * d1y - (q0.y - p0.y) * d1x) / denom;
            const double u = ((q0.x - p0.x) * d0y - (q0.y - p0.y) * d0x) / denom;
            if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// MarqueeGesture
// ---------------------------------------------------------------------------
void MarqueeGesture::reset() {
    shape = MarqueeShape::Rect;
    anchor = core::Vec2(0, 0);
    current = core::Vec2(0, 0);
    lasso.clear();
    samples = 0;
}

bool MarqueeGesture::addLassoPoint(const core::Vec2& point, double minDistance) {
    if (!point.finite()) return false;
    if (!lasso.empty() && length(point - lasso.back()) < minDistance) return false;
    lasso.push_back(point);
    current = point;
    ++samples;
    return true;
}

// ---------------------------------------------------------------------------
// queryMarquee
// ---------------------------------------------------------------------------
std::vector<core::ObjectRef> queryMarquee(const SelectionQuery& query,
                                          const MarqueeGesture& gesture,
                                          const MarqueeOptions& options,
                                          uint64_t* scanned) {
    std::vector<core::ObjectRef> result;
    const core::Box marquee(gesture.anchor, gesture.current);
    const core::Box padded = inflateBox(marquee, options.containmentSlack);

    MarqueeMode mode = options.mode;
    if (options.deriveModeFromDirection) {
        mode = gesture.current.x >= gesture.anchor.x ? MarqueeMode::Contained
                                                     : MarqueeMode::Intersecting;
    }

    if (gesture.shape == MarqueeShape::Lasso) {
        std::vector<core::Vec2> path(gesture.lasso);
        if (path.size() < 2) path.push_back(gesture.current);
        const bool containedOnly = (mode == MarqueeMode::Contained);
        std::vector<core::ObjectRef> candidates = query.objectsInPolygon(path, containedOnly);
        if (scanned) *scanned += candidates.size();
        // The slack is applied to the OBJECT box, not the polygon: growing the
        // polygon would change which side of a boundary an object falls on.
        for (size_t i = 0; i < candidates.size(); ++i) {
            const core::ObjectRef& ref = candidates[i];
            const bool ok = containedOnly
                ? polygonContainsBox(path, inflateBox(query.objectBoundsForQuery(ref),
                                                      options.containmentSlack))
                : true;   // objectsInPolygon already answered the intersection case
            if (ok) result.push_back(ref);
        }
        return result;
    }

    // Rectangular marquee.
    const core::Box queryBox = (mode == MarqueeMode::Contained) ? padded : marquee;
    std::vector<core::ObjectRef> candidates = query.objectsInBox(queryBox);
    if (scanned) *scanned += candidates.size();
    if (mode == MarqueeMode::Intersecting) return candidates;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const core::Box box = query.objectBoundsForQuery(candidates[i]);
        if (box.empty()) continue;
        if (boxContainsBox(padded, box)) result.push_back(candidates[i]);
    }
    return result;
}

} // namespace selection
} // namespace r62
