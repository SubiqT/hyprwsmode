#include "window.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "mode.hpp"
#include "state.hpp"

namespace hyprwsmode {

    namespace {
        // Signal handles must be kept alive; see state.cpp for rationale.
        CHyprSignalListener g_openEarlyListener;
        CHyprSignalListener g_moveToWorkspaceListener;
    }

    void applyModeToWindow(PHLWINDOW w, WORKSPACEID wsId, const Mode& mode) {
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
                           w->m_isFloating = true;
                       },
                       [&](const Managed& m) {
                           w->m_isFloating = false;

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
                                   Log::logger->log(Log::WARN,
                                                    "[hyprwsmode] CGroup::create returned null for workspace {}",
                                                    wsId);
                                   return;
                               }
                               groupRef = fresh;
                           }
                       },
                   },
                   mode);
    }

    void registerWindowListeners() {
        // openEarly fires in CWindow::mapWindow at Window.cpp:2124 (v0.55.4),
        // after window rules have set m_isFloating and before the auto-group
        // check, layout newTarget, and the m_isFloating-based sizing branch.
        // Writing m_isFloating and m_group here is respected by the code
        // that follows.
        g_openEarlyListener = Event::bus()->m_events.window.openEarly.listen(
            [](PHLWINDOW w) {
                Log::logger->log(Log::WARN,
                                 "[hyprwsmode] openEarly fired for window={}",
                                 (void*)w.get());
                if (!w || !w->m_workspace)
                    return;

                const WORKSPACEID id = w->m_workspace->m_id;
                auto& rt = seedFor(id);
                Log::logger->log(Log::WARN,
                                 "[hyprwsmode] openEarly ws={} mode={}",
                                 id, formatMode(rt.current));
                applyModeToWindow(w, id, rt.current);
            });

        // moveToWorkspace(w, ws) fires when a window is moved to a
        // different workspace. Payload is (PHLWINDOW, PHLWORKSPACE), the
        // destination workspace. Apply the destination's mode.
        g_moveToWorkspaceListener = Event::bus()->m_events.window.moveToWorkspace.listen(
            [](PHLWINDOW w, PHLWORKSPACE ws) {
                if (!w || !ws)
                    return;

                const WORKSPACEID id = ws->m_id;
                auto& rt = seedFor(id);
                applyModeToWindow(w, id, rt.current);
            });
    }

}  // namespace hyprwsmode
