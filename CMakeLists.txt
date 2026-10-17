# Mz909 - an emulation of the TR-909 Rhythm Composer
# Created by Simon Moscrop
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU Affero General Public License as published by the Free Software Foundation, version 3.
# It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

cmake_minimum_required(VERSION 3.22)

project(Mz909 VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#
# JUCE lives beside the repository rather than inside it, shared with every other JUCE
# project on the machine. Override with -DJUCE_PATH=... if it is somewhere else.
#
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Where JUCE is checked out")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "No JUCE at ${JUCE_PATH}.\n"
        "  cd ${CMAKE_CURRENT_SOURCE_DIR}/..\n"
        "  git clone --depth 1 https://github.com/juce-framework/JUCE\n"
        "or pass -DJUCE_PATH=<wherever it is>.")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

#
# The per-user VST3 folder, which needs no elevation - JUCE's default of
# C:\Program Files\Common Files\VST3 cannot be written without running as administrator.
#
if(WIN32)
    set(VST3_DEFAULT "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
elseif(APPLE)
    set(VST3_DEFAULT "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(VST3_DEFAULT "$ENV{HOME}/.vst3")
endif()

set(VST3_DESTINATION "${VST3_DEFAULT}" CACHE PATH "Where to install the built VST3")

#
# The copyright, written down once: the binaries' version resource, the panel, and through
# build.ps1 the installer and the release READMEs all take it from here.
#
set(MZ909_COPYRIGHT "Created by Simon Moscrop")

set(MZ909_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MZ909_FORMATS AU)
endif()

#
# PRODUCT_NAME is what a player sees. "Mz909" rather than "TR-909": Roland and TR-909 are
# Roland's trademarks, and this is an independent emulation.
#
# The two codes are the plugin's IDENTITY and must never change once anyone has saved a
# project with it - a VST3's class ID is made from them, and a host finds the plugin in a
# saved set by that ID.
#
juce_add_plugin(Mz909
    COMPANY_NAME                "simozzer"
    COMPANY_COPYRIGHT           "${MZ909_COPYRIGHT}"
    PRODUCT_NAME                "Mz909"
    PLUGIN_MANUFACTURER_CODE    Smoz          # never change - see above
    PLUGIN_CODE                 Tr09          # never change - see above

    # JUCE does not inherit this from project().
    VERSION                     "${PROJECT_VERSION}"

    VST3_COPY_DIR               "${VST3_DESTINATION}"
    VST3_CATEGORIES             Instrument Drum

    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    # Installed by install_vst3.cmake instead, so a host holding the old binary open is a
    # message rather than a failed build.
    COPY_PLUGIN_AFTER_BUILD     FALSE

    FORMATS                     ${MZ909_FORMATS})

target_sources(Mz909 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/Presets.cpp
    Source/TR909/Engine.cpp
    Source/TR909/Roms.cpp)

target_include_directories(Mz909 PRIVATE Source)

add_custom_command(TARGET Mz909_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DSRC=${CMAKE_CURRENT_BINARY_DIR}/Mz909_artefacts/$<CONFIG>/VST3/Mz909.vst3"
            "-DDST=${VST3_DESTINATION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install_vst3.cmake"
    VERBATIM)

target_compile_definitions(Mz909 PUBLIC
    "MZ909_COPYRIGHT=\"${MZ909_COPYRIGHT}\""
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Mz909
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#
# The engine's own check. It links no JUCE: the circuit model is plain C++, so it can be
# held to pitch, decay and level before a plugin is wrapped round it.
#
add_executable(DspCheck
    Tests/DspCheck.cpp
    Source/TR909/Engine.cpp
    Source/TR909/Roms.cpp)

target_include_directories(DspCheck PRIVATE Source/TR909)

enable_testing()
add_test(NAME dsp COMMAND DspCheck)
