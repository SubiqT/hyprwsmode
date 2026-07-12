{
  description = "Per-workspace window modes for Hyprland";

  # Pinned to the latest stable Hyprland tag at the time of writing.
  # Consumers override this with `inputs.hyprland.follows = "hyprland"`
  # so the plugin builds against the same Hyprland they run. Submodules
  # are required because Hyprland vendors hyprland-protocols, udis86,
  # and tracy as submodules.
  inputs = {
    hyprland.url = "git+https://github.com/hyprwm/Hyprland?submodules=1&ref=v0.55.4";
  };

  outputs = { self, hyprland, ... }:
    let
      inherit (hyprland.inputs) nixpkgs;
      # Iterate the same systems Hyprland itself builds for. Today that
      # is x86_64-linux and aarch64-linux via hyprland.inputs.systems.
      hyprlandSystems = fn:
        nixpkgs.lib.genAttrs
          (builtins.attrNames hyprland.packages)
          (system: fn system nixpkgs.legacyPackages.${system});
    in
    {
      packages = hyprlandSystems (system: pkgs: rec {
        hyprwsmode = pkgs.callPackage ./default.nix {
          hyprland = hyprland.packages.${system}.hyprland;
        };
        default = hyprwsmode;
      });

      devShells = hyprlandSystems (system: pkgs: {
        default = pkgs.mkShell.override {
          stdenv = hyprland.packages.${system}.hyprland.stdenv;
        } {
          name = "hyprwsmode";
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
          buildInputs = [ hyprland.packages.${system}.hyprland.dev ]
            ++ hyprland.packages.${system}.hyprland.buildInputs;
        };
      });
    };
}
