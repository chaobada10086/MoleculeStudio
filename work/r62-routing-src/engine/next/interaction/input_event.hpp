#ifndef R62_NEXT_INTERACTION_INPUT_EVENT_HPP
#define R62_NEXT_INTERACTION_INPUT_EVENT_HPP
// =============================================================================
// re_round62_viewer/next/interaction/input_event.hpp
//
// T04 - PLATFORM-NEUTRAL INPUT VOCABULARY.
//
// The platform layer (T19 / the legacy WndProc) translates Win32 messages into
// the values in this header. NOTHING below this line knows about an HWND, a
// message id, WM_KEYDOWN or WM_CHAR, which is what makes the whole routing layer
// testable headlessly at 50,000 atoms.
//
// WHY THIS IS A SEPARATE HEADER FROM THE ROUTER
// -----------------------------------------------------------------------------
// The router can only be tested by *generating* input. If the event struct lived
// in router.hpp together with the router, a test would have to construct a
// window to test a hotkey. Splitting them means the acceptance criteria
// ("Undo/Fit/Open/New then a hotkey must not touch the old object", "typing text
// must not draw bonds") are proven by feeding a vector of InputEvent values.
//
// WINDOWS VIRTUAL KEY VALUES ARE USED AS THE KEY IDENTITY, deliberately:
//   * They are stable integers, not locale characters.
//   * The legacy editor and any future host adapter already have them.
//   * The mapping table records the OFFICIAL ChemDraw key; the fact that it is
//     expressible as a VK is an implementation detail, not the rule. keymap.hpp
//     carries the source column for every binding.
//
// CHARACTERS KEEP THEIR CASE. SOURCES_AND_LIMITS.md records that lowercase "c"
// means carbon while "l" or "Shift+c" means chlorine, and "b" means bromine
// while "Shift+b" means boron. Lowercasing a character before routing it is a
// defect, not a simplification, so InputEvent::character is stored exactly as
// delivered and modifiers are kept alongside it rather than folded into it.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/core/tool_registry.hpp"   // core::MouseButton / kButtonNone

#include <cstdint>
#include <string>
#include <vector>

namespace r62 {
namespace interaction {

// ---------------------------------------------------------------------------
// Version handshake (independent of core and render versions)
// ---------------------------------------------------------------------------
enum {
    kInteractionMajor    = 1,
    kInteractionMinor    = 0,
    kInteractionRevision = 0,
    kInteractionVersion  = kInteractionMajor * 10000 + kInteractionMinor * 100 + kInteractionRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestInteractionCompatibility(int major, int minor) {
    return major == kInteractionMajor && minor <= kInteractionMinor;
}

// ---------------------------------------------------------------------------
// Modifier bits
// ---------------------------------------------------------------------------
// DELIBERATELY NOT RE-DECLARED HERE. core::kModShift / kModControl / kModAlt /
// kModSpace are already the single definition in next/core/tool_registry.hpp,
// and this module reuses them verbatim.
//
// An earlier revision aliased them into r62::interaction, which made
// "kModShift" ambiguous at every call site that also did
// "using namespace r62::core" - including the test harness. Two names for one
// concept is exactly the kind of drift the versioned-core contract exists to
// prevent, so the aliases were removed rather than worked around in the tests.
//
// Call sites inside this module use "kModShift" after "using core::kModShift;"
// in the implementation files; downstream code should qualify as core::kModShift.
using core::kModNone;
using core::kModShift;
using core::kModControl;
using core::kModAlt;
using core::kModSpace;

// ---------------------------------------------------------------------------
// InputFocus: WHO owns the keystroke right now
// ---------------------------------------------------------------------------
// This enum is the mechanism behind the acceptance criterion "input method,
// text editing, hotkeys, menu focus and modal dialogs must be isolated from one
// another". A routing layer that consults a global "is a dialog open" flag from
// three places ends up with a hotkey firing behind a modal; an explicit focus
// token makes the isolation a property of the state machine instead.
//
// Rules enforced by InputStateMachine (see input_state.hpp):
//   * TextEntry      -> characters go to the text editor. Hotkeys MUST NOT run.
//   * ImeComposition -> every key is consumed by the IME. Hotkeys MUST NOT run.
//   * MenuOpen       -> only the menu's own commands run; no document hotkey.
//   * ModalDialog    -> only the dialog's commands run; no document hotkey.
//   * DragCapture    -> pointer movement belongs to the drag owner.
//   * Canvas         -> normal document routing.
enum class InputFocus : uint8_t {
    Canvas = 0,
    TextEntry,
    ImeComposition,
    MenuOpen,
    ModalDialog,
    DragCapture,
    HostOverlay
};

const char* inputFocusName(InputFocus focus);

// ---------------------------------------------------------------------------
// Input event kinds
// ---------------------------------------------------------------------------
enum class InputKind : uint8_t {
    PointerDown = 0,
    PointerMove,
    PointerUp,
    PointerWheel,
    KeyDown,
    KeyUp,
    Character,       // WM_CHAR equivalent: a printable character or backspace
    HotkeyCommand,   // an already-resolved platform command id (menu/toolbar)
    FocusLost,
    CaptureLost,
    Cancel,          // Esc-like: platform asks the state machine to unwind
    DocumentChanged, // new/open/undo/fit etc. invalidated the old hotspot
    ViewChanged
};

const char* inputKindName(InputKind kind);

// ---------------------------------------------------------------------------
// InputEvent
// ---------------------------------------------------------------------------
// One plain value. `devicePosition` is in DEVICE PIXELS (client coordinates, the
// same frame Win32 delivers); the machine converts to document space exactly
// once using the view it was given, because doing the conversion in two places
// is how a hotspot drifts by half a pixel between hover and click.
//
// For Character events `character` holds the UTF-16 code unit the platform
// delivered. It is NOT normalised: see the case-sensitivity note above.
struct InputEvent {
    InputKind   kind;
    InputFocus  focus;          // focus the platform believes is active
    core::Vec2  devicePosition; // device pixels
    uint32_t    modifiers;
    core::MouseButton button;
    double      wheelDelta;     // in wheel detents (1.0 == one notch)
    uint32_t    virtualKey;     // Windows VK_* value for KeyDown/KeyUp
    uint32_t    character;      // UTF-16 code unit for Character
    bool        repeat;         // KeyDown auto-repeat / held key
    uint32_t    command;        // platform command id for HotkeyCommand
    uint64_t    documentId;     // core::DocumentId the event was produced against
    uint64_t    viewGeneration; // caller's view revision; see stale detection
    uint64_t    timestampMicros;

    InputEvent()
        : kind(InputKind::PointerMove), focus(InputFocus::Canvas),
          modifiers(kModNone), button(core::kButtonNone), wheelDelta(0.0),
          virtualKey(0), character(0), repeat(false), command(0),
          documentId(0), viewGeneration(0), timestampMicros(0) {}
};

// ---------------------------------------------------------------------------
// Windows virtual key constants used by the keymap, declared here so the module
// compiles and tests on any toolchain without <windows.h>.
// ---------------------------------------------------------------------------
// Values match the Win32 headers exactly; the platform adapter checks them
// against the real ones at compile time (see PLATFORM_NOTES.md).
namespace vk {
enum : uint32_t {
    Back    = 0x08, Tab    = 0x09, Return = 0x0D, Escape = 0x1B, Space = 0x20,
    PageUp  = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24,
    Left    = 0x25, Up     = 0x26, Right = 0x27, Down = 0x28,
    Delete  = 0x2E,
    Key0 = 0x30, Key1 = 0x31, Key2 = 0x32, Key3 = 0x33, Key4 = 0x34,
    Key5 = 0x35, Key6 = 0x36, Key7 = 0x37, Key8 = 0x38, Key9 = 0x39,
    A = 0x41, B = 0x42, C = 0x43, D = 0x44, E = 0x45, F = 0x46, G = 0x47,
    H = 0x48, I = 0x49, J = 0x4A, K = 0x4B, L = 0x4C, M = 0x4D, N = 0x4E,
    O = 0x4F, P = 0x50, Q = 0x51, R = 0x52, S = 0x53, T = 0x54, U = 0x55,
    V = 0x56, W = 0x57, X = 0x58, Y = 0x59, Z = 0x5A,
    F1 = 0x70, F2 = 0x71, F3 = 0x72, F4 = 0x73, F5 = 0x74, F6 = 0x75,
    F7 = 0x76, F8 = 0x77, F9 = 0x78, F10 = 0x79, F11 = 0x7A, F12 = 0x7B,
    // Modifier keys, delivered as KeyDown/KeyUp. The machine treats them as LIVE
    // preview inputs rather than hotkeys, which is how "Alt changes the preview
    // immediately" is implemented without waiting for pointer movement.
    ShiftKey = 0x10, ControlKey = 0x11, MenuKey = 0x12
};
} // namespace vk

} // namespace interaction
} // namespace r62

#endif // R62_NEXT_INTERACTION_INPUT_EVENT_HPP
