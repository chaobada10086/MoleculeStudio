#ifndef R62_NEXT_INTERACTION_INPUT_STATE_HPP
#define R62_NEXT_INTERACTION_INPUT_STATE_HPP
// =============================================================================
// re_round62_viewer/next/interaction/input_state.hpp
//
// T04 - THE SHARED INPUT STATE MACHINE.
//
// All tools share ONE state machine. A tool does not keep its own 'is the mouse
// down' flag, because two such flags is how a tool ends up acting on a gesture
// that a different tool started.
//
// -----------------------------------------------------------------------------
// THE ACCEPTANCE CRITERIA THIS FILE EXISTS TO SATISFY
// -----------------------------------------------------------------------------
// 1. "Static mouse: Undo/Fit/Open/New then a hotkey must not modify an old
//    object."  -> a document/view change bumps a serial, and EVERY resolved
//    hotspot carries the serial it was computed at. A hotspot whose serial is
//    stale is discarded before any hotkey can read it. The old OBJECT ID cannot
//    reach a new document because the guard compares document identity too.
//
// 2. "Alt down/up updates the preview immediately." -> Alt is a live input to
//    the resolver; a KeyDown/KeyUp for VK_MENU (or any modifier) triggers a
//    preview refresh even though no pointer movement occurred.
//
// 3. "Typing text must not draw bonds." -> when focus is TextEntry or
//    ImeComposition, CHARACTER events are routed to the text sink and the
//    hotkey table is never consulted. This is enforced in one place.
//
// 4. "Long-pressing a sprout hotkey must not create branches repeatedly." ->
//    Character events with the repeat flag set are consumed as no-ops unless the
//    action explicitly opts into auto-repeat. The default is opt-OUT.
//
// 5. "Losing focus / losing capture / Esc / tool switch / closing a dialog
//    restores state." -> one unwind path, exercised by the tests for each of
//    those five triggers individually.
//
// 6. "A light drag preview must equal the commit." -> the resolved SnapResult is
//    stored in the machine and read back at release. Nothing recomputes it.
// -----------------------------------------------------------------------------

#include "input_event.hpp"
#include "keymap.hpp"
#include "router.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace interaction {

// ---------------------------------------------------------------------------
// GesturePhase
// ---------------------------------------------------------------------------
// The machine's own idea of what the pointer is doing. Deliberately smaller
// than the union of every tool's needs: a tool that requires a sub-phase keeps
// it privately, but the OUTER phase is shared so the router knows whether a
// release means 'commit' or 'ignore'.
enum class GesturePhase : uint8_t {
    Idle = 0,
    Armed,       // pressed on a hotspot, no movement past the click slop yet
    Dragging,    // moved past the click slop
    Marquee,     // pressed on empty space with the select/marquee tool
    Captured     // another owner took the capture (see InputFocus::DragCapture)
};

const char* gesturePhaseName(GesturePhase phase);

// ---------------------------------------------------------------------------
// TextSink: where characters go when focus is a text editor
// ---------------------------------------------------------------------------
// Injected so the input layer never links a control. The test uses an in-memory
// recorder and asserts that typing 'c' while editing text produced the character
// and did NOT create a bond.
class TextSink {
public:
    virtual ~TextSink() {}
    virtual void insertCharacter(uint32_t codeUnit) = 0;
    virtual void backspace() = 0;
    virtual void commit() = 0;
    virtual void cancel() = 0;
    virtual bool active() const = 0;
};

// ---------------------------------------------------------------------------
// PreviewPublisher: the seam between routing and rendering
// ---------------------------------------------------------------------------
// The state machine never draws. When the resolved preview changes it calls
// publish(), and the renderer (T03) consumes it. A no-op publisher is provided
// for headless tests, which is why the tests can run without a GL context while
// still asserting 'preview changed' / 'preview did not change'.
struct PreviewSnapshot {
    GesturePhase  phase;
    SnapResult    snap;          // THE value both preview and commit consume
    core::Vec2    origin;        // where the gesture started (document units)
    Hotspot       hotspot;
    uint32_t      modifiers;
    uint64_t      serial;        // increments on every published change
    PreviewSnapshot() : phase(GesturePhase::Idle), modifiers(kModNone), serial(0) {}
};

class PreviewPublisher {
public:
    virtual ~PreviewPublisher() {}
    // Called once per change. Never called for an unchanged snapshot.
    virtual void publish(const PreviewSnapshot& snapshot) = 0;
};

// A publisher that only counts. Used by the perf and routing tests.
class CountingPreviewPublisher : public PreviewPublisher {
public:
    CountingPreviewPublisher() : count_(0), lastChanged_(true) {}
    void publish(const PreviewSnapshot& snapshot) override {
        ++count_;
        last_ = snapshot;
        lastChanged_ = true;
    }
    size_t count() const { return count_; }
    const PreviewSnapshot& last() const { return last_; }
    void reset() { count_ = 0; lastChanged_ = false; }
    bool changedSinceReset() const { return lastChanged_; }
private:
    size_t          count_;
    PreviewSnapshot last_;
    bool            lastChanged_;
};

// ---------------------------------------------------------------------------
// InputStateMachine
// ---------------------------------------------------------------------------
// Owns: focus, gesture phase, the live hotspot + hysteresis, the pointer state
// and the resolved preview. Does NOT own the document, the tool or the view;
// those arrive per call, which is what keeps the machine testable in isolation.
class InputStateMachine {
public:
    InputStateMachine();

    // --- configuration -----------------------------------------------------
    void setProfile(const SnapProfile& profile);
    const SnapProfile& profile() const { return profile_; }
    void setRouter(CommandRouter* router) { router_ = router; }
    void setPreviewPublisher(PreviewPublisher* publisher) { publisher_ = publisher; }
    void setTextSink(TextSink* sink) { textSink_ = sink; }
    void setKeymap(const Keymap* keymap) { keymap_ = keymap; }

    // The document identity the machine is currently bound to. Changing it
    // INVALIDATES every live hotspot: this is the 'open/new/undo then hotkey
    // must not touch the old object' guarantee.
    void bindDocument(uint64_t documentId);
    uint64_t documentId() const { return documentId_; }
    // The view generation the current hotspot was computed at.
    void notifyViewChanged(uint64_t viewGeneration);
    // A document edit (undo/redo/geometry change). Bumps the geometry serial so
    // the retained hotspot cannot survive into a different document shape.
    void notifyDocumentChanged(uint64_t geometrySerial);

    // The view transform used to convert device pixels to document units. The
    // machine keeps a COPY; it is not the renderer's view object.
    void setView(const core::ViewTransform& view) { view_ = view; }
    const core::ViewTransform& view() const { return view_; }

    // --- focus -------------------------------------------------------------
    // Switching focus ALWAYS unwinds a gesture first, so a modal cannot be
    // opened mid-drag and then released into a stale commit.
    void setFocus(InputFocus focus);
    // Explicit view-generation update; equivalent to a ViewChanged event.
    void setViewGeneration(uint64_t generation);
    InputFocus focus() const { return focus_; }

    // --- the single entry point --------------------------------------------
    // Returns true when the event was consumed. `sink` is queried only for
    // pointer and hotspot work; a Character event never reaches it while a text
    // focus owns the keyboard.
    bool handle(const InputEvent& event, CandidateSink* sink);

    // --- observable state --------------------------------------------------
    GesturePhase phase() const { return phase_; }
    const Hotspot& hotspot() const { return hotspot_; }
    const PreviewSnapshot& preview() const { return preview_; }
    const core::Vec2& pointerDocument() const { return pointerDocument_; }
    uint32_t modifiers() const { return modifiers_; }
    core::ObjectRef pressedObject() const { return pressed_; }
    bool pointerInside() const { return pointerInside_; }
    // The SnapResult the CURRENT gesture will commit. Empty when idle.
    const SnapResult& pendingSnap() const { return preview_.snap; }

    struct Stats {
        size_t events;           // every event offered
        size_t consumed;         // events the machine claimed
        size_t hotspotsFound;
        size_t hotspotsRetained; // kept by hysteresis
        size_t snapJoins;        // resolved endpoints that joined an existing atom
        size_t unsupported;      // actions reported as unimplemented
        size_t suppressedRepeats;// auto-repeat keys that were dropped
        size_t textCharacters;   // characters routed to the text sink
        Stats() { reset(); }
        void reset() {
            events = consumed = hotspotsFound = hotspotsRetained = 0;
            snapJoins = unsupported = suppressedRepeats = textCharacters = 0;
        }
    };
    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = Stats(); }

    // --- lifecycle ---------------------------------------------------------
    // Unwind everything: gesture, preview, hotspot, hysteresis. Called on
    // FocusLost, CaptureLost, Cancel, tool switch and dialog close. Idempotent.
    void unwind(const std::string& reason);
    const std::string& lastUnwindReason() const { return unwindReason_; }
    size_t unwindCount() const { return unwindCount_; }

    // A tool switch is a focus-adjacent event but keeps the document binding.
    void notifyToolChanged(const std::string& toolId);
    const std::string& toolId() const { return toolId_; }

    // --- hotspot maintenance -----------------------------------------------
    // Recompute the hotspot at the current pointer. Called automatically after
    // pointer moves and modifier changes; exposed so the platform can force it
    // after it has itself changed the view.
    void refreshHotspot(CandidateSink* sink);

    // Query statistics for the most recent hotspot resolution. The perf test
    // reads this at 50,000 atoms to prove the query is local.
    const QueryStats& lastQueryStats() const { return queryStats_; }
    // Cumulative candidate acquisitions. A test that wants 'per mousemove' uses
    // the delta across a single event.
    size_t totalCandidateAcquisitions() const { return totalAcquisitions_; }

private:
    bool handlePointer(const InputEvent& event, CandidateSink* sink);
    bool handleKey(const InputEvent& event, CandidateSink* sink);
    bool handleCharacter(const InputEvent& event);
    bool handleCommand(const InputEvent& event);

    // Shared dispatch. Returns the router's outcome; NotHandled is the only value
    // a caller may treat as "someone else can have this key".
    RouterOutcome dispatchRequest(const ActionRequest& request);
    // Accelerator path. Kept separate from dispatchRequest so the accelerator a
    // menu item and a toolbar button all reach the router through the SAME call.
    RouterOutcome invokeAccelerator(const Accelerator& accelerator,
                                    const InputEvent& event);

    bool focusOwnsKeyboard() const;
    bool modifiersChanged(uint32_t next) const;
    void publishPreview();
    void resetPreview();
    bool resolveGesture(const InputEvent& event, CandidateSink* sink, SnapResult* out);
    bool staleHotspot(const Hotspot& hotspot) const;

    SnapProfile         profile_;
    CommandRouter*      router_;
    PreviewPublisher*   publisher_;
    TextSink*           textSink_;
    const Keymap*       keymap_;

    InputFocus          focus_;
    GesturePhase        phase_;
    core::ViewTransform view_;
    uint64_t            documentId_;
    uint64_t            geometrySerial_;
    uint64_t            viewGeneration_;
    std::string         toolId_;

    core::Vec2          pointerDocument_;
    core::Vec2          pressDocument_;
    bool                pointerInside_;
    uint32_t            modifiers_;
    core::ObjectRef     pressed_;
    bool                movedPastSlop_;

    Hotspot             hotspot_;
    HysteresisState     hysteresis_;
    HotspotKind         pressedKind_;

    PreviewSnapshot     preview_;
    uint64_t            previewSerial_;
    bool                previewDirty_;

    QueryStats          queryStats_;
    size_t              totalAcquisitions_;
    Stats               stats_;
    std::string         unwindReason_;
    size_t              unwindCount_;
};

} // namespace interaction
} // namespace r62

#endif // R62_NEXT_INTERACTION_INPUT_STATE_HPP
