#pragma once

namespace hyprwsmode {

    // Register the wsmode dispatcher via HyprlandAPI::addDispatcherV2.
    // The single dispatcher handles these subcommands:
    //   wsmode toggle          (tile <-> stack on active workspace)
    //   wsmode toggle_float    (managed <-> float on active workspace)
    //   wsmode set <mode>      (tile | stack | float, explicit)
    //   wsmode current         (print active workspace's mode)
    //   wsmode reseed          (recompute active workspace's mode from
    //                          config; discoverable escape hatch after
    //                          config edits, since hyprctl reload alone
    //                          does not reformat existing workspaces)
    void registerDispatchers();

}  // namespace hyprwsmode
