# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Davidian Canary Plus LV2 bundle and its tests on Windows, macOS and Linux.
#
#   cmake -B out -DCMAKE_BUILD_TYPE=Release
#   cmake --build out --config Release
#   ctest --test-dir out -C Release --output-on-failure
#
# The bundle lands in out/lv2/canary-plus.lv2. On Linux the GUI needs the X11
# development files (libx11-dev), and its test needs a display (xvfb-run).
# For a macOS build that runs on both Intel and Apple Silicon, add
#   -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake_minimum_required(VERSION 3.16)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS the build runs on")
project(canary_plus VERSION 1.2.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(LILV_CHECK "Also check the bundle with the lilv LV2 parser (loads lilv at run time)" OFF)
option(VST3 "Also build the VST3 version (Windows; downloads the VST3 SDK)" ${WIN32})
option(AU "Also build the Audio Unit version (macOS; downloads Apple's AudioUnitSDK)" ${APPLE})
if(VST3 AND NOT WIN32)
    message(FATAL_ERROR "The VST3 version is built for Windows only")
endif()
if(AU AND NOT APPLE)
    message(FATAL_ERROR "Audio Units are macOS only")
endif()

# ---- Platform: library suffix and GUI type ----
if(WIN32)
    set(LIB_SUFFIX .dll)
    set(UI_CLASS ui:WindowsUI)
    set(WITH_UI ON)
elseif(APPLE)
    enable_language(OBJC)
    set(LIB_SUFFIX .dylib)
    set(UI_CLASS ui:CocoaUI)
    set(WITH_UI ON)
else()
    set(LIB_SUFFIX .so)
    set(UI_CLASS ui:X11UI)
    find_package(X11)
    set(WITH_UI ${X11_FOUND})
    if(NOT X11_FOUND)
        message(WARNING "X11 development files not found: building without the GUI")
    endif()
endif()

set(LV2_DIR ${CMAKE_BINARY_DIR}/lv2)
set(BUNDLE_DIR ${LV2_DIR}/canary-plus.lv2)

# Static C runtime on Windows, so hosts don't need a redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

function(plugin_library target)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        SUFFIX ${LIB_SUFFIX}
        C_STANDARD 11
        C_VISIBILITY_PRESET hidden
        OBJC_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>")
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:/W4 /fp:precise>)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

# ---- The plugin ----
add_library(canary-plus MODULE src/plugin.c)
if(WIN32)
    target_sources(canary-plus PRIVATE src/version.rc)
endif()
plugin_library(canary-plus)

# ---- The GUI ----
if(WITH_UI)
    add_library(canary-plus_ui MODULE src/ui/ui_lv2.c)
    plugin_library(canary-plus_ui)
    if(WIN32)
        target_sources(canary-plus_ui PRIVATE src/ui/version.rc)
        target_link_libraries(canary-plus_ui PRIVATE user32 gdi32)
    elseif(APPLE)
        set_source_files_properties(src/ui/ui_lv2.c PROPERTIES LANGUAGE OBJC)
        target_compile_options(canary-plus_ui PRIVATE -fno-objc-arc)
        target_link_libraries(canary-plus_ui PRIVATE "-framework Cocoa")
    else()
        target_include_directories(canary-plus_ui PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(canary-plus_ui PRIVATE ${X11_LIBRARIES})
    endif()
endif()

# ---- Bundle metadata ----
# The manifest is written for Windows; other platforms get their suffix and GUI type.
file(READ bundle/manifest.ttl MANIFEST)
string(REPLACE "canary-plus.dll" "canary-plus${LIB_SUFFIX}" MANIFEST "${MANIFEST}")
string(REPLACE "canary-plus_ui.dll" "canary-plus_ui${LIB_SUFFIX}" MANIFEST "${MANIFEST}")
if(WITH_UI)
    string(REPLACE "ui:WindowsUI" "${UI_CLASS}" MANIFEST "${MANIFEST}")
else()
    string(REGEX REPLACE "# UI-BEGIN.*# UI-END\n?" "" MANIFEST "${MANIFEST}")
endif()
file(WRITE ${BUNDLE_DIR}/manifest.ttl "${MANIFEST}")
foreach(DOC canary-plus.ttl LICENSE.txt AI-DISCLOSURE.txt)
    configure_file(bundle/${DOC} ${BUNDLE_DIR}/${DOC} COPYONLY)
endforeach()
message(STATUS "Davidian Canary Plus ${PROJECT_VERSION}: GPL-3.0-or-later, written with AI assistance "
               "(Claude, by Anthropic); see AI-DISCLOSURE.txt")

# ---- Shared parts of the VST3 and Audio Unit versions ----
# The circuit model (plugin.c, with its LV2 entry point kept internal) and the
# panel, built as C here so the plugin SDKs' compiler options don't reach them.
enable_testing()
if(VST3 OR AU)
    add_library(canary_dsp STATIC src/plugin.c)
    target_compile_definitions(canary_dsp PUBLIC DP_EMBEDDED)
    set_target_properties(canary_dsp PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON
                                                C_VISIBILITY_PRESET hidden)
    if(MSVC)
        target_compile_options(canary_dsp PRIVATE /W4 /fp:precise)
    else()
        target_compile_options(canary_dsp PRIVATE -Wall -Wextra)
        target_link_libraries(canary_dsp PUBLIC m)
    endif()

    add_library(canary_editor STATIC src/ui/editor.c)
    set_target_properties(canary_editor PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON
                                                   C_VISIBILITY_PRESET hidden OBJC_VISIBILITY_PRESET hidden)
    if(MSVC)
        target_compile_options(canary_editor PRIVATE /W4)
        target_link_libraries(canary_editor PUBLIC user32 gdi32)
    else()
        target_compile_options(canary_editor PRIVATE -Wall -Wextra)
        target_link_libraries(canary_editor PUBLIC m)
    endif()
    if(APPLE)
        set_source_files_properties(src/ui/editor.c PROPERTIES LANGUAGE OBJC)
        target_compile_options(canary_editor PRIVATE -fno-objc-arc)
        # Each binary in a process needs its own Objective-C class names.
        target_compile_definitions(canary_editor PRIVATE PANEL_VIEW_CLASS=DavidianCanaryPlusAUPanel)
        target_link_libraries(canary_editor PUBLIC "-framework Cocoa")
    endif()
endif()

if(VST3)
    add_subdirectory(src/vst3)
endif()
if(AU)
    add_subdirectory(src/au)
endif()

# ---- Tests ----
function(test_program target)
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES C_STANDARD 11)
    if(NOT MSVC)
        target_link_libraries(${target} PRIVATE m ${CMAKE_DL_LIBS})
    endif()
endfunction()

test_program(test_dsp test/test_dsp.c)
add_test(NAME test_dsp COMMAND test_dsp ${BUNDLE_DIR})

test_program(ui_render test/ui_render.c)
add_test(NAME ui_render COMMAND ui_render ${CMAKE_BINARY_DIR}/panel.bmp)

if(WITH_UI)
    test_program(ui_check test/ui_check.c)
    if(WIN32)
        target_link_libraries(ui_check PRIVATE user32 gdi32)
    elseif(APPLE)
        set_source_files_properties(test/ui_check.c PROPERTIES LANGUAGE OBJC)
        target_compile_options(ui_check PRIVATE -fno-objc-arc)
        target_link_libraries(ui_check PRIVATE "-framework Cocoa" "-framework CoreGraphics")
    else()
        target_include_directories(ui_check PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(ui_check PRIVATE ${X11_LIBRARIES})
    endif()
    add_test(NAME ui_check COMMAND ui_check ${BUNDLE_DIR})
endif()

if(LILV_CHECK)
    test_program(lilv_check test/lilv_check.c)
    add_test(NAME lilv_check COMMAND lilv_check)
    set_tests_properties(lilv_check PROPERTIES ENVIRONMENT "LV2_PATH=${LV2_DIR}")
endif()
