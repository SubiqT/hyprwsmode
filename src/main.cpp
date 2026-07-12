#define WLR_USE_UNSTABLE

#include <stdexcept>
#include <string>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "config.hpp"
#include "dispatcher.hpp"
#include "globals.hpp"
#include "state.hpp"
#include "window.hpp"

// PLUGIN_API_VERSION returns the ABI tag baked into the Hyprland headers
// at build time. Do NOT change. Hyprland compares this against its own
// hash before running any plugin code; a mismatch prevents load.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Version check: compare the Hyprland commit hash we compiled against
    // to the one currently running. If they diverge, the ABI can differ
    // even when the API tag matches, and any HyprlandAPI call becomes
    // undefined behaviour. Refuse to load rather than crash later.
    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprwsmode] version mismatch with running Hyprland, refusing to load",
            CHyprColor{1.0, 0.2, 0.2, 1.0},
            5000);
        throw std::runtime_error("[hyprwsmode] version mismatch");
    }

    // Register all config keywords before any config parse pass. Calling
    // HyprlandAPI::reloadConfig after registration forces a re-read so
    // user-supplied values in hyprland.conf are honoured on first load.
    hyprwsmode::registerConfig();
    HyprlandAPI::reloadConfig();

    // Register lifecycle listeners after config so seeding reads the
    // user-supplied values, not just defaults.
    hyprwsmode::registerListeners();
    hyprwsmode::registerWindowListeners();
    hyprwsmode::registerDispatchers();

    HyprlandAPI::addNotification(
        PHANDLE,
        "[hyprwsmode] loaded",
        CHyprColor{0.2, 1.0, 0.2, 1.0},
        3000);

    Log::logger->log(Log::WARN, "[hyprwsmode] PLUGIN_INIT completed, listeners registered");

    return {"hyprwsmode",
            "Per-workspace window modes for Hyprland",
            "Subi",
            "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Log::logger->log(Log::INFO, "[hyprwsmode] plugin unloaded");
}
