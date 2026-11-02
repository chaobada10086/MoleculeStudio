#ifndef R62_NEXT_ARROWS_CURVES_TOOLS_HPP
#define R62_NEXT_ARROWS_CURVES_TOOLS_HPP
// =============================================================================
// re_round62_viewer/next/arrows_curves/tools.hpp
//
// T10 - THE INTERACTIVE TOOLS.
//
// Three bounded ToolBehavior values cover the whole T10 scope:
//
//   ToolBehavior::PlaceSingle  - the 56 arrow presets (and the curve-style
//                                presets, which apply a style),
//   ToolBehavior::DrawCurve    - choosePenTool_DrawCurve
//   ToolBehavior::EditCurve    - choosePenTool_EditCurve
//
// -----------------------------------------------------------------------------
// THE PREVIEW/COMMIT RULE, MADE STRUCTURAL
// -----------------------------------------------------------------------------
// A T01 Tool publishes a PreviewScene and NEVER mutates the document during a
// gesture; the commit happens in endGesture through ToolHost::submit().
//
// The classic defect is preview and commit disagreeing. Here that is impossible
// by construction: both read the SAME member (`pending_`), and both derive their
// geometry from the SAME functions in geometry.hpp. There is no second path.
//
// -----------------------------------------------------------------------------
// WHAT IS AND IS NOT IMPLEMENTED
// -----------------------------------------------------------------------------
// Implemented: arrow placement, arc creation and radius editing through the
// middle handle, endpoint/handle dragging, reversal, head size, equilibrium
// split editing, freehand curve drawing with decimation, control-point editing,
// segment addition, point deletion, curve closure, and one-transaction-per-
// gesture commits for every one of them.
//
// NOT implemented, and reported through reportUnsupported() rather than silently
// approximated: rotation by the Structure Perspective tool (T15 scope), grouping
// (T05 scope), and the exact vendor geometry for the Resonance, Barb, Cross/Hash
// and HalfArrow head shapes (geometry.cpp implements them as PROJECT
// interpretations of presets whose precise geometry the guide does not state).
// =============================================================================

#include "next/arrows_curves/geometry.hpp"
#include "next/arrows_curves/model.hpp"
#include "next/arrows_curves/preset.hpp"
#include "next/core/tool_registry.hpp"
#include "next/interaction/hotspot.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace arrows {

// ---------------------------------------------------------------------------
// Pointer-modifier helpers, shared by every T10 tool
// ---------------------------------------------------------------------------
// Alt releases the fixed length/angle constraints [GUIDE p94-95, recorded in
// SOURCES_AND_LIMITS.md]. Shift requests the CONNECT affordance over a midpoint
// [GUIDE p111]. Neither is guessed: both are named functions so a test can call
// them directly.
inline bool altReleasesConstraints(uint32_t modifiers) {
    return (modifiers & core::kModAlt) != 0u;
}
inline bool shiftRequestsConnect(uint32_t modifiers) {
    return (modifiers & core::kModShift) != 0u;
}

// Resolve a drag from `press` to `pointer` into arrow parameters, applying the
// fixed length/angle profile unless Alt releases it. This is THE resolver used by
// BOTH the live preview and the commit: there is no second code path.
ArrowParams resolveArrowDrag(uint32_t presetId, const core::Vec2& press,
                             const core::Vec2& pointer, double documentLineWidth,
                             bool fixedLength, bool fixedAngles, double angleStepDegrees,
                             double defaultBondLength, uint32_t modifiers);

// Where a newly placed arrow should START, honouring the snap module's resolved
// point (an exact stored atom coordinate) rather than the raw pointer.
core::Vec2 resolvePlacementOrigin(const core::Document& document,
                                  const core::Vec2& pointer,
                                  core::ObjectId nearestAtom);

// ---------------------------------------------------------------------------
// Shared gesture state
// ---------------------------------------------------------------------------
struct ArrowGestureState {
    bool        active;
    HandleKind  grabbed;      // which handle the press landed on
    size_t      handleIndex;  // index into arrowHandles()
    core::ObjectId target;    // document arrow being edited (kInvalidObjectId for new)
    ArrowParams pending;      // THE value preview and commit both read
    core::Vec2  pressPoint;
    bool        moved;        // did the pointer move past the click slop?
    ArrowGestureState()
        : active(false), grabbed(HandleKind::None), handleIndex(0),
          target(core::kInvalidObjectId), pressPoint(0, 0), moved(false) {}
};

// ---------------------------------------------------------------------------
// ArrowTool
// ---------------------------------------------------------------------------
// ONE instance serves all 56 presets: the preset id is a constructor argument,
// not a subclass. That is the concrete form of "56 presets are data, not 56
// algorithms".
class ArrowTool : public core::Tool {
public:
    explicit ArrowTool(uint32_t presetId);

    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }
    uint32_t presetId() const { return presetId_; }
    void setStrokeWidth(double width) { defaultStrokeWidth_=width; }

    core::Invalidation activate(core::ToolHost& host);
    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    // Geometry actually published to the preview on the last update. A test
    // compares this with the committed value: they must be equal.
    const ArrowParams&   previewParams() const { return state_.pending; }
    const ArrowGeometry& previewGeometry() const { return previewGeometry_; }
    const ArrowGestureState& state() const { return state_; }

    // Commit the pending params as ONE transaction. Public so a test can drive the
    // gesture headlessly without a full platform layer.
    core::Error commit(core::ToolHost& host);

private:
    void publishPreview(core::ToolHost& host, const core::ViewTransform& view);
    size_t pickHandle(const std::vector<Handle>& handles, const core::Vec2& pointer,
                      double deviceRadius, const core::ViewTransform& view) const;

    double defaultStrokeWidth_=0;
    uint32_t          presetId_;
    core::ToolId      id_;
    ArrowGestureState state_;
    ArrowGeometry     previewGeometry_;
};

// ---------------------------------------------------------------------------
// DrawCurveTool  (choosePenTool_DrawCurve)
// ---------------------------------------------------------------------------
// [GUIDE p117] "Select the Draw Curve tool ... and click and drag your cursor
// across the drawing window."
//
// The raw pointer samples are decimated on RELEASE, not during the drag: the
// preview shows exactly what the user drew while the stroke is in progress, and
// the commit stores the simplified control points. During the drag NO document
// work happens at all, which is what keeps a long freehand stroke from allocating
// a transaction per mouse message.
class DrawCurveTool : public core::Tool {
public:
    DrawCurveTool();
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::DrawCurve; }

    core::Invalidation activate(core::ToolHost& host);
    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    void setStyle(const CurveStyleSpec& style) { style_ = style; }
    const CurveStyleSpec& style() const { return style_; }
    void setFreehandOptions(const FreehandOptions& options) { options_ = options; }
    const FreehandOptions& freehandOptions() const { return options_; }

    const std::vector<core::Vec2>& rawSamples() const { return raw_; }
    const std::vector<core::Vec2>& simplified() const { return simplified_; }
    bool previewClosed() const { return previewClosed_; }
    const CurveGeometry& previewGeometry() const { return previewGeometry_; }
    bool active() const { return active_; }

    // Commit the current stroke as ONE transaction. Public for headless testing.
    core::Error commit(core::ToolHost& host);

private:
    void publishPreview(core::ToolHost& host, const core::ViewTransform& view);
    CurveStyleSpec  style_;
    FreehandOptions options_;
    std::vector<core::Vec2> raw_;
    std::vector<core::Vec2> simplified_;
    bool previewClosed_;
    bool active_;
    CurveGeometry previewGeometry_;
};

// ---------------------------------------------------------------------------
// EditCurveTool  (choosePenTool_EditCurve)
// ---------------------------------------------------------------------------
// [GUIDE p117] "Select the Edit Curve tool ... Point at the curve. The cursor
// will change from a "+" to an arrow when you are positioned correctly. Click to
// select the curve. The curve is selected and adjustment handles appear along the
// curve."
//
// Two modes:
//   * nothing selected -> a click picks the curve under the pointer;
//   * a curve selected -> a press on a handle/point grabs it and the drag previews
//     the moved control point.
//
// A click on the curve body with a curve already selected ADDS A SEGMENT
// [GUIDE p118]; a click near the first point CLOSES the curve.
class EditCurveTool : public core::Tool {
public:
    EditCurveTool();
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::EditCurve; }

    core::Invalidation activate(core::ToolHost& host);
    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    // Delete removes the grabbed control point. Kept as a key handler so "delete a
    // point" is reachable without a context menu, which the platform layer owns.
    bool handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers);

    core::ObjectId selectedCurve() const { return selected_; }
    // The point list the preview is showing. A test asserts this equals what was
    // committed after endGesture.
    const std::vector<core::Vec2>& previewPoints() const { return points_; }
    const std::vector<uint8_t>& previewPointKind() const { return pointKind_; }
    bool previewClosed() const { return closed_; }
    const CurveStyleSpec& style() const { return style_; }
    bool dragging() const { return dragging_; }
    size_t dragIndex() const { return dragIndex_; }

    // A click at a specific document point, with the gesture machinery bypassed,
    // so the acceptance test can exercise "click to add a segment" and "click near
    // the start to close" deterministically without a mouse.
    core::Error clickAt(core::ToolHost& host, const core::Vec2& documentPoint);

    // Commit the current point list as ONE transaction. Public for headless testing.
    core::Error commit(core::ToolHost& host);

    // Select a curve explicitly (used by the tests and by menu-driven editing).
    core::Error selectCurve(core::ToolHost& host, core::ObjectId curveId);

private:
    void publishPreview(core::ToolHost& host, const core::ViewTransform& view);
    core::Error selectAt(core::ToolHost& host, const core::Vec2& point, double deviceRadius);
    size_t pickControlPoint(const core::Vec2& point, double documentRadius) const;

    core::ObjectId selected_;
    std::vector<core::Vec2> points_;
    std::vector<uint8_t> pointKind_;
    bool closed_;
    CurveStyleSpec style_;

    bool   active_;
    bool   dragging_;
    size_t dragIndex_;
    core::Vec2 pressPoint_;
    CurveGeometry previewGeometry_;
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
// Registers every row of the arrow table and the curve-style table into a T01
// ToolRegistry, and registers the three tool instances. One call, so T19 does not
// have to know the tables at all. `outArrowPresets` / `outCurveStyles` receive the
// REGISTRY-assigned preset ids, indexed by our own presetId, and may be null.
core::Error registerArrowsCurves(core::ToolRegistry* registry,
                                 uint32_t* outArrowPresets,
                                 uint32_t* outCurveStyles);

// ---------------------------------------------------------------------------
// Candidate / connect affordances
// ---------------------------------------------------------------------------
// Push an arrow's handles into a T04 candidate list. Implemented here so the TOOL
// and the SINK cannot disagree about where the handles are: both call
// arrowHandles().
core::Error appendArrowCandidates(const core::Document& document, core::ObjectId arrowId,
                                  const core::ViewTransform& view,
                                  double documentLineWidth, const Tessellation& tess,
                                  std::vector<interaction::SnapCandidate>* out);

// Push a curve's control points into a T04 candidate list.
core::Error appendCurveCandidates(const core::Document& document, core::ObjectId curveId,
                                  const core::ViewTransform& view,
                                  std::vector<interaction::SnapCandidate>* out);

// [GUIDE p111] The midpoint connection affordance: while Shift is held over the
// midpoint of an existing arrow, the handle becomes a small highlighted rectangle
// and the cursor becomes "+". This resolves WHICH arrow and WHICH slot a new arrow
// would connect to, without mutating anything.
struct ConnectTarget {
    core::ObjectId object;
    AnchorSlot     slot;
    core::Vec2     point;
    bool           valid;
    ConnectTarget() : object(core::kInvalidObjectId), slot(AnchorSlot::None),
                     point(0, 0), valid(false) {}
};

ConnectTarget resolveConnectTarget(const core::Document& document,
                                   const core::Vec2& pointer, double documentRadius,
                                   double documentLineWidth, const Tessellation& tess);

} // namespace arrows
} // namespace r62

#endif // R62_NEXT_ARROWS_CURVES_TOOLS_HPP
