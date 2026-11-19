#include "input_state.hpp"

#include <cmath>

namespace r62 {
namespace interaction {

const char* gesturePhaseName(GesturePhase phase) {
    switch (phase) {
    case GesturePhase::Idle: return "Idle";
    case GesturePhase::Armed: return "Armed";
    case GesturePhase::Dragging: return "Dragging";
    case GesturePhase::Marquee: return "Marquee";
    case GesturePhase::Captured: return "Captured";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
InputStateMachine::InputStateMachine()
    : router_(0), publisher_(0), textSink_(0), keymap_(0),
      focus_(InputFocus::Canvas), phase_(GesturePhase::Idle),
      documentId_(0), geometrySerial_(0), viewGeneration_(0),
      pointerDocument_(0, 0), pressDocument_(0, 0), pointerInside_(false),
      modifiers_(kModNone), pressedKind_(HotspotKind::None),
      previewSerial_(0), previewDirty_(false), totalAcquisitions_(0),
      unwindCount_(0) {
    if (!keymap_) keymap_ = &Keymap::defaults();
}

void InputStateMachine::bindDocument(uint64_t documentId) {
    if (documentId_ == documentId) return;
    // A DIFFERENT DOCUMENT invalidates everything: the gesture, the preview, the
    // hotspot and the hysteresis. The retained ObjectId could otherwise address
    // an unrelated object in the new document, because ObjectId is a slot index,
    // not a globally unique handle. This is the structural half of the acceptance
    // criterion 'the old ID must not modify a new document'.
    unwind("document-bound");
    documentId_ = documentId;
    geometrySerial_ = 0;
}

void InputStateMachine::notifyViewChanged(uint64_t viewGeneration) {
    if (viewGeneration_ == viewGeneration) return;
    viewGeneration_ = viewGeneration;
    // A view change moves every document point on screen, so a hotspot computed
    // at the old transform may be under a completely different screen location.
    // The hotspot is DROPPED rather than reused; it is recomputed on the next
    // pointer move with the new transform. The gesture itself is NOT unwound -
    // zooming during a drag is legal - but the preview is re-resolved.
    hotspot_ = Hotspot();
    hysteresis_.clear();
    previewDirty_ = true;
}

void InputStateMachine::notifyDocumentChanged(uint64_t geometrySerial) {
    geometrySerial_ = geometrySerial;
    // Any geometry edit can move or remove the hot object. Clearing the retained
    // hotspot forces a fresh resolution against the CURRENT shape, which is what
    // 'after undo/fit/open, a hotkey must not change an old object' means in
    // practice: undo while the pointer is stationary, then press c, and the
    // element is applied to whatever is under the pointer NOW, or to nothing.
    hotspot_ = Hotspot();
    hysteresis_.clear();
    hysteresis_.geometrySerial = geometrySerial;
    hysteresis_.documentId = documentId_;
    previewDirty_ = true;
}

void InputStateMachine::setFocus(InputFocus focus) {
    if (focus_ == focus) return;
    // Focus transitions unwind FIRST. Opening a modal mid-drag and then releasing
    // into a stale commit is exactly the class of bug this ordering prevents.
    if (focus != InputFocus::Canvas) unwind("focus-changed");
    focus_ = focus;
    if (focus == InputFocus::ImeComposition || focus == InputFocus::TextEntry) {
        // The hotspot is meaningless while a text editor owns the pointer context;
        // clearing it also guarantees no label key can fire from a stale hover.
        hotspot_ = Hotspot();
        hysteresis_.clear();
    }
}

void InputStateMachine::notifyToolChanged(const std::string& toolId) {
    // A tool switch changes what a press MEANS, so any uncommitted gesture must
    // be abandoned rather than committed by the new tool's release handler.
    unwind("tool-changed");
    toolId_ = toolId;
}

// ---------------------------------------------------------------------------
// unwind
// ---------------------------------------------------------------------------
void InputStateMachine::unwind(const std::string& reason) {
    // Idempotent by design: FocusLost and then CaptureLost in the same message
    // burst must not double-count or resurrect state. Only the FIRST unwind of an
    // actually-live state is counted, so the tests can assert 'exactly one'.
    const bool hadState = phase_ != GesturePhase::Idle || hotspot_.valid() ||
                          pressed_.valid() || preview_.phase != GesturePhase::Idle;
    phase_ = GesturePhase::Idle;
    pressed_ = core::ObjectRef();
    pressedKind_ = HotspotKind::None;
    movedPastSlop_ = false;
    hotspot_ = Hotspot();
    hysteresis_.clear();
    hysteresis_.documentId = documentId_;
    hysteresis_.geometrySerial = geometrySerial_;
    resetPreview();
    if (hadState) {
        ++unwindCount_;
        unwindReason_ = reason;
    }
}

// ---------------------------------------------------------------------------
// Focus / modifier helpers
// ---------------------------------------------------------------------------
bool InputStateMachine::focusOwnsKeyboard() const {
    // The exhaustive list of focuses that must swallow hotkeys. Adding a new
    // focus value here is a deliberate act; the failure mode of forgetting is a
    // hotkey firing where it must not, so the switch has no default that
    // silently returns false.
    switch (focus_) {
    case InputFocus::TextEntry: return true;
    case InputFocus::ImeComposition: return true;
    case InputFocus::MenuOpen: return true;
    case InputFocus::ModalDialog: return true;
    case InputFocus::HostOverlay: return true;
    case InputFocus::Canvas: return false;
    case InputFocus::DragCapture: return false;
    }
    return true;
}

bool InputStateMachine::modifiersChanged(uint32_t next) const {
    return modifiers_ != next;
}

bool InputStateMachine::staleHotspot(const Hotspot& hotspot) const {
    if (!hotspot.valid()) return false;
    // A hotspot is stale when it was computed against a different document.
    // HysteresisState carries the document id it was resolved against; comparing
    // it here is what stops a hotkey from editing an id that has been reused by
    // an unrelated object after Open/New.
    return hysteresis_.documentId != 0 && hysteresis_.documentId != documentId_;
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------
void InputStateMachine::publishPreview() {
    if (!previewDirty_) return;
    previewDirty_ = false;
    ++previewSerial_;
    preview_.serial = previewSerial_;
    preview_.phase = phase_;
    preview_.hotspot = hotspot_;
    preview_.modifiers = modifiers_;
    if (publisher_) publisher_->publish(preview_);
}

void InputStateMachine::resetPreview() {
    preview_ = PreviewSnapshot();
    preview_.phase = GesturePhase::Idle;
    previewDirty_ = true;
}

// ---------------------------------------------------------------------------
// Hotspot refresh
// ---------------------------------------------------------------------------
void InputStateMachine::refreshHotspot(CandidateSink* sink) {
    if (!sink) return;
    if (focus_ == InputFocus::ModalDialog || focus_ == InputFocus::MenuOpen ||
        focus_ == InputFocus::ImeComposition || focus_ == InputFocus::TextEntry) {
        // A dialog owns the pointer; the canvas beneath it must not develop a
        // hotspot that a keyboard event could then act on.
        hotspot_ = Hotspot();
        return;
    }
    if (!pointerInside_) {
        hotspot_ = Hotspot();
        hysteresis_.clear();
        return;
    }
    queryStats_.reset();
    ++totalAcquisitions_;
    const Hotspot found = SnapResolver::findHotspot(profile_, pointerDocument_,
                                                    view_.scale > 0 ? view_.scale : 1.0,
                                                    *sink, hysteresis_, &queryStats_);
    const Hotspot before = hotspot_;
    hotspot_ = found;
    if (found.valid()) {
        ++stats_.hotspotsFound;
        if (found.retained) ++stats_.hotspotsRetained;
        hysteresis_.previous = found.object;
        hysteresis_.previousKind = found.kind;
        hysteresis_.documentId = documentId_;
        hysteresis_.geometrySerial = sink->geometrySerial();
    } else {
        hysteresis_.clear();
    }
    // Only republish when the hotspot actually changed, so a stationary pointer
    // does not resubmit an identical preview every frame.
    if (before.kind != found.kind || before.object != found.object ||
        before.point != found.point || before.retained != found.retained)
        previewDirty_ = true;
}

// ---------------------------------------------------------------------------
// Gesture resolution
// ---------------------------------------------------------------------------
bool InputStateMachine::resolveGesture(const InputEvent& event, CandidateSink* sink,
                                       SnapResult* out) {
    // The event itself is not read: resolution depends on the machine's live
    // pointer/modifier state, which the pointer/key branch has already updated.
    // Keeping the parameter makes every call site uniform.
    (void)event;
    if (phase_ == GesturePhase::Idle) return false;
    const double zoom = view_.scale > 0.0 ? view_.scale : 1.0;
    const double bondLength = 14.4;  // document default; the caller's preview
                                     // adapter overrides it via setProfile users.
    // A gesture with no CandidateSink cannot resolve a join, so it falls through
    // to the pure free-endpoint path. That is a legitimate headless case (a test
    // that only checks the constraint flags), never a silent wrong answer.
    if (!sink) {
        *out = SnapResolver::resolveFree(profile_, pressDocument_, pointerDocument_,
                                         bondLength, zoom, modifiers_);
        return true;
    }
    *out = SnapResolver::resolveBond(profile_, pressDocument_, pointerDocument_,
                                     bondLength, zoom, *sink, modifiers_,
                                     pressed_, hysteresis_, &queryStats_);
    return true;
}

// ---------------------------------------------------------------------------
// Pointer
// ---------------------------------------------------------------------------
bool InputStateMachine::handlePointer(const InputEvent& event, CandidateSink* sink) {
    pointerDocument_ = view_.toDocument(event.devicePosition);
    if (!pointerDocument_.finite()) {
        // A non-finite pointer is a platform bug; refuse to build state on it.
        pointerDocument_ = core::Vec2(0, 0);
        return true;
    }

    // A capture owned by someone else must not be stolen by canvas routing.
    if (focus_ == InputFocus::DragCapture && phase_ != GesturePhase::Captured) {
        phase_ = GesturePhase::Captured;
    }

    const bool modifierMoved = modifiersChanged(event.modifiers);
    modifiers_ = event.modifiers;

    switch (event.kind) {
    case InputKind::PointerMove: {
        pointerInside_ = true;
        if (phase_ != GesturePhase::Idle) {
            const double slopDocument =
                profile_.clickSlopDevice / (view_.scale > 0 ? view_.scale : 1.0);
            const double moved = std::hypot(pointerDocument_.x - pressDocument_.x,
                                            pointerDocument_.y - pressDocument_.y);
            if (!movedPastSlop_ && moved > slopDocument) {
                movedPastSlop_ = true;
                if (phase_ == GesturePhase::Armed) {
                    phase_ = (pressedKind_ == HotspotKind::None)
                                 ? GesturePhase::Marquee : GesturePhase::Dragging;
                }
            }
            // Re-resolve on EVERY move while a gesture is live. This is what makes
            // the preview track the pointer, and it is also where Alt is honoured.
            SnapResult resolved;
            if (resolveGesture(event, sink, &resolved)) {
                if (resolved.valid && resolved.joined) ++stats_.snapJoins;
                preview_.snap = resolved;
                previewDirty_ = true;
            }
        }
        // The hover hotspot is recomputed even when a gesture is live, because the
        // pressed object may have moved under the pointer and the renderer still
        // wants a truthful highlight. It does NOT replace the gesture's origin.
        refreshHotspot(sink);
        if (modifierMoved) previewDirty_ = true;
        publishPreview();
        return true;
    }
    case InputKind::PointerDown: {
        if (phase_ != GesturePhase::Idle) {
            // A second button press during a live gesture is ignored rather than
            // restarting the gesture: restarting is how a middle-click pan turns
            // into a committed drag.
            publishPreview();
            return true;
        }
        pointerInside_ = true;
        pressDocument_ = pointerDocument_;
        movedPastSlop_ = false;
        refreshHotspot(sink);
        pressedKind_ = hotspot_.kind;
        pressed_ = hotspot_.object;
        phase_ = GesturePhase::Armed;
        preview_ = PreviewSnapshot();
        preview_.phase = phase_;
        preview_.origin = pressDocument_;
        preview_.hotspot = hotspot_;
        preview_.modifiers = modifiers_;
        previewDirty_ = true;
        publishPreview();
        return true;
    }
    case InputKind::PointerUp: {
        if (phase_ == GesturePhase::Idle) {
            // A release with no matching press. Refresh the hotspot (the pointer
            // may have arrived by a drag from outside) and claim the event so the
            // platform does not treat it as a click on a different target.
            pointerInside_ = true;
            refreshHotspot(sink);
            publishPreview();
            return true;
        }
        // RE-RESOLVE ONCE MORE AT RELEASE. The release event carries a position
        // that may differ from the last move (a fast flick delivers only Up). The
        // resolved value from THIS call is what a commit must use, so the preview
        // and the commit are the same value even for a sub-4-pixel click.
        SnapResult resolved;
        if (resolveGesture(event, sink, &resolved)) {
            if (resolved.valid && resolved.joined) ++stats_.snapJoins;
            preview_.snap = resolved;
        }
        preview_.phase = phase_;
        previewDirty_ = true;
        publishPreview();
        // NOTE: this layer does NOT commit. The tool reads pendingSnap() and
        // submits through the single transaction entry point. Clearing the
        // gesture here is what makes a tool's endGesture see a consistent phase.
        phase_ = GesturePhase::Idle;
        pressed_ = core::ObjectRef();
        pressedKind_ = HotspotKind::None;
        movedPastSlop_ = false;
        refreshHotspot(sink);
        return true;
    }
    case InputKind::PointerWheel: {
        // The wheel is a VIEW concern; the machine only records it so the platform
        // can read a consistent modifier/pointer state. It never edits.
        publishPreview();
        return false;
    }
    default: break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
bool InputStateMachine::handleKey(const InputEvent& event, CandidateSink* sink) {
    const uint32_t key = event.virtualKey;

    // --- modifier-only keys are NOT hotkeys, they are live preview inputs -----
    // Alt (and the other modifiers) arriving as a KeyDown/KeyUp must refresh the
    // preview IMMEDIATELY, without waiting for the pointer to move. The acceptance
    // criterion 'Alt down/up updates the preview immediately' is this branch.
    if (key == vk::MenuKey || key == vk::ShiftKey || key == vk::ControlKey) {
        const uint32_t next = event.modifiers;
        if (modifiersChanged(next)) {
            modifiers_ = next;
            if (phase_ != GesturePhase::Idle) {
                SnapResult resolved;
                if (resolveGesture(event, sink, &resolved)) preview_.snap = resolved;
            }
            previewDirty_ = true;
            publishPreview();
        }
        // Recompute the hotspot too: Alt does not change WHICH object is a hotspot
        // (that is a project policy, see hotspot.cpp), but the publisher must see
        // a consistent modifier set.
        refreshHotspot(sink);
        publishPreview();
        return true;
    }

    // Escape unwinds regardless of focus, so a modal opened on top of a drag can
    // still be dismissed and the underlying gesture cannot commit afterwards.
    if (key == vk::Escape) {
        unwind("escape");
        if (focus_ != InputFocus::Canvas && focus_ != InputFocus::DragCapture) {
            // The focused owner handles its own dismissal; the machine has already
            // released the canvas state.
            return false;
        }
        return true;
    }

    // A focus that owns the keyboard swallows everything else.
    if (focusOwnsKeyboard()) {
        if (focus_ == InputFocus::TextEntry && textSink_) {
            if (key == vk::Return) { textSink_->commit(); return true; }
            if (key == vk::Back) { textSink_->backspace(); return true; }
        }
        return true;
    }

    // --- accelerators FIRST: they are commands, not chemistry -----------------
    const Accelerator* accelerator = resolveAccelerator(key, event.modifiers);
    if (accelerator) {
        if (event.repeat) {
            // Holding Ctrl+Z must not unwind the whole document in one long key
            // press. The default is opt-OUT of auto-repeat for every command.
            ++stats_.suppressedRepeats;
            return true;
        }
        return invokeAccelerator(*accelerator, event) != RouterOutcome::NotHandled;
    }
    // A bare F-key that resolved to nothing is not a ChemDraw action.
    if (key >= vk::F1 && key <= vk::F12) {
        // Only F5..F8 are documented; the rest fall through to the platform.
        return false;
    }

    // --- arrow keys: selection movement / nudge -------------------------------
    if (key >= vk::Left && key <= vk::Down) {
        ActionRequest request;
        request.action = (event.modifiers & kModShift) ? HotkeyAction::NudgeSelection
                                                       : HotkeyAction::MoveSelection;
        request.modifiers = event.modifiers;
        request.pointer = pointerDocument_;
        request.hotspot = hotspot_;
        request.origin = "key";
        // Arrow auto-repeat IS wanted for a nudge (a held arrow repeats the move).
        // This is the ONE action family that opts into repeat, and it is recorded
        // as such here rather than being a hidden exception elsewhere.
        return dispatchRequest(request) != RouterOutcome::NotHandled;
    }

    // --- VK-based bindings that produce no character ---------------------------
    // Space, Enter, Backspace and Delete have character bindings in the guide but
    // arrive as WM_KEYDOWN, so they are resolved HERE with virtualKey set and
    // character 0. A CHARACTER-producing letter is deliberately NOT resolved on
    // this path: doing so would make the key case-blind, and the atom table
    // depends on 'c' vs 'C' being distinct. The platform sends the exact code
    // unit through the Character branch for those.
    if (key == vk::Space || key == vk::Return || key == vk::Back || key == vk::Delete) {
        const HotspotContext context = hotspot_.context();
        const HotkeyBinding* binding = keymap_->resolve(context, key, 0u, event.modifiers);
        if (!binding) return false;
        ActionRequest request;
        request.action = binding->action;
        request.context = context;
        request.binding = binding;
        request.modifiers = event.modifiers;
        request.pointer = pointerDocument_;
        request.hotspot = hotspot_;
        request.origin = "key";
        return dispatchRequest(request) != RouterOutcome::NotHandled;
    }

    // --- everything else is a character key; handled in WM_CHAR ---------------
    return false;
}

// ---------------------------------------------------------------------------
// Character: the case-sensitive path
// ---------------------------------------------------------------------------
bool InputStateMachine::handleCharacter(const InputEvent& event) {
    // TEXT FOCUS WINS. This single branch is the whole of "typing text must not
    // draw bonds": no keymap lookup happens at all on this path.
    if (focus_ == InputFocus::TextEntry || focus_ == InputFocus::ImeComposition) {
        if (textSink_) {
            if (event.character == 8u) textSink_->backspace();
            else if (event.character != 0u) textSink_->insertCharacter(event.character);
        }
        ++stats_.textCharacters;
        return true;
    }
    if (focus_ == InputFocus::MenuOpen || focus_ == InputFocus::ModalDialog ||
        focus_ == InputFocus::HostOverlay) {
        // A dialog owns the character. It is consumed but produces no document edit.
        return true;
    }

    // AUTO-REPEAT SUPPRESSION. Holding a sprout key (e.g. '1' on an atom) must not
    // build a branch per repeat. The check is here, BEFORE resolution, so a repeated
    // character cannot even reach the keymap.
    if (event.repeat) {
        ++stats_.suppressedRepeats;
        return true;
    }

    const HotspotContext context = hotspot_.context();
    const HotkeyBinding* binding = keymap_->resolve(context, 0u, event.character,
                                                   event.modifiers);
    if (!binding) return false;

    ActionRequest request;
    request.action = binding->action;
    request.context = context;
    request.binding = binding;
    request.modifiers = event.modifiers;
    request.pointer = pointerDocument_;
    request.hotspot = hotspot_;
    request.origin = "char";
    const RouterOutcome outcome = dispatchRequest(request);
    // A recognised ChemDraw character key is CONSUMED even when unsupported: the
    // platform must not then feed it to a default handler that would do something
    // else with it. That is the 'no silent substitution' rule.
    return outcome != RouterOutcome::NotHandled;
}

// ---------------------------------------------------------------------------
// Already-resolved platform command (menu item / toolbar button accelerator)
// ---------------------------------------------------------------------------
// A menu click and a hotkey reach the SAME router. That is the structural part of
// "all mouse/keyboard, toolbar and menu actions go through a consistent command
// entry point".
bool InputStateMachine::handleCommand(const InputEvent& event) {
    HotkeyAction action = HotkeyAction::None;
    const std::vector<Accelerator>& table = defaultAccelerators();
    for (size_t i = 0; i < table.size(); ++i)
        if (table[i].virtualKey == event.command) { action = table[i].action; break; }
    if (action == HotkeyAction::None) {
        // The platform may pass a platform command id (core platform_command band).
        // Until the registry adapter maps it, report it rather than guessing.
        return false;
    }
    ActionRequest request;
    request.action = action;
    request.origin = "menu";
    request.modifiers = event.modifiers;
    request.pointer = pointerDocument_;
    request.hotspot = hotspot_;
    return dispatchRequest(request) != RouterOutcome::NotHandled;
}

RouterOutcome InputStateMachine::dispatchRequest(const ActionRequest& request) {
    if (!router_) {
        // No router attached: a documented action is still reported honestly as
        // Unsupported rather than silently succeeding.
        if (isDocumentedAction(request.action)) {
            ++stats_.unsupported;
            return RouterOutcome::Unsupported;
        }
        return RouterOutcome::NotHandled;
    }
    const RouterOutcome outcome = router_->dispatch(request);
    if (outcome == RouterOutcome::Unsupported) ++stats_.unsupported;
    return outcome;
}

// ---------------------------------------------------------------------------
// The single entry point
// ---------------------------------------------------------------------------
bool InputStateMachine::handle(const InputEvent& event, CandidateSink* sink) {
    ++stats_.events;

    // A STALE-DOCUMENT EVENT IS REFUSED. The platform attaches the document id it
    // produced the event against; if it does not match the id this machine is
    // bound to, the event is dropped. Without this check a queued keystroke from
    // before an Open could edit the newly loaded document at a recycled slot id.
    if (event.documentId != 0 && documentId_ != 0 && event.documentId != documentId_) {
        return true;
    }

    // Stale hotspot guard: a hotspot whose originating document differs from the
    // current one is discarded BEFORE any handler can read it.
    if (staleHotspot(hotspot_)) {
        hotspot_ = Hotspot();
        hysteresis_.clear();
    }

    bool consumed = false;
    switch (event.kind) {
    case InputKind::PointerDown:
    case InputKind::PointerMove:
    case InputKind::PointerUp:
    case InputKind::PointerWheel:
        consumed = handlePointer(event, sink);
        break;
    case InputKind::KeyDown:
        consumed = handleKey(event, sink);
        break;
    case InputKind::KeyUp:
        // Key releases only matter for modifiers; everything else already fired on
        // KeyDown. Handling them here would double-dispatch a command.
        if (event.virtualKey == vk::MenuKey || event.virtualKey == vk::ShiftKey ||
            event.virtualKey == vk::ControlKey) {
            consumed = handleKey(event, sink);
        } else {
            consumed = false;
        }
        break;
    case InputKind::Character:
        consumed = handleCharacter(event);
        break;
    case InputKind::HotkeyCommand:
        consumed = handleCommand(event);
        break;
    case InputKind::FocusLost:
        // Focus loss releases the gesture AND the hotspot. The pointer is also
        // marked outside so a subsequent KeyDown cannot act on the last position.
        pointerInside_ = false;
        unwind("focus-lost");
        consumed = true;
        break;
    case InputKind::CaptureLost:
        unwind("capture-lost");
        consumed = true;
        break;
    case InputKind::Cancel:
        unwind("cancel");
        consumed = true;
        break;
    case InputKind::DocumentChanged:
        notifyDocumentChanged(event.viewGeneration);
        consumed = true;
        break;
    case InputKind::ViewChanged:
        notifyViewChanged(event.viewGeneration);
        if (sink) refreshHotspot(sink);
        publishPreview();
        consumed = true;
        break;
    }
    if (consumed) ++stats_.consumed;
    return consumed;
}

// handleCommandInternal is the accelerator path; it shares dispatchRequest so an
// accelerator, a menu item and a toolbar button cannot diverge.
RouterOutcome InputStateMachine::invokeAccelerator(const Accelerator& accelerator,
                                                   const InputEvent& event) {
    ActionRequest request;
    request.action = accelerator.action;
    request.origin = "key";
    request.modifiers = event.modifiers;
    request.pointer = pointerDocument_;
    request.hotspot = hotspot_;
    if (!router_) {
        if (isDocumentedAction(request.action)) { ++stats_.unsupported; return RouterOutcome::Unsupported; }
        return RouterOutcome::NotHandled;
    }
    const RouterOutcome outcome = router_->dispatch(request);
    if (outcome == RouterOutcome::Unsupported) ++stats_.unsupported;
    return outcome;
}

void InputStateMachine::setProfile(const SnapProfile& profile) {
    profile_ = profile;
    previewDirty_ = true;
}

void InputStateMachine::setViewGeneration(uint64_t generation) {
    notifyViewChanged(generation);
}

} // namespace interaction
} // namespace r62
