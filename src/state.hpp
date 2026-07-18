#pragma once

#include <unordered_map>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>

#include "mode.hpp"

namespace hyprwsmode {

    struct SWorkspaceRuntime {
        Mode        current;
        ManagedType lastManaged = ManagedType::Tile;
    };

    // Mode intent per workspace. Populated at workspace.created (or
    // defensively on first window open) and mutated by dispatchers.
    // Preserved across config.reloaded so runtime toggles survive.
    // Cleared per-workspace on workspace.removed. See design.md's
    // "State model" and "What config.reloaded does not do".
    inline std::unordered_map<WORKSPACEID, SWorkspaceRuntime> g_workspaceModes;

    // Group-handle cache. Not a source of truth about mode intent; the
    // mode intent is g_workspaceModes. Weak pointer because Hyprland
    // owns the group's lifetime.
    inline std::unordered_map<WORKSPACEID, WP<Desktop::View::CGroup>> g_workspaceGroups;

    // Ensure g_workspaceModes has an entry for id, seeding from current
    // config if not already present. Safe to call from any listener
    // path. Returns a reference to the entry.
    SWorkspaceRuntime& seedFor(WORKSPACEID id);

    // Emit "wsmode>>id,<mode>" on socket2 for the workspace's current
    // effective mode. No-op if the workspace has no entry.
    void emit(WORKSPACEID id);

    // Emit "wsmode>>id,<mode>" for every workspace this plugin tracks,
    // without changing any state. Used both from the config.reloaded
    // listener and from the `wsmode broadcast` dispatcher so bar
    // widgets started after Hyprland can request a fresh snapshot.
    void broadcastAll();

    // Register workspace.created, workspace.removed, and
    // config.reloaded listeners plus an initial seeding pass over
    // Hyprland's existing workspaces at PLUGIN_INIT time.
    void registerListeners();

}  // namespace hyprwsmode
