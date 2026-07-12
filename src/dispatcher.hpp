#pragma once

namespace hyprwsmode {

    // Register the wsmode dispatcher via HyprlandAPI::addDispatcherV2.
    // The single dispatcher handles the four subcommands:
    //   wsmode toggle          (tile <-> stack)
    //   wsmode toggle_float    (managed <-> float)
    //   wsmode set <mode>      (tile | stack | float)
    //   wsmode current
    void registerDispatchers();

}  // namespace hyprwsmode
