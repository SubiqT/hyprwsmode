# hyprwsmode

Hyprland plugin that assigns each workspace a window mode in the spirit
of yabai's `layout` on macOS. Every workspace picks one of three modes:

- **tile** - the default dwindle layout
- **float** - all windows open floating
- **stack** - all windows join a single Hyprland group (tabbed container)

Modes are declared per-workspace in `hyprland.conf` and can be toggled
at runtime via a dispatcher. Mode changes are broadcast on socket2 so
bar widgets update without polling.

The plugin applies modes in the `window.openEarly` hook, before layout
placement, so there is no visible tile-then-adjust flash.

Built against Hyprland `v0.55.4`.

## Install

### Nix flake

```nix
{
  inputs = {
    hyprland.url = "git+https://github.com/hyprwm/Hyprland?submodules=1";

    hyprwsmode = {
      url = "github:SubiqT/hyprwsmode";
      inputs.hyprland.follows = "hyprland";
    };
  };
}
```

Load via the Hyprland home-manager module:

```nix
wayland.windowManager.hyprland = {
  enable = true;
  plugins = [ hyprwsmode.packages.${system}.default ];
};
```

Or point Hyprland at the plugin `.so` directly:

```
plugin = ${hyprwsmode.packages.x86_64-linux.default}/lib/libhyprwsmode.so
```

### Manual build

Requires Hyprland's development headers on the system (`pkg-config
hyprland` must succeed):

```sh
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
hyprctl plugin load "$PWD/build/libhyprwsmode.so"
```

## Configuration

Two axes: whether a workspace is managed vs unmanaged (unmanaged =
float), and when managed, tile vs stack.

Global defaults:

```
plugin:wsmode:default_managed      = managed    # "managed" or "float"
plugin:wsmode:default_managed_type = tile       # "tile" or "stack"
```

Per-workspace overrides for workspaces 1..9. Empty string inherits the
global:

```
plugin:wsmode:default_managed_3      = float    # ws 3: float by default
plugin:wsmode:default_managed_type_3 = stack    # if toggled out of float, restore to stack

plugin:wsmode:default_managed_type_5 = stack    # ws 5: managed (inherit), stacked
```

Workspaces outside 1..9 (including negative-id special workspaces) fall
through to the global defaults with no per-workspace override option.

Config changes take effect on workspace creation. `hyprctl reload` does
not retroactively change the mode of a workspace that already exists;
destroy and recreate the workspace (move windows off, or wait for the
next Hyprland launch) to pick up a new default.

## Dispatchers

Bind these via `bind = ..., dispatcher, wsmode, <subcommand>` in
`hyprland.conf`, or call directly with `hyprctl dispatch wsmode
<subcommand>`.

| Subcommand              | Behaviour                                     |
| ---                     | ---                                           |
| `wsmode toggle`         | On the active workspace, flip `tile ↔ stack`. |
| `wsmode toggle_float`   | On the active workspace, flip `managed ↔ float`. |
| `wsmode set tile`       | Set the active workspace to tile.             |
| `wsmode set stack`      | Set the active workspace to stack.            |
| `wsmode set float`      | Set the active workspace to float.            |
| `wsmode current`        | Print the active workspace's mode.            |

Existing windows on the target workspace are left in place, except when
transitioning to `stack`: existing non-floating windows are grouped
into the workspace's stack group so the workspace ends up with the
one-window-visible-at-a-time invariant that `stack` implies.

`wsmode toggle` on a workspace that is currently `float` does not leave
float. It flips the remembered "last managed type" so the next
`wsmode toggle_float` restores the opposite side (tile ↔ stack). This
keeps the mental model "toggle picks the managed type, toggle_float
picks whether we're managed at all" consistent even when unmanaged.

## Socket2 event

Every mode change emits a `wsmode` event on Hyprland's socket2:

```
wsmode>>3,stack
wsmode>>3,float
```

Payload is `<workspace-id>,<mode>` where mode is one of `tile`,
`stack`, `float`. The event fires on:

- Workspace creation (once per workspace, with the seed mode).
- Every dispatcher call that changes the mode.
- `hyprctl reload`, once per known workspace, with the current effective mode.

Consume it like any other socket2 event:

```sh
socat - "UNIX-CONNECT:$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock" \
  | grep '^wsmode>>'
```

## Compatibility

Assumes Hyprland's built-in tiling layout (dwindle or master). Running
alongside a layout plugin like hy3 or hyprscroller is unsupported:

- `float` still works because floating windows bypass the tiled layout.
- `tile` delegates to whichever layout is active, so "tile" means
  "whatever the loaded layout does".
- `stack` creates a Hyprland `CGroup` which layout plugins do not
  manage. Behaviour is undefined.

If you use hy3, prefer `hy3:makegroup, tab` for tabbed containers and
treat hyprwsmode as tile/float only, or bind only `wsmode toggle_float`.

## Non-goals

- Adding a new layout algorithm. `stack` uses Hyprland groups.
- Replacing dwindle. `tile` means "let dwindle do its thing".
- Cross-compositor support. Hyprland only.
- Persistence of runtime toggles across full Hyprland restarts.

## Licence

MIT.
