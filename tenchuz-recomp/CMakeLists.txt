# tenchuz - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(tenchuz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(TENCHUZ_SOURCES
    src/main.cpp
    src/gameplay.cpp
    src/mod_loader.cpp
    src/camera_probe.cpp
    src/settings_store.cpp
    src/controller.cpp
    src/photo_mode.cpp
    src/speedrun.cpp
    src/ui_settings.cpp
    src/shader_settings.cpp
    src/net_settings.cpp
    src/upnp.cpp
)

if(WIN32)
    add_executable(tenchuz WIN32 ${TENCHUZ_SOURCES})
else()
    add_executable(tenchuz ${TENCHUZ_SOURCES})
endif()

# Stage the Xenos GPU emulation plugin (rexgpu-xenos.dll) next to the exe.
# Without it the runtime comes up in "native rendering mode" (no graphics).
rexglue_setup_target(tenchuz GPU_PLUGINS xenos)

# Emit a link map (tenchuz.map) so a crash RIP can be resolved to the
# recompiled function (sub_/fnptr_ symbols) it belongs to. Used by
# tools/resolve_rip.py when diagnosing guest crashes.
# NB: the toolchain is GNU-flavored clang++ on Windows, so CMake's MSVC
# variable is false here; gate on WIN32 instead.
# /DEBUG additionally emits tenchuz.pdb; the recompiled code is built with
# -gline-tables-only, so llvm-symbolizer can map a crash RIP straight to the
# generated C++ line (i.e. the exact guest instruction). Also temporary
# diagnostic scaffolding for Milestone D.
if(WIN32)
    target_link_options(tenchuz PRIVATE "LINKER:/MAP" "LINKER:/DEBUG")
endif()
