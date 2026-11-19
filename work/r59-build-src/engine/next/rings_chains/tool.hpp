#ifndef R62_NEXT_RINGS_CHAINS_TOOL_HPP
#define R62_NEXT_RINGS_CHAINS_TOOL_HPP
// =============================================================================
// re_round62_viewer/next/rings_chains/tool.hpp
//
// T08 - the platform-facing half: core::Tool implementations and the T04
//       action handlers.
//
// THE RULE THIS FILE EXISTS TO SATISFY
// -----------------------------------------------------------------------------
//   Do not handle generic keyboard messages inside the module. Route through
//   T04 tool events and take every style from T07's bond tool data.
//
// Consequently:
//   * This header contains NO win32 message id and NO virtual-key constant.
//   * The Tool subclasses implement only beginGesture / updateGesture /
//     endGesture / cancelGesture. They never read a key themselves except
//     through the modifiers core::ToolHost already gives them.
//   * handleChar()/handleKey() are deliberately NOT overridden, so a key falls
//     through to T04's keymap rather than being swallowed here.
//   * The bond style used for ring and chain edges comes from the T07 BondAxes
//     carried by the preset, never from a locally invented style.
//
// WHY THE PREVIEW IS A SNAPSHOT
// -----------------------------------------------------------------------------
// updateGesture() recomputes the placement with the SAME resolver the commit
// uses and publishes it through core::PreviewScene. Because the placement is a
// plain value, endGesture() commits the snapshot exactly. There is no second
// geometric computation between preview and commit, which is the T04 contract.
//
// A CANCELLED PREVIEW CANNOT CHANGE THE DOCUMENT because the preview path never
// calls a mutator at all: it only calls resolveRingPlacement()/resolveChainPlan()
// which are read-only by construction.
// =============================================================================

#include "next/interaction/keymap.hpp"
#include "next/interaction/router.hpp"
#include "next/rings_chains/rings_chains.hpp"

namespace r62 {
namespace rings_chains {

// ---------------------------------------------------------------------------
// RingTool - one instance serves all 10 ring presets
// ---------------------------------------------------------------------------
class RingTool : public core::Tool {
public:
    RingTool();

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceRings; }

    // Toolbar activation with a preset: the ONE way a ring preset is selected.
    void setPreset(RingPresetId preset) { preset_ = preset; }
    RingPresetId preset() const { return preset_; }

    // The placement the CURRENT gesture will commit. A test reads this to prove
    // the preview and the commit agree.
    const RingPlacement& pendingPlacement() const { return placement_; }
    const RingPlacement& lastCommittedPlacement() const { return committed_; }

    void resetPerfCounters();
    struct PerfCounters {
        size_t placementsResolved;
        size_t previewsPublished;
        size_t commits;
        PerfCounters() { reset(); }
        void reset() { placementsResolved = previewsPublished = commits = 0; }
    };
    const PerfCounters& perf() const { return perf_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    // Read the hotspot under the pointer through the platform's OWN hit context,
    // so the tool and the renderer agree on what is under the cursor.
    void setHotspotsFrom(core::ToolHost& host, core::HitContext& hits,
                         const core::Vec2& documentPoint, double radiusDevice);

    const std::string& status() const { return status_; }

private:
    // Build the policy from the live host, then resolve. One code path for the
    // preview and for the commit.
    core::Error resolve(core::ToolHost& host, const core::PointerState& pointer);

    core::ToolId  id_;
    RingPresetId  preset_;
    PlacementPolicy policy_;
    RingPlacement placement_;
    RingPlacement committed_;
    core::Vec2    pointer_;
    bool          pressed_;
    bool          haveSecondAnchor_;
    core::ObjectId secondAnchorBond_;
    // The anchor captured at PointerDown and HELD for the whole gesture.
    bool          anchorCaptured_;
    core::ObjectId anchorAtom_;
    core::ObjectId anchorBond_;
    core::ObjectId anchorBond2_;
    std::string   status_;
    PerfCounters  perf_;
};

// ---------------------------------------------------------------------------
// ChainTool - one instance serves both chain presets
// ---------------------------------------------------------------------------
class ChainTool : public core::Tool {
public:
    ChainTool();

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::PlaceChain; }

    void setPreset(ChainPresetId preset);
    ChainPresetId preset() const { return preset_; }
    const ChainPlan& pendingPlan() const { return plan_; }
    const ChainPlan& lastCommittedPlan() const { return committed_; }

    void resetPerfCounters();
    struct PerfCounters {
        size_t plansResolved;
        size_t previewsPublished;
        size_t commits;
        PerfCounters() { reset(); }
        void reset() { plansResolved = previewsPublished = commits = 0; }
    };
    const PerfCounters& perf() const { return perf_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

    const std::string& status() const { return status_; }

private:
    core::Error resolve(core::ToolHost& host, const core::PointerState& pointer);

    core::ToolId   id_;
    ChainPresetId  preset_;
    PlacementPolicy policy_;
    ChainPlan      plan_;
    ChainPlan      committed_;
    core::Vec2     origin_;
    core::Vec2     pointer_;
    bool           pressed_;
    std::string    status_;
    PerfCounters   perf_;
};

// ---------------------------------------------------------------------------
// T04 registration helper
// ---------------------------------------------------------------------------
// Registers the ring and chain tools, their presets and a handler for each
// HotkeyAction this module owns. Returns the number of presets registered.
//
// The handler SIGNATURE is T04's ActionHandler: it receives an ActionRequest
// carrying the resolved hotspot and pointer, so this module never inspects a key.
struct RegistryResult {
    size_t presetsRegistered;
    size_t toolsRegistered;
    size_t actionsRegistered;
    RegistryResult() : presetsRegistered(0), toolsRegistered(0), actionsRegistered(0) {}
};

// Register the tools with the platform's ToolRegistry. `ringTool`/`chainTool`
// stay owned by the caller (a ToolRegistry does not own tool instances).
core::Error registerTools(core::ToolRegistry& registry,
                          RingTool* ringTool, ChainTool* chainTool,
                          RegistryResult* out);

// The actions this module can serve. A caller iterates this list to register
// handlers and to build the coverage report; it is data, not a switch.
struct ActionBinding {
    interaction::HotkeyAction action;
    const char*               sourceCommand;  // tools.csv command, or ""
    RingPresetId              ring;
    ChainPresetId             chain;
    bool                      isChain;
    const char*               note;
};
const std::vector<ActionBinding>& actionBindings();

// Look up the binding for an action, or 0 when this module does not own it.
const ActionBinding* bindingFor(interaction::HotkeyAction action);

// ---------------------------------------------------------------------------
// Action state: what the handler needs that core::ToolHost does not carry
// ---------------------------------------------------------------------------
// T04 ActionRequest carries the action, the hotspot and the pointer. The preset
// a toolbar selected and the two pending placement modes (spiro, reverse-chain)
// are module state, so they live here and the handler reads them.
struct ActionState {
    RingTool*   ringTool;
    ChainTool*  chainTool;
    bool        ringMode;
    bool        spiroRequested;
    bool        reverseChain;
    int         chainAtomCount;
    std::string lastStatus;
    ActionState()
        : ringTool(0), chainTool(0), ringMode(true), spiroRequested(false),
          reverseChain(false), chainAtomCount(1) {}
};

// The T04 handler. userData must point at an ActionState that outlives the
// router registration.
interaction::RouterOutcome handleAction(void* userData,
                                        const interaction::ActionRequest& request);

// Register a handler for every action in actionBindings().
size_t registerActionHandlers(interaction::CommandRouter& router, ActionState* state);

} // namespace rings_chains
} // namespace r62

#endif // R62_NEXT_RINGS_CHAINS_TOOL_HPP