cmake_minimum_required(VERSION 3.22)

# macOS 11 (Big Sur) on Intel is the minimum; build universal (x86_64 + arm64).
# No -march flags anywhere: the DSP is scalar C++ and runs on any x86-64 (no AVX2 needed).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()
project(ChopShop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Core engine (no JUCE dependency, unit-testable) ----
add_library(chopshop_engine STATIC
    Source/engine/Analysis.cpp
    Source/engine/Pattern.cpp
    Source/engine/Engine.cpp
    Source/engine/Breaks.cpp
    Source/engine/Presets.cpp)
target_include_directories(chopshop_engine PUBLIC Source/engine)
set_target_properties(chopshop_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(CHOPSHOP_BUILD_TESTS "Build engine tests" ON)
if(CHOPSHOP_BUILD_TESTS)
    add_executable(chopshop_tests tests/EngineTests.cpp)
    target_link_libraries(chopshop_tests PRIVATE chopshop_engine)
    enable_testing()
    add_test(NAME engine COMMAND chopshop_tests)
endif()

option(CHOPSHOP_BUILD_PLUGIN "Build the JUCE plugins" ON)
if(NOT CHOPSHOP_BUILD_PLUGIN)
    return()
endif()

# ---- JUCE ----
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(CHOPSHOP_PLUGIN_SOURCES
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    Source/plugin/Themes.cpp)

function(chopshop_setup target fx)
    target_sources(${target} PRIVATE ${CHOPSHOP_PLUGIN_SOURCES})
    target_compile_definitions(${target} PUBLIC
        CHOPSHOP_FX=${fx}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target}
        PRIVATE
            chopshop_engine
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

set(CHOPSHOP_FORMATS VST3 Standalone)
set(CHOPSHOP_FX_FORMATS VST3)
if(APPLE)
    list(APPEND CHOPSHOP_FORMATS AU)
    list(APPEND CHOPSHOP_FX_FORMATS AU)
endif()

# Instrument: load a break, play slices from MIDI, pattern player.
juce_add_plugin(ChopShop
    COMPANY_NAME "Chop Shop Audio"
    PLUGIN_MANUFACTURER_CODE Chps
    PLUGIN_CODE Chp1
    FORMATS ${CHOPSHOP_FORMATS}
    PRODUCT_NAME "Chop Shop"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)
chopshop_setup(ChopShop 0)

# Effect: captures the track's audio for N bars, then slices and mangles it.
juce_add_plugin(ChopShopFX
    COMPANY_NAME "Chop Shop Audio"
    PLUGIN_MANUFACTURER_CODE Chps
    PLUGIN_CODE Chp2
    FORMATS ${CHOPSHOP_FX_FORMATS}
    PRODUCT_NAME "Chop Shop FX"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)
chopshop_setup(ChopShopFX 1)

# ---- UI snapshot + plugin smoke test (renders the editor to PNG for docs; not shipped) ----
option(CHOPSHOP_BUILD_SNAPSHOT "Build the UI snapshot / smoke-test tool" OFF)
if(CHOPSHOP_BUILD_SNAPSHOT)
    juce_add_console_app(ChopShopSnapshot PRODUCT_NAME "ChopShopSnapshot")
    target_sources(ChopShopSnapshot PRIVATE tools/Snapshot.cpp ${CHOPSHOP_PLUGIN_SOURCES})
    target_compile_definitions(ChopShopSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 CHOPSHOP_FX=0
        JucePlugin_Name="Chop Shop" JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChopShopSnapshot PRIVATE chopshop_engine juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
