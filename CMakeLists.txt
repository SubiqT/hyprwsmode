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
target_link_libraries(hyprwsmode PRIVATE PkgConfig::deps)

install(TARGETS hyprwsmode LIBRARY)
