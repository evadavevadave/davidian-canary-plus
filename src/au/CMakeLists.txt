# SPDX-License-Identifier: GPL-3.0-or-later
# The Audio Unit (v2) version, for macOS. Fetches Apple's AudioUnitSDK (Apache-2.0)
# at configure time; to use a checkout you already have, pass
# -DFETCHCONTENT_SOURCE_DIR_AUDIOUNITSDK=<path>.
cmake_minimum_required(VERSION 3.24)
enable_language(CXX)
include(FetchContent)

# The SDK needs C++23 and macOS 11. This setting applies to this directory only;
# the LV2 build keeps its older minimum.
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)

FetchContent_Declare(audiounitsdk
    GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
    GIT_TAG AudioUnitSDK-1.4.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake) # sources only: built below
FetchContent_MakeAvailable(audiounitsdk)
set(AUSDK ${audiounitsdk_SOURCE_DIR})

set(AU_BUNDLE ${CMAKE_BINARY_DIR}/au/canary-plus.component)
set(AU_RESOURCES ${AU_BUNDLE}/Contents/Resources)
set(AU_EXECUTABLE canary-plus)
set(AU_BUNDLE_ID "io.github.evadavevadave.davidian-canary-plus.component") # matches au_ids.h
math(EXPR AU_VERSION_INT "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")
configure_file(Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)

set(AUSDK_SOURCES)
foreach(f AUBase AUBuffer AUBufferAllocator AUEffectBase AUInputElement AUOutputElement
          AUPlugInDispatch AUScopeElement ComponentBase)
    list(APPEND AUSDK_SOURCES ${AUSDK}/src/AudioUnitSDK/${f}.cpp)
endforeach()

add_library(canary-plus-au MODULE canary_au.cpp au_view.m ${AUSDK_SOURCES})
target_include_directories(canary-plus-au PRIVATE ${AUSDK}/include)
target_compile_options(canary-plus-au PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc -Wall -Wextra>)
target_link_libraries(canary-plus-au PRIVATE canary_dsp canary_editor
    "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio"
    "-framework CoreFoundation" "-framework Cocoa")
set_target_properties(canary-plus-au PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION component
    OUTPUT_NAME ${AU_EXECUTABLE}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/au>"
    CXX_STANDARD 23
    CXX_VISIBILITY_PRESET hidden
    OBJC_VISIBILITY_PRESET hidden)

# Documents go in before signing, since signing seals the bundle. The signature is
# ad hoc (no Apple Developer ID), which is what Apple Silicon needs to load it.
add_custom_command(TARGET canary-plus-au POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${AU_RESOURCES}
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/bundle/LICENSE.txt ${AU_RESOURCES}/LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/bundle/AI-DISCLOSURE.txt ${AU_RESOURCES}/AI-DISCLOSURE.txt
    COMMAND ${CMAKE_COMMAND} -E copy ${AUSDK}/LICENSE.txt ${AU_RESOURCES}/AudioUnitSDK-LICENSE.txt
    COMMAND codesign --force --sign - ${AU_BUNDLE}
    VERBATIM)

# Our own checks: loads the bundle in-process, renders audio, and drives the view.
add_executable(au_check ${PROJECT_SOURCE_DIR}/test/au_check.m)
target_compile_options(au_check PRIVATE -fno-objc-arc)
target_link_libraries(au_check PRIVATE
    "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio"
    "-framework CoreFoundation" "-framework Cocoa")
add_dependencies(au_check canary-plus-au)
add_test(NAME au_check COMMAND au_check ${AU_BUNDLE})
