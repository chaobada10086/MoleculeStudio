#include "test_support.hpp"

#include <algorithm>
#include <cstring>

namespace t05 {

// Document::addBond has no order parameter (the order lives on BondRecord), so
// every fixture build goes through this one helper. Written as a helper rather
// than repeated inline so a future signature change is a one-line fix.
inline r62::core::Error addBondWithOrder(r62::core::Document& document,
                                         r62::core::ObjectId begin,
                                         r62::core::ObjectId end,
                                         r62::core::BondOrder order,
                                         r62::core::ObjectId* out) {
    r62::core::BondRecord record;
    record.beginAtom = begin;
    record.endAtom = end;
    record.order = order;
    return document.addBondRecord(record, out);
}

std::string number(double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", v);
    return buffer;
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// ---------------------------------------------------------------------------
// BenzeneFixture
// ---------------------------------------------------------------------------
// A hexagon whose side is exactly the document default bond length, plus a
// caption above the ring and a GROUP containing the ring atoms and the caption.
// The group is what makes "delete the group and undo restores the hierarchy" a
// test rather than a claim.
BenzeneFixture::BenzeneFixture()
    : caption(r62::core::kInvalidObjectId), group(r62::core::kInvalidObjectId) {
    document.setDefaultBondLength(14.4);
    document.setDefaultTextSize(10.0);
    const double radius = document.defaultBondLength();
    const double kPi = 3.14159265358979323846;
    for (int i = 0; i < 6; ++i) {
        const double a = (static_cast<double>(i) / 6.0) * 2.0 * kPi;
        r62::core::ObjectId id = r62::core::kInvalidObjectId;
        document.addAtom(r62::core::Vec2(std::cos(a) * radius, std::sin(a) * radius), 6, &id);
        atoms.push_back(id);
    }
    for (int i = 0; i < 6; ++i) {
        r62::core::ObjectId id = r62::core::kInvalidObjectId;
        addBondWithOrder(document, atoms[static_cast<size_t>(i)],
                         atoms[static_cast<size_t>((i + 1) % 6)],
                         (i % 2 == 0) ? r62::core::BondOrder::Double
                                      : r62::core::BondOrder::Single,
                         &id);
        bonds.push_back(id);
    }
    document.addText(r62::core::Vec2(0.0, -radius - 18.0), 10.0, "benzene", &caption);

    r62::core::GroupRecord groupRecord;
    groupRecord.label = "ring+caption";
    for (size_t i = 0; i < atoms.size(); ++i)
        groupRecord.members.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, atoms[i]));
    for (size_t i = 0; i < bonds.size(); ++i)
        groupRecord.members.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Bond, bonds[i]));
    groupRecord.members.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, caption));
    document.addGroup(groupRecord, &group);
    document.rebuildDerived();
}

r62::core::Vec2 BenzeneFixture::centre() const {
    r62::core::Box box;
    bool first = true;
    for (size_t i = 0; i < atoms.size(); ++i) {
        const r62::core::AtomRecord* atom = document.atoms().get(atoms[i]);
        if (!atom) continue;
        if (first) { box = r62::core::Box(atom->position, atom->position); first = false; }
        else box.add(atom->position);
    }
    return first ? r62::core::Vec2(0, 0) : boxCenter(box);
}

// ---------------------------------------------------------------------------
// GridFixture
// ---------------------------------------------------------------------------
GridFixture::GridFixture(size_t count, bool connectChain) {
    document.setDefaultBondLength(14.4);
    atoms.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        // 250 columns, 30 units apart - identical to t04 BvhSink::buildGrid.
        const double x = static_cast<double>(i % 250) * 30.0;
        const double y = static_cast<double>(i / 250) * 30.0;
        r62::core::ObjectId id = r62::core::kInvalidObjectId;
        document.addAtom(r62::core::Vec2(x, y), 6, &id);
        atoms.push_back(id);
    }
    if (connectChain) {
        for (size_t i = 0; i + 1 < atoms.size(); ++i) {
            r62::core::ObjectId id = r62::core::kInvalidObjectId;
            // A long chain rather than a full grid mesh: it is the shape that
            // makes the connected-molecule walk's cost visible (a 50,000-long
            // chain is a 50,000-step traversal).
            addBondWithOrder(document, atoms[i], atoms[i + 1], r62::core::BondOrder::Single, &id);
        }
    }
    document.rebuildDerived();
}

// ---------------------------------------------------------------------------
// MixedFixture
// ---------------------------------------------------------------------------
// One of EVERY kind T05 transforms. A transform that skips a kind is caught by
// the per-kind counters rather than by a screenshot.
MixedFixture::MixedFixture() {
    document.setDefaultBondLength(14.4);
    document.setDefaultTextSize(10.0);
    r62::core::ObjectId a = r62::core::kInvalidObjectId, b = r62::core::kInvalidObjectId;
    document.addAtom(r62::core::Vec2(0, 0), 6, &a);
    document.addAtom(r62::core::Vec2(14.4, 0), 8, &b);
    atom = r62::core::ObjectRef(r62::core::ObjectKind::Atom, a);
    r62::core::ObjectId bond = r62::core::kInvalidObjectId;
    addBondWithOrder(document, a, b, r62::core::BondOrder::Double, &bond);
    this->bond = r62::core::ObjectRef(r62::core::ObjectKind::Bond, bond);

    r62::core::ObjectId text = r62::core::kInvalidObjectId;
    document.addText(r62::core::Vec2(0, 30), 10.0, "caption", &text);
    this->text = r62::core::ObjectRef(r62::core::ObjectKind::Text, text);

    r62::core::PathRecord path;
    path.points.push_back(r62::core::Vec2(0, 40));
    path.points.push_back(r62::core::Vec2(10, 50));
    path.points.push_back(r62::core::Vec2(20, 40));
    path.pointKind.assign(3, 0);
    r62::core::ObjectId pathId = r62::core::kInvalidObjectId;
    document.addPath(path, &pathId);
    this->path = r62::core::ObjectRef(r62::core::ObjectKind::Path, pathId);

    r62::core::AnnotationRecord arrow;
    arrow.kind = r62::core::ObjectKind::Arrow;
    arrow.presetId = 1;
    arrow.geometry[0] = r62::core::Vec2(30, 0);
    arrow.geometry[1] = r62::core::Vec2(50, 20);
    r62::core::ObjectId arrowId = r62::core::kInvalidObjectId;
    document.addAnnotation(r62::core::ObjectKind::Arrow, arrow, &arrowId);
    this->arrow = r62::core::ObjectRef(r62::core::ObjectKind::Arrow, arrowId);

    r62::core::TableRecord table;
    table.frame = r62::core::Box(r62::core::Vec2(60, 0), r62::core::Vec2(100, 40));
    table.rows = 2;
    table.columns = 2;
    table.rowHeight = 20.0;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            r62::core::ObjectId cell = r62::core::kInvalidObjectId;
            document.addText(r62::core::Vec2(62.0 + c * 20.0, 15.0 + r * 20.0), 9.0, "x", &cell);
            table.cells.push_back(cell);
            tableCells.push_back(cell);
        }
    }
    r62::core::ObjectId tableId = r62::core::kInvalidObjectId;
    document.addTable(table, &tableId);
    this->table = r62::core::ObjectRef(r62::core::ObjectKind::Table, tableId);

    r62::core::GroupRecord groupRecord;
    groupRecord.label = "mixed";
    groupRecord.members.push_back(atom);
    groupRecord.members.push_back(this->bond);
    groupRecord.members.push_back(this->text);
    r62::core::ObjectId groupId = r62::core::kInvalidObjectId;
    document.addGroup(groupRecord, &groupId);
    this->group = r62::core::ObjectRef(r62::core::ObjectKind::Group, groupId);

    all.push_back(atom);
    all.push_back(this->bond);
    all.push_back(this->text);
    all.push_back(this->path);
    all.push_back(this->arrow);
    all.push_back(this->table);
    all.push_back(this->group);
    document.rebuildDerived();
}

// ---------------------------------------------------------------------------
// GridSelectionQuery
// ---------------------------------------------------------------------------
GridSelectionQuery::GridSelectionQuery(const r62::core::Document& document, double cellSize)
    : document_(document), cellSize_(cellSize > 0.0 ? cellSize : 1.0), serial_(1),
      cellsVisited_(0) {
    build();
}

void GridSelectionQuery::build() {
    refs_.clear();
    boxes_.clear();
    cells_.clear();
    // The same full walk the reference uses, so the two agree by construction on
    // WHICH objects exist; only the query strategy differs.
    DocumentSelectionQuery reference(document_);
    refs_ = reference.allObjects();
    boxes_.reserve(refs_.size());
    for (size_t i = 0; i < refs_.size(); ++i) {
        const r62::core::Box box = objectBounds(document_, refs_[i]);
        boxes_.push_back(box);
        if (box.empty()) continue;
        long x0, y0, x1, y1;
        cellRange(box, &x0, &y0, &x1, &y1);
        for (long x = x0; x <= x1; ++x)
            for (long y = y0; y <= y1; ++y)
                cells_[std::make_pair(x, y)].push_back(i);
    }
}

void GridSelectionQuery::cellRange(const r62::core::Box& box, long* x0, long* y0,
                                   long* x1, long* y1) const {
    *x0 = static_cast<long>(std::floor(box.min.x / cellSize_));
    *y0 = static_cast<long>(std::floor(box.min.y / cellSize_));
    *x1 = static_cast<long>(std::floor(box.max.x / cellSize_));
    *y1 = static_cast<long>(std::floor(box.max.y / cellSize_));
}

std::vector<r62::core::ObjectRef> GridSelectionQuery::objectsInBox(const r62::core::Box& box) const {
    std::vector<r62::core::ObjectRef> result;
    if (box.empty()) return result;
    long x0, y0, x1, y1;
    cellRange(box, &x0, &y0, &x1, &y1);
    std::vector<size_t> seen;
    for (long x = x0; x <= x1; ++x) {
        for (long y = y0; y <= y1; ++y) {
            ++cellsVisited_;
            const std::map<std::pair<long, long>, std::vector<size_t> >::const_iterator it =
                cells_.find(std::make_pair(x, y));
            if (it == cells_.end()) continue;
            for (size_t k = 0; k < it->second.size(); ++k) {
                const size_t index = it->second[k];
                if (boxes_[index].empty()) continue;
                if (!boxIntersectsBox(box, boxes_[index])) continue;
                seen.push_back(index);
            }
        }
    }
    // The CELL approach can return an object more than once (it spans several
    // cells). Deduplicate and sort so the result is directly comparable with the
    // reference's ordering.
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    for (size_t i = 0; i < seen.size(); ++i) result.push_back(refs_[seen[i]]);
    return result;
}

std::vector<r62::core::ObjectRef> GridSelectionQuery::objectsInPolygon(
        const std::vector<r62::core::Vec2>& points, bool containedOnly) const {
    std::vector<r62::core::ObjectRef> result;
    if (points.empty()) return result;
    r62::core::Box hull = r62::core::Box(points[0], points[0]);
    for (size_t i = 1; i < points.size(); ++i) hull.add(points[i]);
    const std::vector<r62::core::ObjectRef> candidates = objectsInBox(hull);
    for (size_t i = 0; i < candidates.size(); ++i) {
        const r62::core::Box box = objectBounds(document_, candidates[i]);
        if (box.empty()) continue;
        if (containedOnly ? polygonContainsBox(points, box) : polygonIntersectsBox(points, box))
            result.push_back(candidates[i]);
    }
    return result;
}

std::vector<r62::core::ObjectRef> GridSelectionQuery::allObjects() const { return refs_; }

r62::core::Box GridSelectionQuery::objectBoundsForQuery(const r62::core::ObjectRef& ref) const {
    return objectBounds(document_, ref);
}

// ---------------------------------------------------------------------------
// Delete bridges
// ---------------------------------------------------------------------------
r62::core::Error countingDeleteFn(void* userData,
                                  const std::vector<r62::core::ObjectRef>& refs) {
    DeleteLog* log = static_cast<DeleteLog*>(userData);
    if (!log) return r62::core::Error(r62::core::ErrorCode::InvalidArgument, "no log");
    ++log->calls;
    log->lastRefs = refs;
    if (!log->forcedError.ok()) return log->forcedError;
    return r62::core::Error::success();
}

r62::core::Error logDeleteFn(void* userData, const std::vector<r62::core::ObjectRef>& refs) {
    LogDeleteBridge* bridge = static_cast<LogDeleteBridge*>(userData);
    if (!bridge || !bridge->log || !bridge->document)
        return r62::core::Error(r62::core::ErrorCode::InvalidArgument, "bridge not wired");
    const r62::core::Command command = r62::core::commands::deleteObjects(refs);
    return bridge->log->execute(*bridge->document, command);
}

// ---------------------------------------------------------------------------
// Independent document signature
// ---------------------------------------------------------------------------
std::string independentDocumentSignature(const r62::core::Document& document) {
    // Text, not a hash: a mismatch can be DIFFED by eye, which a hash cannot.
    // Deliberately does not call Document::digest().
    std::ostringstream out;
    const std::vector<r62::core::AtomRecord>& atoms = document.atoms().records();
    out << "A" << atoms.size();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) { out << "/d"; continue; }
        out << "/" << atoms[i].id << ":" << atoms[i].element
            << "@" << number(atoms[i].position.x) << "," << number(atoms[i].position.y)
            << "~" << static_cast<int>(atoms[i].semantics.charge)
            << "," << static_cast<int>(atoms[i].semantics.radical)
            << "," << atoms[i].semantics.aromatic;
    }
    const std::vector<r62::core::BondRecord>& bonds = document.bonds().records();
    out << "|B" << bonds.size();
    for (size_t i = 0; i < bonds.size(); ++i) {
        if (!bonds[i].isAlive()) { out << "/d"; continue; }
        out << "/" << bonds[i].id << ":" << bonds[i].beginAtom << "-" << bonds[i].endAtom
            << "o" << static_cast<int>(bonds[i].order)
            << "d" << static_cast<int>(bonds[i].display)
            << "s" << static_cast<int>(bonds[i].semantics)
            << "t" << static_cast<int>(bonds[i].stereo);
    }
    const std::vector<r62::core::TextRecord>& texts = document.texts().records();
    out << "|T" << texts.size();
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) { out << "/d"; continue; }
        out << "/" << texts[i].id << "@" << number(texts[i].position.x) << ","
            << number(texts[i].position.y) << "z" << number(texts[i].size)
            << "'" << document.plainText(texts[i].id) << "'";
    }
    const std::vector<r62::core::PathRecord>& paths = document.paths().records();
    out << "|P" << paths.size();
    for (size_t i = 0; i < paths.size(); ++i) {
        if (!paths[i].isAlive()) { out << "/d"; continue; }
        out << "/" << paths[i].id << "[";
        for (size_t k = 0; k < paths[i].points.size(); ++k)
            out << number(paths[i].points[k].x) << "," << number(paths[i].points[k].y) << ";";
        out << "]";
    }
    const r62::core::ObjectKind annotationKinds[5] = {
        r62::core::ObjectKind::Arrow, r62::core::ObjectKind::Bracket,
        r62::core::ObjectKind::Orbital, r62::core::ObjectKind::Symbol,
        r62::core::ObjectKind::BioShape
    };
    const r62::core::ObjectSlab<r62::core::AnnotationRecord>* slabs[5] = {
        &document.arrows(), &document.brackets(), &document.orbitals(),
        &document.symbols(), &document.bioShapes()
    };
    for (int s = 0; s < 5; ++s) {
        const std::vector<r62::core::AnnotationRecord>& records = slabs[s]->records();
        out << "|" << r62::core::kindName(annotationKinds[s]) << records.size();
        for (size_t i = 0; i < records.size(); ++i) {
            if (!records[i].isAlive()) { out << "/d"; continue; }
            out << "/" << records[i].id << "p" << records[i].presetId << "[";
            for (int g = 0; g < 4; ++g)
                out << number(records[i].geometry[g].x) << "," << number(records[i].geometry[g].y) << ";";
            out << "]r" << number(records[i].rotation) << "s" << number(records[i].scale);
        }
    }
    const std::vector<r62::core::TableRecord>& tables = document.tables().records();
    out << "|TB" << tables.size();
    for (size_t i = 0; i < tables.size(); ++i) {
        if (!tables[i].isAlive()) { out << "/d"; continue; }
        out << "/" << tables[i].id << "box" << number(tables[i].frame.min.x) << ","
            << number(tables[i].frame.min.y) << "-" << number(tables[i].frame.max.x) << ","
            << number(tables[i].frame.max.y) << "cells" << tables[i].cells.size();
    }
    const std::vector<r62::core::GroupRecord>& groups = document.groups().records();
    out << "|G" << groups.size();
    for (size_t i = 0; i < groups.size(); ++i) {
        if (!groups[i].isAlive()) { out << "/d"; continue; }
        out << "/" << groups[i].id << "z" << groups[i].zIndex << "'" << groups[i].label << "'[";
        for (size_t m = 0; m < groups[i].members.size(); ++m)
            out << static_cast<int>(groups[i].members[m].kind) << ":"
                << groups[i].members[m].id << ";";
        out << "]";
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// summarise
// ---------------------------------------------------------------------------
SampleStats summarise(std::vector<double> samples) {
    SampleStats stats;
    if (samples.empty()) return stats;
    std::sort(samples.begin(), samples.end());
    stats.count = samples.size();
    stats.min = samples.front();
    stats.max = samples.back();
    stats.median = samples[samples.size() / 2];
    const size_t index = samples.size() >= 20
        ? (samples.size() * 95) / 100 : samples.size() - 1;
    stats.p95 = samples[index < samples.size() ? index : samples.size() - 1];
    return stats;
}

} // namespace t05
