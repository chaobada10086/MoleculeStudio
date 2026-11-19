#ifndef R62_NEXT_BIODRAW_TOOLS_HPP
#define R62_NEXT_BIODRAW_TOOLS_HPP
// =============================================================================
// re_round62_viewer/next/biodraw/tools.hpp
//
// T14 - THE 89 PLACEMENT PRESETS, THE PLACE TOOL AND THE EDIT HANDLES.
//
// -----------------------------------------------------------------------------
// THE ONE-INSTANCE-PER-BEHAVIOR CONSTRAINT
// -----------------------------------------------------------------------------
// core::ToolRegistry keys tools by ToolBehavior (std::map<ToolBehavior, Tool*>),
// so a behavior holds exactly ONE instance. All 89 BioDraw presets are
// PlaceSingle, so registering 89 tools would leave only the last one reachable --
// every toolbar button would then place the SAME figure. (T11 hit this with
// shapes and brackets and solved it the same way.)
//
// BioDrawPlaceTool therefore holds PlaceSingle and DISPATCHES to the preset that
// is currently active. Registration is 89 presets and ONE tool instance.
//
// -----------------------------------------------------------------------------
// PREVIEW == COMMIT
// -----------------------------------------------------------------------------
// During a drag the tool NEVER mutates the document. It publishes a PreviewScene
// built by the SAME function that the commit path uses to resolve the parameters
// (resolveBioPreset). On release the already-resolved parameters are submitted,
// so 'the preview and the result differ' cannot happen by construction.
// =============================================================================

#include "next/biodraw/geometry.hpp"
#include "next/biodraw/model.hpp"
#include "next/core/tool_registry.hpp"

#include <vector>

namespace r62 {
namespace biodraw {

// Tool id namespace. Matches the preset rows' toolId strings in preset.cpp.
namespace tool_ids {
extern const char* const kPlaceBioShape;   // "r62.tool.biodraw.place"
extern const char* const kEditBioShape;    // "r62.tool.biodraw.edit"
}

// ---------------------------------------------------------------------------
// BioDrawPlaceTool
// ---------------------------------------------------------------------------
// behavior() == ToolBehavior::PlaceSingle. One instance serves all 89 presets.
class BioDrawPlaceTool : public core::Tool {
public:
    BioDrawPlaceTool();

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceSingle; }

    // Activate a preset. Returns false for an unknown id, in which case the
    // previously active preset stays active (never silently falls back to 0).
    bool activatePreset(uint32_t presetId);
    uint32_t activePreset() const { return activePreset_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    // Escape cancels; 1..9 and +/- adjust the ACTIVE figure's most useful
    // parameter while a figure is selected (component count is the one users
    // change most), which is the 'editable data' requirement at the keyboard.
    bool handleKey(core::ToolHost& host, uint32_t virtualKey, uint32_t modifiers);

    bool dragging() const { return dragging_; }
    // The parameters the preview is currently showing. The commit path submits
    // EXACTLY these, which is what makes preview and result agree.
    const BioShapeParams& previewParams() const { return preview_; }

private:
    void publishPreview(core::ToolHost& host, const core::Vec2& begin,
                        const core::Vec2& current);

    core::ToolId   id_;
    uint32_t       activePreset_;
    bool           dragging_;
    core::Vec2     beginPoint_;
    BioShapeParams preview_;
};

// ---------------------------------------------------------------------------
// Handles for an existing figure
// ---------------------------------------------------------------------------
// A BioDraw figure exposes control handles so its PARAMETERS can be dragged
// directly instead of only typed. Each handle names the parameter it edits and
// is positioned on the geometry it controls, so a handle is never somewhere
// unrelated to its effect.
enum class BioHandleKind : uint8_t {
    ExtentU = 0,       // drag the figure's +x half-extent
    ExtentV,           // drag the figure's +y half-extent
    Centre,            // drag the whole figure
    UnitSpacing,       // drag a component's spacing
    UnitSize,          // drag a component's unit size
    PlasmidStart,      // drag a plasmid feature's start base
    PlasmidEnd         // drag a plasmid feature's end base
};
const char* bioHandleName(BioHandleKind kind);

struct BioHandle {
    BioHandleKind kind;
    core::Vec2    position;    // document space
    size_t        index;       // component or feature index, 0 when unused
    BioHandle() : kind(BioHandleKind::Centre), index(0) {}
};

// The handles for a figure, in document space. Derived from the SAME geometry the
// renderer draws, so a handle always sits on the thing it moves.
std::vector<BioHandle> bioShapeHandles(const BioShapeParams& params,
                                       double documentLineWidth);

// The handle nearest a document-space point within `tolerance`, or false.
bool nearestBioHandle(const std::vector<BioHandle>& handles, const core::Vec2& point,
                      double tolerance, size_t* index);

// ---------------------------------------------------------------------------
// Gesture: dragging one handle
// ---------------------------------------------------------------------------
// Holds the ORIGINAL parameters, so each update recomputes from the original
// rather than accumulating rounding error across a drag. solve() returns the
// parameters the drag currently implies; the caller previews them and submits
// the final value as ONE transaction.
class BioHandleGesture {
public:
    BioHandleGesture();
    bool begin(const BioShapeParams& original, size_t handleIndex, const core::Vec2& point);
    bool active() const { return active_; }
    BioHandleKind kind() const { return handle_->kind; }
    size_t handleIndex() const { return handleIndex_; }
    // Solve the parameters implied by the current pointer position.
    BioShapeParams solve(const core::Vec2& current, double documentLineWidth) const;
    void cancel();
private:
    bool               active_;
    BioShapeParams     original_;
    std::vector<BioHandle> handles_;
    size_t             handleIndex_;
    core::Vec2         grabPoint_;
    const BioHandle*   handle_;
};

// ---------------------------------------------------------------------------
// Edit tool (selection-driven parameter editing)
// ---------------------------------------------------------------------------
// behavior() == ToolBehavior::Select would STEAL the selection behavior from T05,
// so this tool is NOT registered by registerBioPresets(). It is a plain object the
// host may instantiate and drive explicitly for spec edits, exactly as T11 left
// EditTableTool unregistered.
class BioEditTool {
public:
    // Change the component count of the figure `id`. Returns a command; the
    // caller submits it, so one edit is one undo step.
    core::Command setCount(const core::Document& document, core::ObjectId id,
                           size_t componentIndex, int count) const;
    core::Command setSpacing(const core::Document& document, core::ObjectId id,
                             size_t componentIndex, double spacing) const;
    core::Command setUnitSize(const core::Document& document, core::ObjectId id,
                              size_t componentIndex, double unitSize) const;
    core::Command setPlasmidBaseCount(const core::Document& document, core::ObjectId id,
                                      int baseCount) const;
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
// Register all 89 presets plus the ONE tool instance, and bind each preset to a
// platform command id in the ToolBand. Returns the number of presets bound.
//
// Returns 0 and fills *firstError when the band is exhausted or a preset is
// rejected; a partial registration is reported, never silently accepted.
size_t registerBioPresets(core::ToolRegistry& registry, BioDrawPlaceTool* placeTool,
                          std::string* firstError);

} // namespace biodraw
} // namespace r62

#endif // R62_NEXT_BIODRAW_TOOLS_HPP
