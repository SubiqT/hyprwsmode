#include "dispatcher.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "mode.hpp"
#include "state.hpp"
#include "window.hpp"

namespace hyprwsmode {

    namespace {

        // Split the dispatcher argument string on whitespace into at most
        // two tokens. `wsmode set tile` -> ("set", "tile"); `wsmode toggle`
        // -> ("toggle", ""). Trailing whitespace is ignored.
        std::pair<std::string_view, std::string_view> splitArgs(std::string_view s) {
            auto trimLeft = [](std::string_view v) {
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
                    v.remove_prefix(1);
                return v;
            };

            s        = trimLeft(s);
            auto sp  = s.find_first_of(" \t");
            auto cmd = sp == std::string_view::npos ? s : s.substr(0, sp);
            auto rest = sp == std::string_view::npos ? std::string_view{} : trimLeft(s.substr(sp + 1));

            // Trim trailing whitespace from rest.
            while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t'))
                rest.remove_suffix(1);

            return {cmd, rest};
        }

        WORKSPACEID activeWorkspaceId() {
            const auto mon = Desktop::focusState()->monitor();
            if (!mon || !mon->m_activeWorkspace)
                return WORKSPACE_INVALID;
            return mon->m_activeWorkspace->m_id;
        }

        // Group all non-floating windows on the workspace into a single
        // group. Called when a workspace transitions to Managed{Stack};
        // the design's rule-3 exception on top of rule-2 (existing windows
        // are otherwise left alone).
        void groupExistingWindows(WORKSPACEID wsId) {
            for (const auto& w : g_pCompositor->m_windows) {
                if (!w || !w->m_workspace || w->m_workspace->m_id != wsId)
                    continue;
                if (w->m_isFloating)
                    continue;

                applyModeToWindow(w, wsId, Mode{Managed{ManagedType::Stack}});
            }
        }

        // Apply the state change for a `set` or `toggle`, then apply mode
        // to existing windows per design rules 2 and 3, and emit the
        // socket2 event only when the wire-facing mode actually changed.
        // Returns the mode string suitable for SDispatchResult.error
        // (used as hyprctl output).
        bool isStack(const Mode& m) {
            const auto* mgr = std::get_if<Managed>(&m);
            return mgr && mgr->type == ManagedType::Stack;
        }

        std::string commit(WORKSPACEID wsId, SWorkspaceRuntime& rt, const Mode& previous) {
            const bool wireModeChanged = formatMode(rt.current) != formatMode(previous);

            // Rule 3 exception: transitioning INTO Managed{Stack} groups
            // existing non-floating windows. All other transitions leave
            // existing windows alone.
            if (isStack(rt.current) && !isStack(previous))
                groupExistingWindows(wsId);

            // Redundant emit suppression: `wsmode toggle` on Floating flips
            // lastManaged only, so the wire mode doesn't change. Emitting
            // the same `wsmode>>N,float` twice tells consumers nothing.
            // `wsmode set X` on an already-X workspace similarly.
            if (wireModeChanged)
                emit(wsId);

            return std::string{formatMode(rt.current)};
        }

        SDispatchResult okWith(const std::string& msg) {
            return SDispatchResult{.passEvent = false, .success = true, .error = msg};
        }

        SDispatchResult err(const std::string& msg) {
            Log::logger->log(Log::WARN, "[hyprwsmode] dispatcher: {}", msg);
            return SDispatchResult{.passEvent = false, .success = false, .error = msg};
        }

        SDispatchResult doSet(WORKSPACEID wsId, SWorkspaceRuntime& rt, std::string_view arg) {
            const auto parsed = parseMode(arg);
            if (!parsed)
                return err(std::format("wsmode set: unknown mode '{}' (expected tile, stack, or float)", arg));

            const auto previous = rt.current;
            rt.current          = *parsed;

            // Update lastManaged when set to a managed type. `set float`
            // preserves lastManaged so a later toggle_float restores the
            // last managed type the user had.
            if (auto* m = std::get_if<Managed>(&rt.current))
                rt.lastManaged = m->type;

            return okWith(commit(wsId, rt, previous));
        }

        SDispatchResult doToggle(WORKSPACEID wsId, SWorkspaceRuntime& rt) {
            const auto previous = rt.current;

            if (auto* m = std::get_if<Managed>(&rt.current)) {
                const auto next  = flip(m->type);
                rt.current       = Mode{Managed{next}};
                rt.lastManaged   = next;
            } else {
                // On Floating: don't leave float. Flip lastManaged so the
                // next toggle_float restores the opposite managed type.
                // Emit anyway so consumers see the DEBUG-like signal.
                rt.lastManaged = flip(rt.lastManaged);
                Log::logger->log(Log::DEBUG,
                                 "[hyprwsmode] toggle on floating ws {}, updated lastManaged to {}",
                                 wsId, formatManagedType(rt.lastManaged));
            }

            return okWith(commit(wsId, rt, previous));
        }

        SDispatchResult doToggleFloat(WORKSPACEID wsId, SWorkspaceRuntime& rt) {
            const auto previous = rt.current;

            if (auto* m = std::get_if<Managed>(&rt.current)) {
                rt.lastManaged = m->type;   // snapshot
                rt.current     = Mode{Floating{}};
            } else {
                rt.current = Mode{Managed{rt.lastManaged}};
            }

            return okWith(commit(wsId, rt, previous));
        }

        SDispatchResult doCurrent(SWorkspaceRuntime& rt) {
            return okWith(std::string{formatMode(rt.current)});
        }

        // `wsmode reseed` on the active workspace: forget the current runtime
        // entry, re-read the workspace's default from config, and apply it as
        // if a fresh `wsmode set <default>` had been issued. Discoverable
        // escape hatch for the "I edited my Nix config and want it to apply
        // right now" case; the alternative is bouncing the workspace by
        // moving windows off it or waiting for the next Hyprland launch.
        SDispatchResult doReseed(WORKSPACEID wsId) {
            const auto previous = g_workspaceModes.contains(wsId)
                                    ? g_workspaceModes.at(wsId).current
                                    : resolveDefault(wsId);

            g_workspaceModes.erase(wsId);
            auto& rt = seedFor(wsId);
            return okWith(commit(wsId, rt, previous));
        }

        SDispatchResult handle(std::string args) {
            const auto [cmd, arg] = splitArgs(args);

            const auto wsId = activeWorkspaceId();
            if (wsId == WORKSPACE_INVALID)
                return err("no active workspace");

            // `reseed` deletes the entry, so route it before seedFor.
            if (cmd == "reseed")       return doReseed(wsId);

            auto& rt = seedFor(wsId);

            if (cmd == "toggle")       return doToggle(wsId, rt);
            if (cmd == "toggle_float") return doToggleFloat(wsId, rt);
            if (cmd == "set")          return doSet(wsId, rt, arg);
            if (cmd == "current")      return doCurrent(rt);
            if (cmd.empty())           return err("wsmode: missing subcommand (toggle | toggle_float | set | reseed | current)");

            return err(std::format("wsmode: unknown subcommand '{}'", cmd));
        }

    }  // namespace

    void registerDispatchers() {
        HyprlandAPI::addDispatcherV2(PHANDLE, "wsmode", handle);
    }

}  // namespace hyprwsmode
