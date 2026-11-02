#ifndef R62_NEXT_EXPERIMENTAL_TOOLS_HPP
#define R62_NEXT_EXPERIMENTAL_TOOLS_HPP
// =============================================================================
// re_round62_viewer/next/experimental/tools.hpp
//
// T16 - TOOLS AND GESTURES FOR THE TWO EXPERIMENTAL FIGURE COMMANDS.
//
// -----------------------------------------------------------------------------
// ONE INSTANCE, TWO PRESETS - AND THE PlaceSingle COLLISION, SAID OUT LOUD
// -----------------------------------------------------------------------------
// T01 ToolRegistry keys tools by ToolBehavior and keeps ONE instance per behavior
// ("tool_registry.hpp"), so two modules that both register ToolBehavior::PlaceSingle
// silently overwrite each other. T11 next/graphics already owns PlaceSingle with its
// PlaceGraphicsTool. Registering PlacePlateTool directly would therefore be a coin
// flip on which module answers a click.
//
// THE RESOLUTION: T16 exposes the placement tool AND a dispatcher that owns the
// single PlaceSingle slot and forwards to whichever owner the activated preset
// belongs to. The foreign owner is held as a bare core::Tool* plus a
// std::function hook, so this module does NOT link or include next/graphics:
//
//     PlateToolDispatcher dispatcher;
//     PlacePlateTool plate;
//     graphics::PlaceGraphicsTool shapes;              // owned by T19
//     dispatcher.adoptPlate(&plate);
//     dispatcher.adoptForeign(&shapes);
//     dispatcher.setForeignActivate([&shapes](uint32_t id) {
//         return shapes.activatePreset(id); });
//     registerExperimentalPresets(registry, &dispatcher, &error);
//     // do NOT also register shapes (or plate) under PlaceSingle
//
// -----------------------------------------------------------------------------
// DRAGGING INSIDE A FIGURE IS NOT DRAGGING THE FIGURE
// -----------------------------------------------------------------------------
// PlateEditGesture resolves, on the press, exactly ONE of:
//   ResizeBoard  - the pointer is on a board grip
//   MoveElement  - the pointer is on a spot / band / lane (the element moves)
//   MoveBody     - the pointer is inside the board but on no element (the WHOLE
//                  figure moves; the frame changes, no relative coordinate does)
// The gesture carries the resolved figure AND frame, and the commit writes exactly
// those values, so the preview cannot disagree with the commit (T01 section 8.4).
// =============================================================================

#include "next/core/tool_registry.hpp"
#include "next/experimental/edit.hpp"

#include <functional>
#include <string>
#include <vector>

namespace r62 {
namespace experimental {

namespace tool_ids {
extern const char* const kPlacePlate;   // one instance serves both presets
} // namespace tool_ids

// ---------------------------------------------------------------------------
// Placement: drag one corner of the board
// ---------------------------------------------------------------------------
class PlacePlateTool : public core::Tool {
public:
    PlacePlateTool();
    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }

    // presetId is the row id in platePresets() (0 = TLC, 1 = gel).
    bool activatePreset(uint32_t presetId);
    uint32_t presetId() const { return presetId_; }
    FigureKind figureKind() const;
    void setLaneCount(uint32_t count) { laneCount_ = count ? count : 1u; }
    uint32_t laneCount() const { return laneCount_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);
    bool handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers);

    const PlateFigure& previewFigure() const { return preview_; }
    const PlateFrame&  previewFrame() const { return previewFrame_; }
    bool dragging() const { return dragging_; }
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;

private:
    core::ToolId id_;
    uint32_t     presetId_;
    uint32_t     laneCount_;
    bool         dragging_;
    core::Vec2   origin_;
    core::Vec2   current_;
    PlateFigure  preview_;
    PlateFrame   previewFrame_;
};

// ---------------------------------------------------------------------------
// Edit gesture: pick once, resolve every frame, commit the resolved value
// ---------------------------------------------------------------------------
struct PlateEditGesture {
    enum class Mode : uint8_t { None = 0, MoveElement, MoveBody, ResizeBoard };

    core::ObjectId object;
    Mode           mode;
    bool           active;
    ElementRef     element;      // MoveElement
    BoardHandle    handle;       // ResizeBoard
    PlateFrame     startFrame;
    PlateFigure    startFigure;
    PlateFrame     currentFrame;
    PlateFigure    currentFigure;
    core::Vec2     startPointer;
    double         grabU, grabV; // element grab offset, relative units
    bool           changed;

    PlateEditGesture();
    // Picks the strongest element under the pointer. Returns false when the
    // pointer is not on the figure at all.
    bool begin(const core::Document& document, core::ObjectId figureId,
               const core::Vec2& pointer, double tolerance);
    // Resolve the CURRENT values. Shift constrains a body move to one axis.
    bool update(const core::Vec2& pointer, bool constrainAxis);
    void cancel();
    void buildPreview(core::PreviewScene* scene, const core::ObjectRef& subject) const;
};

// Commit the RESOLVED gesture values in one transaction. Also re-anchors the
// figure annotation text objects (T09) when the frame moved.
core::Command commitGestureCommand(const PlateEditGesture& gesture, EditOutcome* out);

// ---------------------------------------------------------------------------
// PlaceSingle dispatcher (see the header block)
// ---------------------------------------------------------------------------
class PlateToolDispatcher : public core::Tool {
public:
    typedef std::function<bool(uint32_t)> ForeignActivateFn;

    PlateToolDispatcher();
    void adoptPlate(PlacePlateTool* plate);
    void adoptForeign(core::Tool* foreign);
    void setForeignActivate(const ForeignActivateFn& fn) { foreignActivate_ = fn; }
    // Mark a REGISTRY preset id as owned by this module, and say which local
    // preset row it activates. Keeping the pair explicit avoids assuming the
    // registry assigned our ids contiguously in our own order.
    void ownPreset(uint32_t registryPresetId, uint32_t localPresetId);
    bool ownsPreset(uint32_t registryPresetId) const;
    // Local preset row for a registry id, or kPresetNone.
    uint32_t localPresetFor(uint32_t registryPresetId) const;
    // Routes by preset id: ours -> the plate placer, otherwise the foreign hook.
    bool activatePreset(uint32_t registryPresetId);
    bool plateActive() const { return plateActive_; }

    const core::ToolId& id() const;
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);
    bool handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers);
    core::Invalidation activate(core::ToolHost& host);
    void deactivate(core::ToolHost& host);

private:
    core::ToolId       id_;
    PlacePlateTool*    plate_;
    core::Tool*        foreign_;
    ForeignActivateFn  foreignActivate_;
    struct OwnedPreset { uint32_t registryId; uint32_t localId; };
    std::vector<OwnedPreset> ownIds_;
    bool               plateActive_;
};

// ---------------------------------------------------------------------------
// Registry wiring
// ---------------------------------------------------------------------------
// Registers the two presets (TLC + gel) and binds each to ToolBand + its dense
// preset id. Returns how many were bound; firstError receives the first failure.
// The caller must NOT register another PlaceSingle tool (see the header block).
size_t registerExperimentalPresets(core::ToolRegistry& registry,
                                   PlateToolDispatcher* dispatcher,
                                   std::string* firstError);

} // namespace experimental
} // namespace r62

#endif // R62_NEXT_EXPERIMENTAL_TOOLS_HPP
