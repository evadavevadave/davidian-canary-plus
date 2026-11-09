# SPDX-License-Identifier: GPL-3.0-or-later
# The VST3 version. Fetches the VST3 SDK (MIT licence) at configure time; to use a
# checkout you already have, pass -DFETCHCONTENT_SOURCE_DIR_VST3SDK=<path>.
#
# This lives in its own directory because the SDK sets compiler options (such as
# fast floating-point math) for everything in the directory that includes it. The
# circuit model and the panel are built outside it, in the main CMakeLists.txt.
cmake_minimum_required(VERSION 3.25)
enable_language(CXX)
include(FetchContent)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF)
set(SMTG_CREATE_PLUGIN_LINK OFF)   # would need admin rights to link into the system VST3 folder
set(SMTG_RUN_VST_VALIDATOR OFF)    # run as a test instead, below
set(SMTG_CREATE_MODULE_INFO OFF)   # done below, for this bundle layout
set(SMTG_USE_STATIC_CRT ON)

FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.8.1_build_84
    GIT_SHALLOW TRUE
    GIT_SUBMODULES base cmake pluginterfaces public.sdk)
FetchContent_MakeAvailable(vst3sdk)
smtg_enable_vst3_sdk()

# Bundle layout from the VST3 specification: canary-plus.vst3/Contents/<arch>-win/canary-plus.vst3
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
    set(VST3_ARCH arm64-win)
else()
    set(VST3_ARCH x86_64-win)
endif()
set(VST3_BUNDLE ${CMAKE_BINARY_DIR}/vst3/canary-plus.vst3)
set(VST3_RESOURCES ${VST3_BUNDLE}/Contents/Resources)

add_library(canary-plus-vst3 MODULE
    canary_vst3.cpp
    version.rc
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/dllmain.cpp)
target_link_libraries(canary-plus-vst3 PRIVATE sdk canary_dsp canary_editor)
target_compile_definitions(canary-plus-vst3 PRIVATE CP_VERSION_STR="${PROJECT_VERSION}")
set_target_properties(canary-plus-vst3 PROPERTIES
    OUTPUT_NAME canary-plus
    PREFIX ""
    SUFFIX .vst3
    CXX_STANDARD 17
    LIBRARY_OUTPUT_DIRECTORY "$<1:${VST3_BUNDLE}/Contents/${VST3_ARCH}>")

# moduleinfo.json lets hosts list the plugin without loading it.
add_dependencies(canary-plus-vst3 moduleinfotool)
add_custom_command(TARGET canary-plus-vst3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VST3_RESOURCES}
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/bundle/LICENSE.txt ${VST3_RESOURCES}/LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/bundle/AI-DISCLOSURE.txt ${VST3_RESOURCES}/AI-DISCLOSURE.txt
    COMMAND ${CMAKE_COMMAND} -E copy ${vst3sdk_SOURCE_DIR}/LICENSE.txt ${VST3_RESOURCES}/VST3-SDK-LICENSE.txt
    COMMAND $<TARGET_FILE:moduleinfotool> -create -version ${PROJECT_VERSION}
            -path ${VST3_BUNDLE} -output ${VST3_RESOURCES}/moduleinfo.json
    VERBATIM)

# Steinberg's validator: loads the bundle and runs the SDK's conformance tests.
add_test(NAME vst3_validator COMMAND validator ${VST3_BUNDLE})

# Our own checks of the sound, bypass, state and editor, as a host would use them.
add_executable(vst3_check
    ${PROJECT_SOURCE_DIR}/test/vst3_check.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_link_libraries(vst3_check PRIVATE sdk_hosting user32 gdi32)
set_target_properties(vst3_check PROPERTIES CXX_STANDARD 17)
add_dependencies(vst3_check canary-plus-vst3)
add_test(NAME vst3_check COMMAND vst3_check ${VST3_BUNDLE})

set(VST3_BUNDLE ${VST3_BUNDLE} PARENT_SCOPE)
