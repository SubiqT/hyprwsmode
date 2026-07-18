#include "dispatcher.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "config.hpp"
#include "globals.hpp"
#include "log.hpp"
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

        // Reformat all windows on the workspace to match a target mode.
        // Called on every wire-mode change. This is the plugin's core
        // premise: the workspace's mode governs the behaviour of every
        // window on it, so a mode change must visibly apply to windows
        // already open, not just to future ones.
        void applyModeToExistingWindows(WORKSPACEID wsId, const Mode& mode) {
            for (const auto& w : g_pCompositor->m_windows) {
                if (!w || !w->m_workspace || w->m_workspace->m_id != wsId)
                    continue;

                applyModeToWindow(w, wsId, mode, /*postPlacement=*/true);
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

            // Apply mode to all existing windows whenever the wire mode
            // changes. The workspace mode governs every window on the
            // workspace; a toggle that only affected new windows would
            // defeat the plugin's purpose.
            if (wireModeChanged)
                applyModeToExistingWindows(wsId, rt.current);

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
            log::warn("dispatcher: {}", msg);
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
                // On Floating: transition back to Managed[lastManaged].
                // Users who want to go INTO float use toggle_float. This
                // makes `toggle` always produce a visible change: the
                // workspace either flips type within managed, or leaves
                // float back to whichever managed type it last held.
                rt.current = Mode{Managed{rt.lastManaged}};
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

        // `wsmode broadcast` re-emits wsmode>>N,<mode> on socket2 for every
        // workspace this plugin tracks, without changing any state. Bar
        // widgets that started after Hyprland use it to fetch an initial
        // snapshot; the socket2 stream is otherwise event-only, and a
        // widget with no delivered events sits with no mode data. The
        // same rebroadcast path is used from the config.reloaded listener.
        SDispatchResult doBroadcast() {
            broadcastAll();
            return okWith("");
        }

        SDispatchResult handle(std::string args) {
            const auto [cmd, arg] = splitArgs(args);

            // `broadcast` is a whole-plugin read; no active workspace needed.
            if (cmd == "broadcast")    return doBroadcast();

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
            if (cmd.empty())           return err("wsmode: missing subcommand (toggle | toggle_float | set | reseed | current | broadcast)");

            return err(std::format("wsmode: unknown subcommand '{}'", cmd));
        }

    }  // namespace

    namespace {

        // Lua thunks. Hyprland 0.55+ exposes plugin operations under
        // hl.plugin.<namespace>.<name>. `hyprctl dispatch` now routes
        // through Lua, so an addDispatcherV2 handler alone is not
        // reachable via `hyprctl dispatch wsmode <...>` unless the user
        // also configures Hyprland to use the classic keybind syntax.
        // Registering these thunks gives users both paths:
        //
        //   bind = SUPER, m, dispatcher, wsmode, toggle          # classic
        //   hyprctl dispatch 'hl.plugin.wsmode.toggle()'         # Lua CLI
        //   hl.bind("SUPER + m", function() return              # Lua config
        //     hl.plugin.wsmode.toggle() end)
        //
        // Signatures match PLUGIN_LUA_FN = int (*)(lua_State*); a return
        // value of N means "N return values pushed on the Lua stack".
        //
        // Return contract: hl.bind's callback and hyprctl's Lua evaluator
        // both expect the returned value to be a valid dispatcher table.
        // The action thunks (toggle, toggle_float, set, reseed) perform
        // their side effect via handle() and then push hl.dsp.no_op() as
        // a valid dispatcher table so the caller has something to invoke.
        // Without this the action-fires-on-callback path was silent (bind
        // saw nil back, treated the whole thing as unbindable).
        //
        // The query thunk (current) pushes the mode string and returns it
        // as-is. It is not intended for hl.bind; users querying via CLI
        // wrap it in print, e.g.
        //   hyprctl dispatch 'print(hl.plugin.wsmode.current())'.

        // Build hl.dsp.no_op() on top of the Lua stack. Pushes nil on
        // any lookup failure so the caller always sees exactly one
        // value pushed regardless of Hyprland Lua init state.
        void pushNoOp(lua_State* L) {
            lua_getglobal(L, "hl");
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }

            lua_getfield(L, -1, "dsp");
            lua_remove(L, -2);  // pop `hl`, leave `dsp` on top
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }

            lua_getfield(L, -1, "no_op");
            lua_remove(L, -2);  // pop `dsp`, leave `no_op` on top
            if (!lua_isfunction(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }

            // lua_pcall runs no_op() and replaces the function with its
            // single return value. If it raises, the error message ends
            // up on the stack; convert to a warn line and yield nil so
            // the compositor doesn't panic in a plugin callback.
            if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
                const char* errMsg = lua_tostring(L, -1);
                log::warn("hl.dsp.no_op() failed: {}", errMsg ? errMsg : "(no message)");
                lua_pop(L, 1);
                lua_pushnil(L);
            }
        }

        int lua_toggle(lua_State* L) {
            handle("toggle");
            pushNoOp(L);
            return 1;
        }

        int lua_toggle_float(lua_State* L) {
            handle("toggle_float");
            pushNoOp(L);
            return 1;
        }

        int lua_set(lua_State* L) {
            const char* arg = luaL_checkstring(L, 1);
            handle(std::string{"set "} + arg);
            pushNoOp(L);
            return 1;
        }

        int lua_current(lua_State* L) {
            auto result = handle("current");
            lua_pushstring(L, result.error.c_str());
            return 1;
        }

        int lua_reseed(lua_State* L) {
            handle("reseed");
            pushNoOp(L);
            return 1;
        }

        int lua_broadcast(lua_State* L) {
            handle("broadcast");
            pushNoOp(L);
            return 1;
        }

    }  // namespace

    void registerDispatchers() {
        HyprlandAPI::addDispatcherV2(PHANDLE, "wsmode", handle);

        // Lua namespace `hl.plugin.wsmode.*`. Register each subcommand as
        // a separate callable rather than a single dispatcher-string, so
        // the Lua-side API mirrors the C++ one and errors on typos.
        struct SLuaBinding {
            const char* name;
            PLUGIN_LUA_FN fn;
        };
        static const SLuaBinding bindings[] = {
            {"toggle",       &lua_toggle},
            {"toggle_float", &lua_toggle_float},
            {"set",          &lua_set},
            {"current",      &lua_current},
            {"reseed",       &lua_reseed},
            {"broadcast",    &lua_broadcast},
        };

        for (const auto& b : bindings) {
            if (!HyprlandAPI::addLuaFunction(PHANDLE, "wsmode", b.name, b.fn))
                log::warn("addLuaFunction failed for hl.plugin.wsmode.{}", b.name);
        }
    }

}  // namespace hyprwsmode
