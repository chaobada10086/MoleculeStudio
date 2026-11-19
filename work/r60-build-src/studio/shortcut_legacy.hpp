#ifndef STUDIO_SHORTCUT_LEGACY_HPP
#define STUDIO_SHORTCUT_LEGACY_HPP
// Read-only migration of shortcut keys saved by releases before 2026-10-30.
// New writes use project IDs; old user bindings remain usable on first launch.
inline std::string legacyShortcutId(const std::string& id) {
    const std::string prefix="studio.";
    return id.compare(0,prefix.size(),prefix)==0 ? "cd26."+id.substr(prefix.size()) : id;
}
#endif
