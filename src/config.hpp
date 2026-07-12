#pragma once

#include <hyprland/src/SharedDefs.hpp>

#include "mode.hpp"

namespace hyprwsmode {

    // Register the twenty config keywords in PLUGIN_INIT. Must be called
    // before HyprlandAPI::reloadConfig() so the values pick up any
    // user-supplied hyprland.conf lines on first parse.
    void registerConfig();

    // Resolve the effective default mode for a workspace, following the
    // two-axis resolution described in the design:
    //   managed(N)  = default_managed_N       if non-empty, else default_managed
    //   type(N)     = default_managed_type_N  if non-empty, else default_managed_type
    // and combining them into a Mode.
    Mode        resolveDefault(WORKSPACEID id);
    ManagedType resolveDefaultType(WORKSPACEID id);

}  // namespace hyprwsmode
