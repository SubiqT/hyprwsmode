#pragma once

#include <cstdio>
#include <format>
#include <string>
#include <utility>

// The Hyprland-provided Log::logger in src/debug/log/Logger.hpp is
// `inline UP<CLogger> logger = makeUnique<CLogger>();` in a header.
// Because our plugin is loaded as a separate DSO with its own copy of
// the header, the plugin's Log::logger is a distinct, uninitialised
// CLogger instance whose output goes nowhere. Writing to it makes the
// plugin appear silent in Hyprland's log.
//
// Workaround: write to stderr directly. Hyprland captures fd 2 for its
// own log output (systemd journal or a redirected log file), so plugin
// messages appear alongside compositor messages there.

namespace hyprwsmode::log {

    template <typename... Args>
    void write(const char* level, std::format_string<Args...> fmt, Args&&... args) {
        // std::format into a std::string first so any exceptions raised
        // by malformed formats do not run inside fprintf.
        const auto msg = std::format(fmt, std::forward<Args>(args)...);
        std::fprintf(stderr, "[hyprwsmode] %s: %s\n", level, msg.c_str());
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        write("info", fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        write("warn", fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        write("debug", fmt, std::forward<Args>(args)...);
    }

}  // namespace hyprwsmode::log
