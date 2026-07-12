#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>

// The handle Hyprland gives us at PLUGIN_INIT. Required by every
// HyprlandAPI:: call. Stored inline so any translation unit can reach
// it via #include "globals.hpp".
inline HANDLE PHANDLE = nullptr;
