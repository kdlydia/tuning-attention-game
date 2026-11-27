cmake_minimum_required(VERSION 3.31...4.3)
project(TuningAttentionEditor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

find_package(MayaFlux REQUIRED
    PATHS "$ENV{MAYAFLUX_ROOT}/lib/cmake/MayaFlux" /usr/lib/cmake/MayaFlux /usr/local/lib/cmake/MayaFlux
    NO_DEFAULT_PATH)
find_package(Threads REQUIRED)

# cargo builds the Rust workspace into one static library (app, with forma and scene inside it),
# and cxx writes the bridge headers (forma/src/lib.rs.h, scene/src/lib.rs.h, rust/cxx.h) under
# <target>/cxxbridge.
# The first cargo on PATH, so a rustup setup uses its own cargo and rustc together.
# RUSTUP_AUTO_INSTALL=0 keeps a build from installing or updating a toolchain.
# cargo runs on every build and decides itself whether anything changed.
find_program(CARGO cargo PATHS ENV PATH NO_DEFAULT_PATH REQUIRED)
message(STATUS "cargo: ${CARGO}")
set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
set(RUST_LIB "${RUST_TARGET_DIR}/release/libapp.a")

add_custom_target(editor_cargo
    COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${RUST_TARGET_DIR}" RUSTUP_AUTO_INSTALL=0
        ${CARGO} build --release --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml" -p app
    BYPRODUCTS "${RUST_LIB}" "${RUST_TARGET_DIR}/cxxbridge/forma/src/lib.rs.h" "${RUST_TARGET_DIR}/cxxbridge/scene/src/lib.rs.h"
    COMMENT "Building the Rust workspace"
    USES_TERMINAL
    VERBATIM)

add_executable(editor main.cpp forma/cpp/forma_shim.cpp scene/cpp/scene_shim.cpp)
add_dependencies(editor editor_cargo)
# The game's own code (src/MazeView.hpp, src/scene/) runs in the editor's game view.
cmake_path(GET CMAKE_SOURCE_DIR PARENT_PATH GAME_ROOT)
target_include_directories(editor PRIVATE forma/cpp scene/cpp "${RUST_TARGET_DIR}/cxxbridge" "${GAME_ROOT}/src")
target_precompile_headers(editor PRIVATE "${MayaFlux_PCH_FILE}")
target_compile_definitions(editor PRIVATE MAYAFLUX_PROJECT MAYAFLUX_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    GAME_MAZE_FILE="${GAME_ROOT}/data/maze.txt")
target_link_libraries(editor PRIVATE MayaFlux::MayaFluxLib "${RUST_LIB}" Threads::Threads ${CMAKE_DL_LIBS})
target_link_options(editor PRIVATE -rdynamic)
set_target_properties(editor PROPERTIES
    INSTALL_RPATH "${MayaFlux_LIB_DIR}"
    BUILD_WITH_INSTALL_RPATH TRUE)
