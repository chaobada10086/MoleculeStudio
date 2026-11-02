#include "router.hpp"

#include <algorithm>

namespace r62 {
namespace interaction {

const char* routerOutcomeName(RouterOutcome outcome) {
    switch (outcome) {
    case RouterOutcome::NotHandled: return "NotHandled";
    case RouterOutcome::Handled: return "Handled";
    case RouterOutcome::Unsupported: return "Unsupported";
    case RouterOutcome::Rejected: return "Rejected";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// isDocumentedAction
// ---------------------------------------------------------------------------
// WHICH ACTIONS COUNT AS 'A REAL CHEMDRAW ACTION'.
//
// The rule: if the guide assigns a meaning to the key in this context, then an
// unhandled outcome must be Unsupported - the user asked for something real and
// must be told it is not built. If the guide says nothing, the outcome is
// NotHandled and the platform is free to pass the key on.
//
// This split is what prevents the failure the task names: '未实现行为返回明确结果，
// 不偷换成另一个工具' - an unimplemented behaviour returns a clear result rather
// than being quietly swapped for a different tool.
bool isDocumentedAction(HotkeyAction action) {
    switch (action) {
    case HotkeyAction::None: return false;
    case HotkeyAction::Ambiguous: return true;
    case HotkeyAction::Unsupported: return true;
    default: break;
    }
    // Everything else in the vocabulary IS a documented ChemDraw action: the
    // enum was built from the guide tables (see keymap.hpp for the citation per
    // row) plus a small explicitly-labelled project set (FileNew/Open/Save/As,
    // MoveSelection). Returning true here means 'the user asked for this on
    // purpose', which is precisely the condition for reporting Unsupported.
    return true;
}

// ---------------------------------------------------------------------------
// CommandRouter
// ---------------------------------------------------------------------------
CommandRouter::CommandRouter() : origin_("interaction") {}

void CommandRouter::registerHandler(HotkeyAction action, ActionHandler handler,
                                    void* userData) {
    if (!handler) {
        handlers_.erase(action);
        return;
    }
    Entry entry;
    entry.handler = handler;
    entry.userData = userData;
    entry.originTag = origin_;
    handlers_[action] = entry;
}

bool CommandRouter::hasHandler(HotkeyAction action) const {
    return handlers_.find(action) != handlers_.end();
}

const char* CommandRouter::handlerOrigin(HotkeyAction action) const {
    std::map<HotkeyAction, Entry>::const_iterator it = handlers_.find(action);
    return it == handlers_.end() ? "" : it->second.originTag.c_str();
}

RouterOutcome CommandRouter::dispatch(const ActionRequest& request) {
    ++stats_.dispatched;
    // AN AMBIGUOUS GUIDE BINDING IS NEVER GUESSED. The guide assigns 'k' two
    // different meanings depending on a context this module cannot resolve (the
    // excerpt says so explicitly). Even if a handler somehow registered for it,
    // dispatching would be inventing a rule, so it short-circuits to Unsupported
    // here - in the router, where no handler can bypass it.
    if (request.action == HotkeyAction::AmbiguousGuideBinding) {
        ++stats_.unsupported;
        lastUnsupported_ = hotkeyActionName(request.action);
        return RouterOutcome::Unsupported;
    }
    std::map<HotkeyAction, Entry>::const_iterator it = handlers_.find(request.action);
    if (it != handlers_.end() && it->second.handler) {
        const RouterOutcome outcome =
            it->second.handler(it->second.userData, request);
        switch (outcome) {
        case RouterOutcome::Handled:     ++stats_.handled;     break;
        case RouterOutcome::Unsupported: ++stats_.unsupported; break;
        case RouterOutcome::Rejected:    ++stats_.rejected;    break;
        case RouterOutcome::NotHandled:  ++stats_.notHandled;  break;
        }
        if (outcome == RouterOutcome::Unsupported)
            lastUnsupported_ = hotkeyActionName(request.action);
        return outcome;
    }
    // NO HANDLER.
    // A documented action with no handler must NOT silently fall through into
    // whatever the next consumer happens to do. It is Unsupported, loudly.
    if (isDocumentedAction(request.action)) {
        ++stats_.unsupported;
        lastUnsupported_ = hotkeyActionName(request.action);
        return RouterOutcome::Unsupported;
    }
    ++stats_.notHandled;
    return RouterOutcome::NotHandled;
}

RouterOutcome CommandRouter::dispatch(HotkeyAction action, const std::string& origin,
                                      uint32_t modifiers) {
    ActionRequest request;
    request.action = action;
    request.origin = origin;
    request.modifiers = modifiers;
    return dispatch(request);
}

std::vector<HotkeyAction> CommandRouter::registeredActions() const {
    std::vector<HotkeyAction> out;
    out.reserve(handlers_.size());
    for (std::map<HotkeyAction, Entry>::const_iterator it = handlers_.begin();
         it != handlers_.end(); ++it)
        out.push_back(it->first);
    return out;
}

} // namespace interaction
} // namespace r62
