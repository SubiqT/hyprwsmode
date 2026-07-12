#pragma once

#include <array>

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

// The handle Hyprland gives us at PLUGIN_INIT. Required by every
// HyprlandAPI:: call. Stored inline so any translation unit can reach
// it via #include "globals.hpp".
inline HANDLE PHANDLE = nullptr;

namespace hyprwsmode {

    // Maximum per-workspace overrides registered in PLUGIN_INIT. Bumping
    // this and registering more keyword pairs is the one-line extension
    // point mentioned in the design's Scope section. Special (negative-id)
    // workspaces and any workspace id outside 1..MAX_CONFIGURED_WS fall
    // through to the global defaults, which is the desired behaviour.
    inline constexpr size_t MAX_CONFIGURED_WS = 9;

    struct SConfigValues {
        // Global defaults.
        SP<Config::Values::CStringValue> defaultManaged;      // "managed" | "float"
        SP<Config::Values::CStringValue> defaultManagedType;  // "tile"    | "stack"

        // Per-workspace overrides. Empty string means "inherit global".
        // Indexed 0..MAX_CONFIGURED_WS-1 for workspace ids 1..MAX_CONFIGURED_WS.
        std::array<SP<Config::Values::CStringValue>, MAX_CONFIGURED_WS> defaultManagedN;
        std::array<SP<Config::Values::CStringValue>, MAX_CONFIGURED_WS> defaultManagedTypeN;
    };

    inline SConfigValues g_config = {};

}  // namespace hyprwsmode
