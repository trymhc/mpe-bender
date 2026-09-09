cmake_minimum_required(VERSION 3.22)

project(MpePianoRoll VERSION 0.3.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically link the MSVC runtime so the shared .vst3 works on a friend's machine
# without them installing the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(JUCE)

juce_add_plugin(MpePianoRoll
    COMPANY_NAME "prodcoldie"
    # NB: don't change PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE - that changes the
    # plugin's unique ID and breaks every project that already uses it.
    PLUGIN_MANUFACTURER_CODE Ystu
    PLUGIN_CODE Mpe2
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # The build itself does NOT copy anywhere (writing to the system VST3 folder
    # needs admin and would fail the build). After building, run install.ps1 in an
    # elevated shell to push the .vst3 to C:\Program Files\Common Files\VST3, which
    # is the folder FL Studio actually scans.
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Instrument" "Synth"
    PRODUCT_NAME "MPE Bender")

target_sources(MpePianoRoll
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PianoRollComponent.cpp
        Source/MpeEngine.cpp
        Source/HostedPlugin.cpp
        Source/UpdateChecker.cpp)

# Built-in auto-updater feed. Default is this project's public repo; override with
#   cmake -S . -B build -DMPE_BENDER_UPDATE_URL="https://.../latest.json"
# or set it to "off" to disable (the Settings tab then shows "not configured").
# Normal dev builds without -D are still inert (latest.json version == this build).
set(MPE_BENDER_UPDATE_URL "https://raw.githubusercontent.com/trymhc/mpe-bender/main/latest.json"
    CACHE STRING "URL of the auto-update manifest (latest.json)")
if(MPE_BENDER_UPDATE_URL AND NOT MPE_BENDER_UPDATE_URL STREQUAL "off")
    target_compile_definitions(MpePianoRoll PRIVATE MPE_BENDER_UPDATE_MANIFEST_URL="${MPE_BENDER_UPDATE_URL}")
endif()

target_compile_definitions(MpePianoRoll
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        # Enable JUCE's plugin-hosting code so we can load Serum 2 (a VST3) inside us.
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_AU=0)

target_link_libraries(MpePianoRoll
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
