#ifndef R62_NEXT_INTERACTION_ROUTER_HPP
#define R62_NEXT_INTERACTION_ROUTER_HPP
// =============================================================================
// re_round62_viewer/next/interaction/router.hpp
//
// T04 - THE SINGLE COMMAND ENTRY POINT.
//
// EVERY user action goes through CommandRouter::dispatch(): mouse, keyboard,
// toolbar buttons, menu items and the platform accelerator table. A module
// registers a HANDLER for an action; it never competes for WM_KEYDOWN/WM_CHAR.
//
// Why an explicit router rather than letting each tool check a key:
//   * "was this key consumed?" becomes a value the caller can assert on;
//   * an unimplemented action can report Unsupported in ONE place instead of
//     silently falling through into whatever the next tool happened to do;
//   * the routing decision is testable without a window.
//
// -----------------------------------------------------------------------------
// THREE OUTCOMES, NOT TWO
// -----------------------------------------------------------------------------
// The dangerous middle case is "the user asked for a real ChemDraw action that
// this build does not implement". Returning `Handled` there would lie, and
// returning `NotHandled` would let a tool hotkey steal the key. So the third
// outcome, `Unsupported`, is a first-class result: the caller reports it to the
// user and the document is NOT modified. The acceptance test asserts that an
// unsupported hotspot key leaves the document digest unchanged.
// -----------------------------------------------------------------------------

#include "hotspot.hpp"
#include "keymap.hpp"

#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace interaction {

// ---------------------------------------------------------------------------
// RouterOutcome
// ---------------------------------------------------------------------------
enum class RouterOutcome : uint8_t {
    NotHandled = 0,   // nothing claimed the action; the caller may use a default
    Handled,          // a registered handler ran (or the action is a no-op by design)
    Unsupported,      // a real ChemDraw action this build does not implement
    Rejected          // the action was understood but is illegal right now
};

const char* routerOutcomeName(RouterOutcome outcome);

// ---------------------------------------------------------------------------
// ActionRequest: what the router is being asked to do
// ---------------------------------------------------------------------------
// `action` is the context-resolved semantic action. `context`/`binding` are
// carried through so a handler (and the status line) knows WHY this action was
// chosen - the same HotkeyAction can arrive from a menu click or a hotkey.
struct ActionRequest {
    HotkeyAction  action;
    HotspotContext context;
    const HotkeyBinding* binding;  // null for menu/toolbar origins
    uint32_t      modifiers;
    core::Vec2    pointer;         // document coordinates, for pointer-origin actions
    Hotspot       hotspot;         // the hotspot the action was resolved against
    std::string   origin;          // "key", "menu", "toolbar", "char"
    ActionRequest()
        : action(HotkeyAction::None), context(HotspotContext::None), binding(0),
          modifiers(kModNone), origin("key") {}
};

// ---------------------------------------------------------------------------
// ActionHandler
// ---------------------------------------------------------------------------
// A handler returns the outcome it achieved. Returning Unsupported from a
// handler is normal and correct for a partially implemented editor.
typedef RouterOutcome (*ActionHandler)(void* userData, const ActionRequest& request);

// ---------------------------------------------------------------------------
// CommandRouter
// ---------------------------------------------------------------------------
class CommandRouter {
public:
    CommandRouter();

    // Register a handler for an action. Registering twice REPLACES, so a module
    // can upgrade a stub without a deregistration dance. A null handler removes
    // the registration.
    void registerHandler(HotkeyAction action, ActionHandler handler, void* userData);
    bool hasHandler(HotkeyAction action) const;
    const char* handlerOrigin(HotkeyAction action) const;

    // Tag the router's owner; used in the status line and the log.
    void setOrigin(const std::string& origin) { origin_ = origin; }
    const std::string& origin() const { return origin_; }

    // Dispatch. Returns Unsupported when no handler claimed a real ChemDraw
    // action, and NotHandled only for actions the router genuinely does not
    // know about.
    RouterOutcome dispatch(const ActionRequest& request);

    // Convenience: build a request and dispatch.
    RouterOutcome dispatch(HotkeyAction action, const std::string& origin,
                           uint32_t modifiers = kModNone);

    // --- accounting, so "what was dispatched" is observable ---------------
    struct Stats {
        size_t handled, unsupported, notHandled, rejected, dispatched;
        Stats() : handled(0), unsupported(0), notHandled(0), rejected(0), dispatched(0) {}
    };
    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = Stats(); }

    // The most recent unsupported action, for the status line. Empty when none.
    const std::string& lastUnsupported() const { return lastUnsupported_; }

    // Every registration, in registration order. Used by the coverage report.
    std::vector<HotkeyAction> registeredActions() const;

private:
    struct Entry {
        ActionHandler handler;
        void*         userData;
        std::string   originTag;
        Entry() : handler(0), userData(0) {}
    };
    std::map<HotkeyAction, Entry> handlers_;
    std::string origin_;
    std::string lastUnsupported_;
    Stats stats_;
};

// ---------------------------------------------------------------------------
// Helper: is this action a real ChemDraw action (as opposed to a no-op)?
// ---------------------------------------------------------------------------
// The router uses this to decide between Unsupported and NotHandled. A key the
// guide lists is Unsupported when unhandled; a key it does not list is
// NotHandled and the platform may pass it on.
bool isDocumentedAction(HotkeyAction action);

} // namespace interaction
} // namespace r62

#endif // R62_NEXT_INTERACTION_ROUTER_HPP
