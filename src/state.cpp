#include "state.hpp"

#include <format>
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/EventManager.hpp>

#include "config.hpp"
#include "mode.hpp"

namespace hyprwsmode {

    namespace {
        // Signal handles kept alive so the listeners stay registered for
        // the plugin's lifetime. The CHyprSignalListener return from
        // Signal::listen() is [[nodiscard]]: losing the handle
        // unregisters the callback immediately.
        CHyprSignalListener g_workspaceCreatedListener;
        CHyprSignalListener g_workspaceRemovedListener;
        CHyprSignalListener g_configReloadedListener;
    }

    SWorkspaceRuntime& seedFor(WORKSPACEID id) {
        auto it = g_workspaceModes.find(id);
        if (it != g_workspaceModes.end())
            return it->second;

        auto [inserted, _] = g_workspaceModes.emplace(
            id,
            SWorkspaceRuntime{
                .current     = resolveDefault(id),
                .lastManaged = resolveDefaultType(id),
            });
        return inserted->second;
    }

    void emit(WORKSPACEID id) {
        auto it = g_workspaceModes.find(id);
        if (it == g_workspaceModes.end())
            return;

        // g_pEventManager is inline UP<CEventManager> at
        // src/managers/EventManager.hpp:44. Guard defensively for the
        // shutdown window even though listeners shouldn't fire then.
        if (!g_pEventManager) {
            Log::logger->log(Log::WARN,
                             "[hyprwsmode] emit({}): g_pEventManager is null, skipping",
                             id);
            return;
        }

        g_pEventManager->postEvent(SHyprIPCEvent{
            .event = "wsmode",
            .data  = std::format("{},{}", id, formatMode(it->second.current)),
        });
    }

    void registerListeners() {
        // Seed any workspaces Hyprland already knows about at plugin
        // load time. This covers hyprctl plugin load after the
        // compositor is up. Under normal Nix + exec-once = hyprpm
        // reload the compositor starts with zero workspaces, so this
        // loop is a no-op then.
        for (const auto& w : g_pCompositor->getWorkspaces()) {
            const WORKSPACEID id = w->m_id;
            seedFor(id);
            emit(id);
        }

        g_workspaceCreatedListener = Event::bus()->m_events.workspace.created.listen(
            [](PHLWORKSPACEREF wref) {
                const auto w = wref.lock();
                if (!w) return;
                const WORKSPACEID id = w->m_id;
                seedFor(id);
                emit(id);
            });

        g_workspaceRemovedListener = Event::bus()->m_events.workspace.removed.listen(
            [](PHLWORKSPACEREF wref) {
                const auto w = wref.lock();
                if (!w) return;
                const WORKSPACEID id = w->m_id;
                g_workspaceModes.erase(id);
                g_workspaceGroups.erase(id);
            });

        // On config.reloaded, do not touch existing mode state; just
        // re-emit so bar widgets get a fresh snapshot. See design.md
        // "What config.reloaded does not do".
        g_configReloadedListener = Event::bus()->m_events.config.reloaded.listen(
            []() {
                for (const auto& [id, _] : g_workspaceModes)
                    emit(id);
            });
    }

}  // namespace hyprwsmode
