cmake_minimum_required(VERSION 3.22)

project(MpePianoRoll VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically link the MSVC runtime so the shared .vst3 works on a friend's machine
# without them installing the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(JUCE)

juce_add_plugin(MpePianoRoll
    COMPANY_NAME "YourStudio"
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

# Point the built-in auto-updater at your hosted manifest, e.g.:
#   cmake -S . -B build -DMPE_BENDER_UPDATE_URL="https://raw.githubusercontent.com/you/mpe-bender/main/latest.json"
# Leave unset to disable auto-update (the Settings tab shows "not configured").
if(DEFINED MPE_BENDER_UPDATE_URL)
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
