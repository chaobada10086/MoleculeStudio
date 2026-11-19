#ifndef R62_T19_RIBBON_MODEL_HPP
#define R62_T19_RIBBON_MODEL_HPP
// Ribbon information architecture for the T19 assembly. The model is data, not
// behaviour: every entry names the real source command or router action it maps
// to, plus a disposition. Nothing here may claim a capability the assembly does
// not have - the view disables and explains Unsupported rows.
#include "ribbon_icons.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace r62 { namespace integration { namespace ribbon {

enum class ItemKind : uint8_t {
    Large,      // 48x48 icon + label below
    Small,      // 24x24 icon + label right
    Split,      // large button with a dropdown arrow (default action + menu)
    Gallery,    // dropdown that opens a grid of style presets
    Toggle,     // small checkable button
    Separator,  // vertical divider
    Label       // static caption
};

// Where an item's action goes. Every kind ultimately enters the single
// CommandRouter / Session so buttons, menus and hotkeys share one path.
enum class SourceKind : uint8_t {
    SourceCommand,  // Session::activateCommand (one of the 275)
    Action,         // interaction::HotkeyAction through CommandRouter
    FileOp,         // New/Open/Save/SaveAs/Export (also through the router)
    PropertyOp,     // selection property edit submitted as a core::Command
    Unsupported     // recognised feature with no accepted implementation
};

enum class Disp : uint8_t {
    Implemented, Partial, Unsupported, NativeBacked, Unverified
};
const char* dispName(Disp d);

struct Item {
    std::string id;         // stable unique id
    std::string label;      // Chinese display name
    std::string sublabel;   // short qualifier, may be empty
    std::string tooltip;    // hover explanation
    std::string hotkey;     // display accelerator, empty when none
    std::string command;    // source command (SourceCommand) or property key
    std::string owner;      // module that owns the behaviour
    std::string reason;     // why Unsupported/Partial
    ItemKind    kind;
    SourceKind  source;
    Disp        disp;
    int icon;
    int iconVariant;
    int action;             // HotkeyAction value for SourceKind::Action
    int value;              // parameter for PropertyOp / FileOp
    bool checkable;
    std::vector<Item> children;
    Item()
        : kind(ItemKind::Small), source(SourceKind::Unsupported),
          disp(Disp::Unverified), icon(IconNone), iconVariant(0), action(0),
          value(0), checkable(false) {}
};

struct Group { std::string title; std::vector<Item> items; };
struct Tab   { std::string title; std::vector<Group> groups; };
struct Model { std::vector<Tab> tabs; };

// Built once from the generated tools.csv table plus the curated groups.
const Model& ribbonModel();

// Chinese label for a source command, derived from its category and a reviewed
// token dictionary. Never returns an empty string; unknown tokens fall through
// so the real command name stays visible in the tooltip.
std::string zhLabelForCommand(const std::string& command, const std::string& category);
std::string zhCategory(const std::string& category);
// Semantic icon for a source command; writes a variant where relevant.
int iconForCommand(const std::string& command, int* variant);

}}}
#endif
