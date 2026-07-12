{
  hyprland,
  lib,
  nix-gitignore,
  cmake,
  ninja,
  pkg-config,
}:
# Build with Hyprland's stdenv so the compiler, libstdc++ and PIE flags
# match the compositor. Any mismatch here shows up as an obscure symbol
# resolution error at plugin load time rather than a build failure.
hyprland.stdenv.mkDerivation {
  pname = "hyprwsmode";
  version = "0.1";

  # Ignore anything in .gitignore so the source snapshot doesn't include
  # build/, compile_commands.json, or editor droppings.
  src = nix-gitignore.gitignoreSource [ ] ./.;

  nativeBuildInputs = [ cmake ninja pkg-config ];
  # hyprland.dev provides pkg-config discovery for the hyprland .pc file.
  # hyprland.buildInputs pulls in aquamarine, hyprutils, hyprlang etc.
  # that Hyprland's headers include.
  buildInputs = [ hyprland.dev ] ++ hyprland.buildInputs;

  # Use ninja (added to nativeBuildInputs) rather than the default make.
  # This matches hy3's pattern and gives noticeably faster incremental
  # builds for a plugin of this size.
  buildPhase = "ninjaBuildPhase";
  enableParallelBuilding = true;

  meta = with lib; {
    homepage = "https://github.com/SubiqT/hyprwsmode";
    description = "Per-workspace window modes for Hyprland";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
