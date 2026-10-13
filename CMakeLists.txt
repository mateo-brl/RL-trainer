cmake_minimum_required(VERSION 3.20)

# Static CRT (/MT) for every configuration, never the debug CRT: pluginsdk.dll is a release build and
# std::string / std::function cross the DLL boundary, so _ITERATOR_DEBUG_LEVEL must stay 0. /MT also
# avoids depending on the msvcp140.dll version loaded by the game. Must be set before project().
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Version: the release workflow passes -DRLT_VERSION=<x.y.z> (from the git tag or the run number).
set(RLT_VERSION "1.0.0" CACHE STRING "Plugin version (major.minor.patch)")
project(RLTrainer VERSION ${RLT_VERSION} LANGUAGES CXX)
set(RLT_REPO_SLUG "mateo-brl/RL-trainer" CACHE STRING "GitHub owner/repo used by the in-game update check")

# ------------------------------------------------------------------------------------------------
# RL Trainer: BakkesMod plugin for Rocket League.
#   - rlt_* libraries: pure C++20 core (no SDK), built and unit-tested on any platform.
#   - RLTrainer (DLL): BakkesMod glue + ImGui UI, Windows/MSVC only, needs the BakkesMod SDK.
#
# Linux/macOS (tests only):  cmake -S . -B build && cmake --build build && ctest --test-dir build
# Windows (plugin):          cmake -S . -B build -A x64 -DBAKKESMOD_SDK_DIR=<path to BakkesModSDK>
#                            cmake --build build --config Release
# ------------------------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(RLT_BUILD_TESTS "Build the core unit tests" ON)
if(MSVC)
    set(RLT_PLUGIN_DEFAULT ON)
else()
    set(RLT_PLUGIN_DEFAULT OFF)
endif()
option(RLT_BUILD_PLUGIN "Build the BakkesMod plugin DLL (MSVC + BakkesMod SDK)" ${RLT_PLUGIN_DEFAULT})
option(RLT_INSTALL_TO_BAKKESMOD "Copy the DLL to %APPDATA%/bakkesmod/bakkesmod/plugins after build" OFF)
option(RLT_LTO "Whole-program optimisation (/GL + /LTCG) for Release builds" ON)
set(BAKKESMOD_SDK_DIR "" CACHE PATH "BakkesModSDK folder (contains include/, lib/pluginsdk.lib, imgui_bm.zip)")

if(MSVC)
    # /utf-8: French UTF-8 literals stay UTF-8 in the binary. /permissive-: standard conformance.
    add_compile_options(/utf-8 /permissive- /W3 /MP /Zc:__cplusplus)
    # Release: fast code, functions/data in separate COMDATs so the linker drops what is unused.
    add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw>)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    if(RLT_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT RLT_IPO_OK OUTPUT RLT_IPO_MSG LANGUAGES CXX)
        if(RLT_IPO_OK)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        endif()
    endif()
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---- Core ----------------------------------------------------------------------------------------
set(RLT_MODULES overlay firsttouch prediction mechanics ballmech kickoff courses shooting maps coach profile)

file(GLOB RLT_COMMON_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common/*.cpp)
add_library(rlt_common STATIC ${RLT_COMMON_SOURCES})
target_include_directories(rlt_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# One library per module so a module (and its tests) builds even while another one is being changed.
# Modules never depend on each other nor on the engine (they talk to it through ActivityHost).
foreach(mod ${RLT_MODULES})
    file(GLOB_RECURSE _src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/modules/${mod}/*.cpp)
    add_library(rlt_mod_${mod} STATIC ${_src})
    target_link_libraries(rlt_mod_${mod} PUBLIC rlt_common)
endforeach()

file(GLOB RLT_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine/*.cpp)
add_library(rlt_engine STATIC ${RLT_ENGINE_SOURCES})
foreach(mod ${RLT_MODULES})
    target_link_libraries(rlt_engine PUBLIC rlt_mod_${mod})
endforeach()

# ---- Tests ---------------------------------------------------------------------------------------
if(RLT_BUILD_TESTS)
    enable_testing()
    # Each module test binary links only its module (+ common), so it builds independently.
    foreach(mod ${RLT_MODULES})
        file(GLOB_RECURSE _tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${mod}/*.cpp)
        if(_tests)
            add_executable(rlt_test_${mod} ${CMAKE_CURRENT_SOURCE_DIR}/tests/TestMain.cpp ${_tests})
            target_link_libraries(rlt_test_${mod} PRIVATE rlt_mod_${mod})
            add_test(NAME ${mod} COMMAND rlt_test_${mod})
        endif()
    endforeach()
    # Common + engine tests (whole core).
    file(GLOB_RECURSE _tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/common/*.cpp)
    add_executable(rlt_test_common ${CMAKE_CURRENT_SOURCE_DIR}/tests/TestMain.cpp ${_tests})
    target_link_libraries(rlt_test_common PRIVATE rlt_engine)
    add_test(NAME common COMMAND rlt_test_common)
endif()

# ---- Plugin (Windows) ----------------------------------------------------------------------------
if(RLT_BUILD_PLUGIN)
    if(NOT MSVC)
        message(FATAL_ERROR "The BakkesMod plugin must be built with MSVC (x64).")
    endif()
    if(NOT BAKKESMOD_SDK_DIR)
        # Default: the SDK shipped with BakkesMod.
        set(BAKKESMOD_SDK_DIR "$ENV{APPDATA}/bakkesmod/bakkesmod/bakkesmodsdk")
    endif()
    file(TO_CMAKE_PATH "${BAKKESMOD_SDK_DIR}" BAKKESMOD_SDK_DIR)
    if(NOT EXISTS "${BAKKESMOD_SDK_DIR}/include/bakkesmod/plugin/bakkesmodplugin.h")
        message(FATAL_ERROR "BakkesMod SDK not found in '${BAKKESMOD_SDK_DIR}'. Pass -DBAKKESMOD_SDK_DIR=<path>.")
    endif()

    # ImGui: BakkesMod's own build (1.75 + one extra ImGuiCol entry) shipped in the SDK. Vanilla 1.75 has a
    # different ImGuiContext layout and would corrupt BakkesMod's context: always use the SDK zip.
    set(RLT_IMGUI_DIR ${CMAKE_BINARY_DIR}/imgui)
    if(NOT EXISTS ${RLT_IMGUI_DIR}/imgui.h)
        file(ARCHIVE_EXTRACT INPUT "${BAKKESMOD_SDK_DIR}/imgui_bm.zip" DESTINATION ${CMAKE_BINARY_DIR})
    endif()
    set(RLT_IMGUI_SOURCES ${RLT_IMGUI_DIR}/imgui.cpp ${RLT_IMGUI_DIR}/imgui_draw.cpp ${RLT_IMGUI_DIR}/imgui_widgets.cpp)

    file(GLOB_RECURSE RLT_PLUGIN_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.cpp)
    add_library(RLTrainer SHARED ${RLT_PLUGIN_SOURCES} ${RLT_IMGUI_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/RLTrainer.rc)
    target_include_directories(RLTrainer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${BAKKESMOD_SDK_DIR}/include
        ${RLT_IMGUI_DIR})
    target_link_libraries(RLTrainer PRIVATE rlt_engine "${BAKKESMOD_SDK_DIR}/lib/pluginsdk.lib")
    target_compile_definitions(RLTrainer PRIVATE
        RLT_REPO_SLUG="${RLT_REPO_SLUG}"
        RLT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        RLT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        RLT_VERSION_PATCH=${PROJECT_VERSION_PATCH})
    # Lower-case file name: "plugin load rltrainer" in the BakkesMod console.
    set_target_properties(RLTrainer PROPERTIES OUTPUT_NAME "rltrainer")
    target_link_options(RLTrainer PRIVATE /DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO)

    if(RLT_INSTALL_TO_BAKKESMOD)
        add_custom_command(TARGET RLTrainer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:RLTrainer>
                    "$ENV{APPDATA}/bakkesmod/bakkesmod/plugins/rltrainer.dll"
            COMMENT "Copie de rltrainer.dll dans le dossier plugins de BakkesMod")
    endif()
endif()
