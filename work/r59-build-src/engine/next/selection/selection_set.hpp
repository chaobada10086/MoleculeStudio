#ifndef R62_NEXT_SELECTION_SELECTION_SET_HPP
#define R62_NEXT_SELECTION_SELECTION_SET_HPP
// =============================================================================
// re_round62_viewer/next/selection/selection_set.hpp
//
// T05 - THE SELECTION SET ITSELF: WHAT IS SELECTED, HOW A GESTURE CHANGES IT,
//       AND WHAT CHANGES ABOUT IT INCREMENTALLY.
//
// WHY SELECTION IS NOT DOCUMENT STATE
// -----------------------------------------------------------------------------
// The selection lives OUTSIDE core::Document on purpose (T01 states it, T04
// relies on it): saving must not persist it and undo must not restore it. So
// this module owns a SelectionSet value that the host holds, and every operation
// here is a pure function from (document, set, input) to a new set plus a
// CHANGE RECORD.
//
// WHY THERE IS A CHANGE RECORD AND NOT JUST "the vector got shorter"
// -----------------------------------------------------------------------------
// THE PERFORMANCE REQUIREMENT IS LITERAL: "highlight提交批量，不逐个对象单独调用
// 绘制" - the highlight must be submitted in BATCHES, not one draw call per
// object. A host that re-uploads 50,000 highlight primitives because one atom
// was added to the selection is the defect this module exists to prevent. So
// every mutation returns a SelectionDelta listing exactly the refs that entered
// and left, and the overlay builder emits only those.
//
// A NOTE ON "DO NOT TREAT EVERY OBJECT AS AN ATOM"
// -----------------------------------------------------------------------------
// The legacy editor's selection was a list of atom ids. Here a selection element
// is an ObjectRef (kind + id), because a copied ring carries its TEXT and a
// marquee over mixed content must catch bonds, arrows, tables and groups too.
// Anywhere this module needs atom-only semantics (connected molecule walk) it
// says so explicitly and filters by kind.
// =============================================================================

#include "next/selection/bounds.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// SelectionReason: why an object is in the set
// ---------------------------------------------------------------------------
// Needed because "delete a group" and "delete the group's members" are
// different results, and because an undo that restores a group must restore the
// MEMBERS' reason too. A set of bare refs loses that distinction.
enum class SelectionReason : uint8_t {
    Direct = 0,       // the user pointed at (or marqueed) this object
    Dependency = 1,   // pulled in because a direct member references it
    Molecule = 2,     // pulled in by the connected-molecule walk
    GroupMember = 3   // pulled in because its group is selected
};

const char* selectionReasonName(SelectionReason reason);

// ---------------------------------------------------------------------------
// SelectionSet
// ---------------------------------------------------------------------------
// Ordered by (kind, id) so a comparison is a single vector compare and the
// overlay emits primitives in a deterministic order (a hash-set order would make
// screenshots differ run to run for no reason).
class SelectionSet {
public:
    SelectionSet() {}

    size_t size() const { return refs_.size(); }
    bool empty() const { return refs_.empty(); }
    const std::vector<core::ObjectRef>& refs() const { return refs_; }
    const std::vector<SelectionReason>& reasons() const { return reasons_; }
    SelectionReason reasonAt(size_t index) const {
        return index < reasons_.size() ? reasons_[index] : SelectionReason::Direct;
    }
    bool contains(const core::ObjectRef& ref) const;
    void clear();

    // Add if absent. Returns true when the set actually changed. `reason` is
    // only stored for a NEW member; re-adding an existing one never downgrades a
    // Direct member to a Dependency.
    bool add(const core::ObjectRef& ref, SelectionReason reason = SelectionReason::Direct);
    bool remove(const core::ObjectRef& ref);
    // Add when present, remove when absent. The Shift-click / Ctrl-click toggle.
    bool toggle(const core::ObjectRef& ref, SelectionReason reason = SelectionReason::Direct);

    // Bulk add/remove that keep the ordering property. Used by marquee, Select
    // All and undo restore, so a 50,000-object selection is built by ONE sort
    // rather than 50,000 ordered insertions.
    size_t addAll(const std::vector<core::ObjectRef>& refs,
                  SelectionReason reason = SelectionReason::Direct);
    size_t removeAll(const std::vector<core::ObjectRef>& refs);
    void setRefs(const std::vector<core::ObjectRef>& refs,
                 SelectionReason reason = SelectionReason::Direct);

    // Remove dead refs. Called after a delete or an undo so a selection can
    // never outlive its document slots. Returns how many were dropped.
    size_t pruneDead(const core::Document& document);

    // Stable digest for the "undo restored the exact selection" assertion.
    uint64_t digest() const;

    bool operator==(const SelectionSet& other) const {
        return refs_ == other.refs_ && reasons_ == other.reasons_;
    }

private:
    // Insert one ref keeping refs_ and reasons_ parallel and ordered.
    void insertOrdered(const core::ObjectRef& ref, SelectionReason reason);

    std::vector<core::ObjectRef>   refs_;
    std::vector<SelectionReason>   reasons_;
};

// ---------------------------------------------------------------------------
// SelectionDelta: the incremental overlay evidence
// ---------------------------------------------------------------------------
// entered/left are exactly the objects whose highlight state changed. The host
// passes them to the overlay builder; a batch submission of 0 items for an
// unrelated click is the point.
struct SelectionDelta {
    std::vector<core::ObjectRef> entered;
    std::vector<core::ObjectRef> left;
    uint64_t addedCount;
    uint64_t removedCount;
    // How many objects the operation SCANNED. A marquee over 50,000 objects
    // scans 50,000; selecting a molecule scans its component. The ratio is what
    // makes "the marquee is not O(N) per frame" a measurement.
    uint64_t scanned;
    SelectionDelta() : addedCount(0), removedCount(0), scanned(0) {}
    bool empty() const { return entered.empty() && left.empty(); }
    size_t batchSize() const { return entered.size() + left.size(); }
    std::string summary() const;
};

// Apply "set becomes target" and record the difference. This is THE one place a
// delta is produced, so every operation below inherits the same delta semantics.
SelectionDelta diffSelection(const SelectionSet& before, const SelectionSet& after,
                             uint64_t scanned);

// ---------------------------------------------------------------------------
// HitContext abstraction (the marquee/lasso query surface)
// ---------------------------------------------------------------------------
// Exactly T01's ToolHost::HitContext idea, but expressed as a minimal interface
// so T05 has NO dependency on a spatial index implementation. A caller with a
// BVH implements objectsInBox/objectsInPolygon by querying it; a caller without
// one implements them as a full scan. The tests provide BOTH and assert they
// agree, which is how "the BVH path is correct" is proven rather than assumed.
class SelectionQuery {
public:
    virtual ~SelectionQuery() {}
    // Objects whose bounds intersect a document-space box.
    virtual std::vector<core::ObjectRef> objectsInBox(const core::Box& box) const = 0;
    // Objects whose bounds intersect (or lie inside, per `containedOnly`) a
    // freehand document-space polygon. The caller picks the rule; this module
    // does not silently pick one.
    virtual std::vector<core::ObjectRef> objectsInPolygon(const std::vector<core::Vec2>& points,
                                                          bool containedOnly) const = 0;
    // Every selectable object. Used by Select All and by the full-scan fallback.
    virtual std::vector<core::ObjectRef> allObjects() const = 0;
    // Bounds of one object, using whatever metric options the implementation
    // holds. This is on the interface because a CONTAINED marquee must re-test
    // the object box against a padded marquee, and the query is the only party
    // that knows whether real text metrics were injected for that computation.
    virtual core::Box objectBoundsForQuery(const core::ObjectRef& ref) const = 0;
    // Monotonic counter; the host bumps it on every geometry edit so a cached
    // index can be detected as stale.
    virtual uint64_t geometrySerial() const = 0;
};

// A full-scan implementation over a Document. Correct by construction and the
// O(N) reference the indexed paths are compared against.
class DocumentSelectionQuery : public SelectionQuery {
public:
    explicit DocumentSelectionQuery(const core::Document& document)
        : document_(document), options_(), serial_(1) {
        options_.defaultTextSize = document.defaultTextSize();
        options_.defaultFont = document.defaultFont();
    }
    void setBoundsOptions(const BoundsOptions& options) { options_ = options; }
    void setTextMetrics(TextMetricSink* sink) { options_.textMetrics = sink; }
    void bumpSerial() { ++serial_; }

    virtual std::vector<core::ObjectRef> objectsInBox(const core::Box& box) const;
    virtual std::vector<core::ObjectRef> objectsInPolygon(const std::vector<core::Vec2>& points,
                                                          bool containedOnly) const;
    virtual std::vector<core::ObjectRef> allObjects() const;
    virtual core::Box objectBoundsForQuery(const core::ObjectRef& ref) const;
    virtual uint64_t geometrySerial() const { return serial_; }

    // Counters so a test can prove the scan actually happened exactly once.
    mutable uint64_t boxQueries;
    mutable uint64_t polygonQueries;
    mutable uint64_t fullScans;

private:
    const core::Document& document_;
    BoundsOptions options_;
    uint64_t serial_;
};

// ---------------------------------------------------------------------------
// Geometry predicates used by marquee/lasso
// ---------------------------------------------------------------------------
// A box CONTAINS a point. Kept here (not in the header inline block) because the
// polygon version needs an even-odd crossing count with a written boundary rule.
bool boxContainsPoint(const core::Box& box, const core::Vec2& point);
bool boxContainsBox(const core::Box& outer, const core::Box& inner);
bool boxIntersectsBox(const core::Box& a, const core::Box& b);
// Even-odd point-in-polygon. A point exactly ON an edge is INSIDE (a marquee is
// a grab region, and excluding the boundary makes edge atoms unselectable).
bool polygonContainsPoint(const std::vector<core::Vec2>& polygon, const core::Vec2& point);
bool polygonContainsBox(const std::vector<core::Vec2>& polygon, const core::Box& box);
bool polygonIntersectsBox(const std::vector<core::Vec2>& polygon, const core::Box& box);

// ---------------------------------------------------------------------------
// Marquee / lasso rules
// ---------------------------------------------------------------------------
enum class MarqueeMode : uint8_t {
    // Left-to-right / any drag with EnableContainment: only objects fully inside.
    Contained = 0,
    // Right-to-left or an explicit override: anything the box touches.
    Intersecting = 1
};

enum class MarqueeShape : uint8_t { Rect = 0, Lasso = 1 };

struct MarqueeOptions {
    MarqueeShape shape;
    MarqueeMode  mode;
    // When true the mode is recomputed from the drag direction (ChemDraw's
    // left-to-right = contained / right-to-left = crossing convention). This is
    // PROJECT policy: the guide excerpt does not state the direction rule, so it
    // is a switch and defaults to off, meaning the caller's mode wins.
    bool deriveModeFromDirection;
    // A box that is a bare ATOM POINT cannot be "contained" by a thin marquee
    // without a tolerance, so a contained test inflates by this many document
    // units. Default 0 keeps the test exact; a UI passes a pixel-derived value.
    double containmentSlack;
    MarqueeOptions()
        : shape(MarqueeShape::Rect), mode(MarqueeMode::Intersecting),
          deriveModeFromDirection(false), containmentSlack(0.0) {}
};

// The marquee/gesture state. `anchor` and `current` are DOCUMENT coordinates.
struct MarqueeGesture {
    MarqueeShape shape;
    core::Vec2   anchor;
    core::Vec2   current;
    std::vector<core::Vec2> lasso;   // lasso path; the last point equals current
    uint64_t     samples;            // lasso points appended (dedup counted here)
    MarqueeGesture() : shape(MarqueeShape::Rect), samples(0) {}
    bool empty() const { return shape == MarqueeShape::Rect && anchor == current && lasso.empty(); }
    void reset();
    // Append a lasso sample, ignoring a point within `minDistance` of the last
    // one. Returns true when the sample was kept (so a caller can skip a redraw).
    bool addLassoPoint(const core::Vec2& point, double minDistance);
};

// Objects a marquee/gesture selects. Pure: it does not touch the set.
std::vector<core::ObjectRef> queryMarquee(const SelectionQuery& query,
                                          const MarqueeGesture& gesture,
                                          const MarqueeOptions& options,
                                          uint64_t* scanned);

// ---------------------------------------------------------------------------
// Selection operations
// ---------------------------------------------------------------------------
// Every operation takes the CURRENT set and returns the NEW set plus a delta.
// None of them write the document.

// A click on one object. `additive` == Shift held; `toggle` == Ctrl held with
// no drag (a Ctrl-click toggles membership). PROJECT policy for Ctrl-click,
// recorded as such.
SelectionSet selectOne(const core::Document& document, const SelectionSet& current,
                       const core::ObjectRef& ref, bool additive, bool toggle,
                       SelectionDelta* delta);

// Marquee / lasso. `additive` replaces the set; when additive the new objects
// are UNIONED with the current set (Shift+marquee adds, per [G82-85]).
SelectionSet selectMarquee(const core::Document& document, const SelectionSet& current,
                           const SelectionQuery& query, const MarqueeGesture& gesture,
                           const MarqueeOptions& options, bool additive,
                           SelectionDelta* delta);

// The connected molecule containing `seed`: the seed atom, every atom reachable
// through alive bonds, and every bond traversed. BONDS ARE INCLUDED because the
// user selected a molecule, not a pile of atoms. A seed that is not an atom
// resolves to the object plus the molecule of each of its endpoints (a bond seed
// selects the atoms it joins and their whole component).
SelectionSet selectMolecule(const core::Document& document, const SelectionSet& current,
                            const core::ObjectRef& seed, bool additive,
                            SelectionDelta* delta);

// A group and ALL its members, transitively. Members are marked GroupMember so
// a later ungroup/delete can tell them apart from direct picks.
SelectionSet selectGroup(const core::Document& document, const SelectionSet& current,
                         core::ObjectId groupId, bool additive, SelectionDelta* delta);

// Every alive object. Reports the scan honestly.
SelectionSet selectAll(const core::Document& document, const SelectionQuery& query,
                       const SelectionSet& current, SelectionDelta* delta);

// Inverse of the current set over all objects. ChemDraw's "Select Inverse" is a
// menu command; the API exists because it is the cheapest possible test of the
// delta machinery (every object changes state).
SelectionSet selectInverse(const core::Document& document, const SelectionQuery& query,
                           const SelectionSet& current, SelectionDelta* delta);

// Close a selection over its dependencies: a selected bond whose atoms are not
// selected pulls them in as Dependency; a selected group pulls in its members;
// a selected annotation pulls in nothing (anchors are soft references and are
// reported instead, see closeSelectionReport).
SelectionSet closeSelection(const core::Document& document, const SelectionSet& input,
                            SelectionDelta* delta);

struct CloseReport {
    uint64_t bondsPulledAtoms;
    uint64_t groupsPulledMembers;
    uint64_t annotationsWithUnselectedAnchors;
    CloseReport() : bondsPulledAtoms(0), groupsPulledMembers(0),
                    annotationsWithUnselectedAnchors(0) {}
};
SelectionSet closeSelection(const core::Document& document, const SelectionSet& input,
                            CloseReport* report, SelectionDelta* delta);

// Refuse to merge. Distance alone is NOT a compatibility test: two nodes 0.2pt
// apart can be a carbon and a charged nitrogen, and merging those destroys
// chemistry. Returns true only when BOTH nodes carry the same element and the
// same multi-axis semantics, and neither carries a label override. This is the
// explicit-command form the task demands, distinct from snapping and grouping.
bool mergeCompatible(const core::Document& document, core::ObjectId a, core::ObjectId b,
                     std::string* why);

// The group that DIRECTLY owns `ref`, or kInvalidObjectId. The host needs it to
// decide whether a click means "the group" or "the object".
core::ObjectId groupOwning(const core::Document& document, const core::ObjectRef& ref);

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_SELECTION_SET_HPP
