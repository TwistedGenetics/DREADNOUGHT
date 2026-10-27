# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT as a plugin: AU, VST3 and a standalone app, built with JUCE around the C core (plugin/core: the app and
# the engines the FM-1 runs). Copyright (C) 2026 Jamie (TwistedGenetics).
#
#   cmake -S plugin -B build/plugin -DCMAKE_BUILD_TYPE=Release && cmake --build build/plugin --config Release
#
# JUCE is fetched (8.0.9, AGPLv3 / commercial) unless -DDN_JUCE_DIR=path says where a copy is. Python 3 with numpy
# and Pillow makes the tables the engines read (as the firmware's build does).
cmake_minimum_required(VERSION 3.22)

get_filename_component(DN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(STRINGS "${DN_ROOT}/VERSION" DN_VERSION LIMIT_COUNT 1)
string(STRIP "${DN_VERSION}" DN_VERSION)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
project(DREADNOUGHT_PLUGIN VERSION ${DN_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DN_X11_MIN "Linux: no XRandR / Xinerama / Xcursor / ALSA (a build machine without their headers)" OFF)

# ---- JUCE ----
set(DN_JUCE_DIR "" CACHE PATH "a JUCE checkout (else fetched)")
if(DN_JUCE_DIR)
    add_subdirectory("${DN_JUCE_DIR}" juce)
else()
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# ---- the tables the engines read (made as tests/run_host.sh makes them) ----
# (-DDN_GEN_DIR=dir: the tables made elsewhere, as CI makes them once on Linux: gen_font.py needs Pillow with libraqm,
#  which macOS and Windows wheels lack: brew install libraqm, or take CI's "tables" artifact)
set(DN_GEN_DIR "${CMAKE_BINARY_DIR}/gen" CACHE PATH "the generated tables (made here when missing)")
set(DN_GEN "${DN_GEN_DIR}")
file(MAKE_DIRECTORY "${DN_GEN}")
foreach(g "tools/gen_dn_tables.py;dn_tables.h" "tools/gen_dn_wt.py;dn_wt_tables.h"
          "tools/gen_dn_breaks.py;dn_breaks_builtin.h" "x0x-firmware/tools/gen_font.py;felucca_font.h")
    list(GET g 0 script)
    list(GET g 1 out)
    if(NOT EXISTS "${DN_GEN}/${out}")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${DN_ROOT}/${script}" "${DN_GEN}/${out}"
                        WORKING_DIRECTORY "${DN_ROOT}" RESULT_VARIABLE rc OUTPUT_QUIET)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "${script} failed (Python 3 with numpy and Pillow?)")
        endif()
    endif()
endforeach()

# ---- the C core ----
set(DN_DSP reese dn_seq dn_breaks dn_lfo dn_fx dn_fxc dn_fxq dn_fxm dn_fxl dn_sub dn_smp dn_fm dn_wt dn_ju)
list(TRANSFORM DN_DSP PREPEND "${DN_ROOT}/src/dsp/")
list(TRANSFORM DN_DSP APPEND ".c")
add_library(dn_core STATIC "${DN_ROOT}/plugin/core/dn_core.c" ${DN_DSP})
set_target_properties(dn_core PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(dn_core PRIVATE "${DN_GEN}" "${DN_ROOT}/src/dsp" "${DN_ROOT}/src/app"
                                           "${DN_ROOT}/x0x-firmware/firmware/src")
target_include_directories(dn_core PUBLIC "${DN_ROOT}/plugin/core")
target_compile_definitions(dn_core PRIVATE DN_VERSION="${DN_VERSION}" _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(dn_core PRIVATE /W0 /fp:precise)
else()
    target_compile_options(dn_core PRIVATE -w -ffp-contract=off)
endif()

# ---- the plugin ----
juce_add_plugin(Dreadnought
    PRODUCT_NAME "DREADNOUGHT"
    COMPANY_NAME "TwistedGenetics"
    BUNDLE_ID com.twistedgenetics.dreadnought
    PLUGIN_MANUFACTURER_CODE Twgn
    PLUGIN_CODE Drnt
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    DESCRIPTION "Jungle and drum & bass groovebox (the FM-1 firmware)")
target_sources(Dreadnought PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_compile_definitions(Dreadnought PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    DN_VERSION_STR="${DN_VERSION}")
if(DN_X11_MIN)
    target_compile_definitions(Dreadnought PUBLIC JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_ALSA=0
                                                  JUCE_JACK=0)
endif()
target_link_libraries(Dreadnought PRIVATE dn_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- the editor drawn to a PNG (the tests and the manual: no host needed) ----
option(DN_SHOT "build dn_shot (the editor to a PNG)" ON)
if(DN_SHOT)
    juce_add_console_app(dn_shot PRODUCT_NAME "dn_shot")
    target_sources(dn_shot PRIVATE src/Shot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
    target_compile_definitions(dn_shot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 DN_VERSION_STR="${DN_VERSION}"
                                               JucePlugin_Name="DREADNOUGHT" DN_NO_PLUGIN_ENTRY=1
                                               DN_NO_EDITOR_PORT=1)
    if(DN_X11_MIN)
        target_compile_definitions(dn_shot PRIVATE JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
    endif()
    target_link_libraries(dn_shot PRIVATE dn_core juce::juce_audio_processors juce::juce_gui_basics
                                          juce::juce_recommended_config_flags)
endif()
