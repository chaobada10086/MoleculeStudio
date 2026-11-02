#ifndef R62_NEXT_SELECTION_OVERLAY_HPP
#define R62_NEXT_SELECTION_OVERLAY_HPP
// =============================================================================
// re_round62_viewer/next/selection/overlay.hpp
//
// T05 - BATCHED HIGHLIGHT / MARQUEE / GHOST SUBMISSION.
//
// THE REQUIREMENT, LITERALLY
// -----------------------------------------------------------------------------
// "高亮批量提交，不逐个对象单独调用绘制" -
// the highlight must be submitted as a BATCH, not one draw call per object.
//
// The way to make that structural rather than aspirational is to never expose a
// per-object drawing entry point at all: this module produces ONE OverlayBatch
// per frame, and a caller has no API that draws a single highlight. A test
// counts the batch submissions against the object count and asserts the ratio.
//
// IT DOES NOT DRAW. IT DOES NOT LINK OpenGL.
// -----------------------------------------------------------------------------
// An OverlayBatch is a flat list of MARK records in document space plus a
// summary. T03's SceneBuilder::buildSelection consumes the marks and produces
// render::Primitive values; the backend submits them. This module therefore has
// no GPU dependency and is testable headlessly - which is how "the batch has
// the right CONTENT" is checked without a screenshot.
//
// TWO KINDS OF UPDATE
// -----------------------------------------------------------------------------
// Full: the selection changed wholesale (Select All, Open). One batch, N marks.
// Incremental: an atom entered or left (a Shift-click). The host hands the
// SelectionDelta; only the changed objects produce marks, and the batch carries
// the counts so the ratio is measurable. A full repaint of 50,000 highlights
// because one object changed is exactly the defect this design forbids.
// =============================================================================

#include "next/core/document.hpp"
#include "next/selection/bounds.hpp"
#include "next/selection/commands.hpp"
#include "next/selection/selection_set.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace selection {

// ---------------------------------------------------------------------------
// HighlightMarkKind
// ---------------------------------------------------------------------------
enum class HighlightMarkKind : uint8_t {
    AtomRing = 0,     // a circle at the atom position
    BondBar = 1,      // a rectangle around the bond segment
    ObjectBox = 2,    // an axis-aligned box around the object's bounds
    GroupFrame = 3,   // a dashed box around a group, distinct from a member box
    Handle = 4        // a drag handle at a bounds corner/edge
};

const char* highlightMarkKindName(HighlightMarkKind kind);

// Where a handle sits on a box.
enum class HandleCorner : uint8_t {
    TopLeft = 0, TopCenter, TopRight,
    MiddleLeft, MiddleRight,
    BottomLeft, BottomCenter, BottomRight
};

// ---------------------------------------------------------------------------
// HighlightMark
// ---------------------------------------------------------------------------
// One mark. Kept as a POD-ish value so a 50,000-mark batch is one contiguous
// buffer, not 50,000 heap objects.
struct HighlightMark {
    HighlightMarkKind kind;
    core::ObjectRef   source;
    // AtomRing: centre + radius. BondBar/ObjectBox/GroupFrame: the two corners.
    // Handle: center + size.
    core::Vec2 a;
    core::Vec2 b;
    double     size;
    double     z;             // draw order within the batch
    uint32_t   rgba;
    uint8_t    dashed;
    HighlightMark()
        : kind(HighlightMarkKind::ObjectBox), source(), a(), b(), size(0.0), z(0.0),
          rgba(0x1A73E8FFu), dashed(0) {}
};

// ---------------------------------------------------------------------------
// OverlayBatch
// ---------------------------------------------------------------------------
// ONE batch per frame. `submissions` is the number of times the caller handed
// the batch to the renderer, and it must be 1. `rebuilds` counts how many times
// the batch CONTENT was rebuilt; an incremental update with no delta must not
// rebuild.
struct OverlayBatch {
    std::vector<HighlightMark> marks;
    // Marquee rectangle, when a marquee gesture is live. Separate from the marks
    // because it is a guide, not a highlight, and lives on a different layer.
    bool       hasMarquee;
    core::Vec2 marqueeA;
    core::Vec2 marqueeB;
    std::vector<core::Vec2> lassoPath;
    // Copy-drag ghost: the frozen source bounds translated by the drag offset.
    bool       hasGhost;
    core::Vec2 ghostA;
    core::Vec2 ghostB;

    uint64_t   submissions;      // times recordSubmission() handed this batch over
    uint64_t   submitCallsRecorded;  // the matching global counter at that moment
    uint64_t   rebuilds;
    uint64_t   marksEmitted;
    uint64_t   objectsConsidered;
    bool       incremental;
    OverlayBatch()
        : hasMarquee(false), marqueeA(), marqueeB(), hasGhost(false), ghostA(), ghostB(),
          submissions(0), submitCallsRecorded(0), rebuilds(0), marksEmitted(0),
          objectsConsidered(0), incremental(false) {}
    size_t markCount() const { return marks.size(); }
    // THE metric the requirement is about: marks per submission. A batch that
    // submits once for N marks has a ratio of N; a per-object implementation has
    // a ratio of 1 and N submissions.
    double marksPerSubmission() const {
        return submissions == 0 ? 0.0 : static_cast<double>(marks.size()) /
                                         static_cast<double>(submissions);
    }
    void clear() {
        marks.clear();
        hasMarquee = false;
        lassoPath.clear();
        hasGhost = false;
        marksEmitted = 0;
        objectsConsidered = 0;
        incremental = false;
    }
    std::string summary() const;
};

// ---------------------------------------------------------------------------
// OverlayOptions
// ---------------------------------------------------------------------------
struct OverlayOptions {
    double atomRingRadius;      // document units
    double bondBarHalfWidth;    // document units
    double handleSize;          // document units
    double boxPadding;          // document units added around an object box
    uint32_t selectionColor;
    uint32_t groupColor;
    uint32_t handleColor;
    uint32_t marqueeColor;
    uint32_t ghostColor;
    bool     dashedGroupFrame;
    bool     emitHandles;
    // Cap on marks per batch. Exceeding it is REPORTED (truncatedMarks) rather
    // than silently dropping content - a truncated overlay must be visible.
    size_t   maxMarks;
    // Text metrics for measuring a text object's box; null means anchor only.
    TextMetricSink* textMetrics;
    double   defaultTextSize;
    OverlayOptions()
        : atomRingRadius(6.0), bondBarHalfWidth(3.0), handleSize(4.0),
          boxPadding(0.0), selectionColor(0x1A73E8FFu), groupColor(0x8E44ADFFu),
          handleColor(0x1A73E8FFu), marqueeColor(0x9E9E9EFFu),
          ghostColor(0x8A8A8AFFu), dashedGroupFrame(true), emitHandles(true),
          maxMarks(200000), textMetrics(0), defaultTextSize(10.0) {}
};

// ---------------------------------------------------------------------------
// buildSelectionOverlay
// ---------------------------------------------------------------------------
// The full rebuild. ONE batch, one mark per selected object (a group produces a
// GroupFrame AND member boxes). Emits handles for the set bounds when asked.
OverlayBatch buildSelectionOverlay(const core::Document& document,
                                   const SelectionSet& selection,
                                   const OverlayOptions& options);

// The incremental rebuild. Only the objects in `delta` produce marks, so a
// Shift-click on one atom produces a bounded number of marks. The caller merges
// the returned batch into its persistent batch (see mergeInto).
OverlayBatch buildSelectionOverlayIncremental(const core::Document& document,
                                              const SelectionSet& selection,
                                              const SelectionDelta& delta,
                                              const OverlayOptions& options);

// Merge `update` into `accumulator`: remove the marks whose source left the
// selection, add the marks whose source entered, and keep everything else. This
// is the operation that makes the highlight persistent across frames without a
// per-frame rebuild. Returns the number of marks added.
size_t mergeInto(OverlayBatch* accumulator, const OverlayBatch& update);

// Drop every mark whose source is in refs. This is how the delta's "left" list
// is applied: objects that left the selection lose their highlight without
// rebuilding the rest. Returns the number of marks removed.
size_t removeMarksFor(OverlayBatch* batch, const std::vector<core::ObjectRef>& refs);

// Attach the live marquee/lasso path to a batch. Separate from the selection
// build because a marquee changes every mouse move while the selection does not.
void attachMarquee(OverlayBatch* batch, const MarqueeGesture& gesture);

// Attach a copy-drag ghost: the FROZEN source bounds translated by the drag
// offset. Because the bounds are frozen at begin(), the ghost visibly separates
// from the original as the pointer moves - which is the visual proof that the
// original was never mutated.
void attachGhost(OverlayBatch* batch, const CopyDrag& drag);

// The document-space box the overlay covers. Used by a caller to decide whether
// a repaint is needed at all.
core::Box overlayBounds(const OverlayBatch& batch);

// ---------------------------------------------------------------------------
// OverlayStats: the evidence
// ---------------------------------------------------------------------------
struct OverlayStats {
    uint64_t frames;
    uint64_t submissions;          // MUST equal frames for a batched overlay
    uint64_t marksEmitted;
    uint64_t fullRebuilds;
    uint64_t incrementalRebuilds;
    uint64_t marksKeptAcrossFrames;
    uint64_t truncatedMarks;
    uint64_t submitCalls;          // total hand-offs to the renderer
    OverlayStats()
        : frames(0), submissions(0), marksEmitted(0), fullRebuilds(0),
          incrementalRebuilds(0), marksKeptAcrossFrames(0), truncatedMarks(0),
          submitCalls(0) {}
    void clear();
    double marksPerSubmission() const {
        return submissions == 0 ? 0.0 : static_cast<double>(marksEmitted) /
                                         static_cast<double>(submissions);
    }
    std::string summary() const;
};

// Counts one frame's hand-off. A caller calls this ONCE per frame regardless of
// how many marks the batch holds; a per-object implementation would have to call
// it N times, and the counter is what makes that visible in the report.
void recordSubmission(OverlayStats* stats, const OverlayBatch& batch);

} // namespace selection
} // namespace r62

#endif // R62_NEXT_SELECTION_OVERLAY_HPP
