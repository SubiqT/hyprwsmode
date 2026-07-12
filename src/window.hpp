#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "mode.hpp"

namespace hyprwsmode {

    // Apply a mode's effects to a window: mutate m_isFloating and manage
    // group membership. Idempotent: safe to call whether the window is
    // already in the target mode or not. Used by both the openEarly
    // listener (for new windows, where the window has no existing
    // group/float state) and moveToWorkspace (where it may). Handles
    // both cases uniformly.
    void applyModeToWindow(PHLWINDOW w, WORKSPACEID wsId, const Mode& mode);

    // Register window.openEarly and window.moveToWorkspace listeners.
    // Must be called after registerListeners() so seeding entries exist
    // by the time the first window opens on a new workspace.
    void registerWindowListeners();

}  // namespace hyprwsmode
