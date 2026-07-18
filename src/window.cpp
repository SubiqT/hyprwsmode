#include "window.hpp"

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>

#include "log.hpp"
#include "mode.hpp"
#include "state.hpp"

namespace hyprwsmode {

    namespace {
        // Signal handles must be kept alive; see state.cpp for rationale.
        CHyprSignalListener g_openEarlyListener;
        CHyprSignalListener g_openListener;
        CHyprSignalListener g_moveToWorkspaceListener;

        // Flip a window's floating wire flag.
        //
        // - postPlacement=false is the openEarly path: newTarget has not
        //   run yet, so set m_isFloating directly. newTarget then branches
        //   into the floating placement path from that flag with no
        //   visible flash.
        //
        // - postPlacement=true is every other path (open, moveToWorkspace,
        //   applyModeToExistingWindows on wsmode toggle). The window is
        //   already in the layout, so routing the flip through
        //   CLayoutManager::changeFloatingMode is the only way to keep
        //   the tile tree consistent (dwindle picks up windows leaving
        //   float and drops windows entering it). Setting m_isFloating
        //   directly here would leave a window that just left float at
        //   its old floating position until an unrelated event forced a
        //   layout recompute.
        void setFloatingWireState(PHLWINDOW w, bool floating, bool postPlacement) {
            if (w->m_isFloating == floating)
                return;

            if (postPlacement) {
                if (auto target = w->layoutTarget()) {
                    g_layoutManager->changeFloatingMode(target);
                    return;
                }
            }

            w->m_isFloating = floating;
        }
    }

    void applyModeToWindow(PHLWINDOW w, WORKSPACEID wsId, const Mode& mode, bool postPlacement) {
        if (!w)
            return;

        // Snapshot the workspace's cached group (may be null or expired).
        auto& groupRef      = g_workspaceGroups[wsId];
        auto  expectedGroup = groupRef.lock();

        std::visit(overloaded{
                       [&](const Floating&) {
                           // Float: leave any group, then set floating. remove()
                           // resets w->m_group internally, no need to null it here.
                           if (w->m_group)
                               w->m_group->remove(w);
                           setFloatingWireState(w, true, postPlacement);
                       },
                       [&](const Managed& m) {
                           setFloatingWireState(w, false, postPlacement);

                           if (m.type == ManagedType::Tile) {
                               if (w->m_group)
                                   w->m_group->remove(w);
                               return;
                           }

                           // Managed::Stack: ensure the window is in the
                           // workspace's stack group.

                           // Already in the right group. Nothing to do.
                           if (w->m_group && w->m_group == expectedGroup)
                               return;

                           // In some other group. Detach first so add() below
                           // doesn't try to move that group's other windows.
                           if (w->m_group)
                               w->m_group->remove(w);

                           // Reuse the cached group if it's still alive and
                           // has members. Otherwise create a new one and store
                           // the handle.
                           if (expectedGroup && expectedGroup->size() > 0) {
                               expectedGroup->add(w);
                           } else {
                               auto fresh = Desktop::View::CGroup::create({w});
                               if (!fresh) {
                                   log::warn("CGroup::create returned null for workspace {}", wsId);
                                   return;
                               }
                               groupRef = fresh;
                           }
                       },
                   },
                   mode);
    }

    void registerWindowListeners() {
        // Split the openEarly path by mode:
        //
        // - Floating: safe pre-placement. Setting m_isFloating = true before
        //   g_layoutManager->newTarget (Window.cpp:2136) lets the layout
        //   branch into its floating path directly, no flash.
        //
        // - Managed{Stack}: NOT safe pre-placement. CGroup::add expects
        //   w->layoutTarget()->space() to be set, which requires newTarget
        //   to have run first. Calling add at openEarly on the second window
        //   crashes. Defer to window.open (fires at Window.cpp:2265, after
        //   newTarget and updateWindowData). Trade-off documented: a brief
        //   frame where the second+ stack window may render tiled before
        //   joining the group.
        //
        // - Managed{Tile}: no-op in either listener, dwindle takes over.
        g_openEarlyListener = Event::bus()->m_events.window.openEarly.listen(
            [](PHLWINDOW w) {
                if (!w || !w->m_workspace)
                    return;

                const WORKSPACEID id = w->m_workspace->m_id;
                auto&             rt = seedFor(id);

                if (std::holds_alternative<Floating>(rt.current))
                    applyModeToWindow(w, id, rt.current, /*postPlacement=*/false);
            });

        g_openListener = Event::bus()->m_events.window.open.listen(
            [](PHLWINDOW w) {
                if (!w || !w->m_workspace)
                    return;

                const WORKSPACEID id = w->m_workspace->m_id;
                auto&             rt = seedFor(id);

                if (!std::holds_alternative<Managed>(rt.current))
                    return;
                if (std::get<Managed>(rt.current).type != ManagedType::Stack)
                    return;

                applyModeToWindow(w, id, rt.current, /*postPlacement=*/true);
            });

        // moveToWorkspace(w, ws) fires after the window has been fully placed
        // on the source workspace, so calling applyModeToWindow with its
        // group manipulation is safe here even for stack mode.
        g_moveToWorkspaceListener = Event::bus()->m_events.window.moveToWorkspace.listen(
            [](PHLWINDOW w, PHLWORKSPACE ws) {
                if (!w || !ws)
                    return;

                const WORKSPACEID id = ws->m_id;
                auto&             rt = seedFor(id);
                applyModeToWindow(w, id, rt.current, /*postPlacement=*/true);
            });
    }

}  // namespace hyprwsmode
