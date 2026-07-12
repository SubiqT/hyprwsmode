#include "config.hpp"

#include <format>
#include <string>

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "globals.hpp"
#include "log.hpp"
#include "mode.hpp"

namespace hyprwsmode {

    namespace {
        // Register one string keyword with a strChoice validator. `choices`
        // is passed as a vector so per-workspace variants can include the
        // empty string ("inherit global"); strChoice at
        // src/config/values/ConfigValues.hpp:33 uses std::ranges::contains
        // and accepts empty strings when they're in the vector.
        SP<Config::Values::CStringValue> makeStringChoice(const char*                     name,
                                                          const char*                     description,
                                                          const char*                     defaultVal,
                                                          std::vector<std::string>        choices) {
            auto v = makeShared<Config::Values::CStringValue>(
                name,
                description,
                defaultVal,
                Config::Values::SStringValueOptions{
                    .validator = Config::Values::strChoice(std::move(choices)),
                });
            HyprlandAPI::addConfigValueV2(PHANDLE, v);
            return v;
        }
    }

    void registerConfig() {
        // Global defaults. String literals passed to CStringValue have
        // static storage; no lifetime concerns.
        g_config.defaultManaged = makeStringChoice(
            "plugin:wsmode:default_managed",
            "Global default: managed vs float (values: managed, float)",
            "managed",
            {"managed", "float"});

        g_config.defaultManagedType = makeStringChoice(
            "plugin:wsmode:default_managed_type",
            "Global default managed type when managed (values: tile, stack)",
            "tile",
            {"tile", "stack"});

        // Per-workspace overrides. CStringValue stores m_name as a
        // const char* without copying (see IValue at
        // src/config/values/types/IValue.hpp), so the strings backing
        // these names must outlive the config values. Static arrays
        // give them program-lifetime storage. This mirrors what
        // borders-plus-plus does for its per-slot keywords.
        static std::array<std::string, MAX_CONFIGURED_WS> managedNames;
        static std::array<std::string, MAX_CONFIGURED_WS> typeNames;

        for (size_t i = 0; i < MAX_CONFIGURED_WS; ++i) {
            managedNames[i] = std::format("plugin:wsmode:default_managed_{}", i + 1);
            typeNames[i]    = std::format("plugin:wsmode:default_managed_type_{}", i + 1);

            g_config.defaultManagedN[i] = makeStringChoice(
                managedNames[i].c_str(),
                "Per-workspace managed override (managed, float, or empty to inherit)",
                "",
                {"managed", "float", ""});

            g_config.defaultManagedTypeN[i] = makeStringChoice(
                typeNames[i].c_str(),
                "Per-workspace managed-type override (tile, stack, or empty to inherit)",
                "",
                {"tile", "stack", ""});
        }
    }

    namespace {
        // Return the per-workspace override string for a given axis, or an
        // empty string if the workspace is outside 1..MAX_CONFIGURED_WS
        // (which is also the "inherit" signal). Negative ids (special
        // workspaces) are naturally excluded by the >= 1 check.
        std::string perWorkspaceValue(WORKSPACEID id,
                                      const std::array<SP<Config::Values::CStringValue>, MAX_CONFIGURED_WS>& axis) {
            if (id < 1 || static_cast<size_t>(id) > MAX_CONFIGURED_WS)
                return "";
            const auto& v = axis[static_cast<size_t>(id - 1)];
            return v ? std::string{v->value()} : "";
        }
    }

    ManagedType resolveDefaultType(WORKSPACEID id) {
        const auto perWs = perWorkspaceValue(id, g_config.defaultManagedTypeN);
        const auto raw   = perWs.empty() ? std::string{g_config.defaultManagedType->value()} : perWs;

        if (auto t = parseManagedType(raw))
            return *t;

        // strChoice should have rejected this at parse time, so getting
        // here means either the config value is uninitialised (PLUGIN_INIT
        // not yet called) or Hyprlang gave us something unexpected. Fall
        // back rather than throw; a listener that throws crashes the
        // compositor.
        log::warn("resolveDefaultType({}): unknown value '{}', falling back to tile", id, raw);
        return ManagedType::Tile;
    }

    Mode resolveDefault(WORKSPACEID id) {
        const auto perWs = perWorkspaceValue(id, g_config.defaultManagedN);
        const auto raw   = perWs.empty() ? std::string{g_config.defaultManaged->value()} : perWs;

        if (raw == "float")
            return Mode{Floating{}};

        // Anything else (including the resolved "managed" and any
        // uninitialised fallback) becomes managed with the type axis
        // resolved separately.
        return Mode{Managed{resolveDefaultType(id)}};
    }

}  // namespace hyprwsmode
