#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "mode.hpp"

namespace hyprwsmode {

    // Apply a mode's effects to a window: mutate m_isFloating and manage
    // group membership. Idempotent: safe to call whether the window is
    // already in the target mode or not.
    //
    // postPlacement discriminates between two call contexts:
    //
    // - postPlacement=false is only for the window.openEarly path, where
    //   newTarget has not yet run. Setting m_isFloating directly steers
    //   newTarget into the right placement branch with no flash.
    //
    // - postPlacement=true is every other caller: window.open,
    //   moveToWorkspace, and the wsmode-toggle rebroadcast in
    //   applyModeToExistingWindows. Any floating-flag flip routes through
    //   CLayoutManager::changeFloatingMode so the tile tree stays in sync
    //   with the flag.
    void applyModeToWindow(PHLWINDOW w, WORKSPACEID wsId, const Mode& mode, bool postPlacement);

    // Register window.openEarly and window.moveToWorkspace listeners.
    // Must be called after registerListeners() so seeding entries exist
    // by the time the first window opens on a new workspace.
    void registerWindowListeners();

}  // namespace hyprwsmode
