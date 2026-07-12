cmake_minimum_required(VERSION 3.27)

project(hyprwsmode
    DESCRIPTION "Per-workspace window modes for Hyprland"
    VERSION 0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings that match Hyprland's own build without the noisy ones the
# codebase intentionally silences.
add_compile_options(-fPIC -Wall -Wextra -Wno-unused-parameter -Wno-unused-value
                    -Wno-missing-field-initializers -Wno-narrowing -Wno-pointer-arith)

# GCC's --no-gnu-unique keeps type_info addresses per-DSO, matching the
# flag Hyprland compiles with. Required for reliable function-hook and
# typeid-based plugin behaviour.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(--no-gnu-unique)
endif()

file(GLOB SRC CONFIGURE_DEPENDS "src/*.cpp")

add_library(hyprwsmode SHARED ${SRC})

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    pixman-1
    wayland-server
    xkbcommon
)

# Hyprland 0.55+ requires Lua 5.5 for its Lua-based config and routes
# hyprctl dispatch through the Lua bindings. Plugins that want to be
# callable via `hyprctl dispatch <lua-expression>` and from Lua config
# files (hl.plugin.<name>.<fn>) need to link Lua themselves; hyprland.pc
# does not chain it in. Search the same set of package names Hyprland's
# own CMakeLists uses so distros' varying Lua .pc naming is handled.
pkg_search_module(LUA REQUIRED IMPORTED_TARGET
    lua55 lua5.5 lua-55 lua-5.5 lua>=5.5 lua<5.6)

target_link_libraries(hyprwsmode PRIVATE PkgConfig::deps PkgConfig::LUA)

install(TARGETS hyprwsmode LIBRARY)
