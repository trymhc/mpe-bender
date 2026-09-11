#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PianoRollComponent.h"
#include "KeyboardSidebar.h"
#include "HostedPluginWindow.h"
#include "UiTheme.h"
#include "UpdateChecker.h"
#include "SynthLibrary.h"
#include "MidiIo.h"
#include <memory>
#include <functional>

class MpePianoRollAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit MpePianoRollAudioProcessorEditor(MpePianoRollAudioProcessor&);
    ~MpePianoRollAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void chooseHostedPlugin();
    void toggleHostedWindow();
    void refreshHostedUi();

    // synth quick-switch list
    void refreshSynthBox();
    void loadSynthEntry(const SynthLibrary::Entry&);
    void promptSynthName(juce::File file, juce::String initialName,
                         std::function<void(juce::String)> onAccept);

    void importMidi();

    enum class Tab { roll, settings };
    void showTab(Tab t);
    void toggleFullscreen();

    MpePianoRollAudioProcessor& processor;
    FlatLookAndFeel flatLnf;
    juce::TooltipWindow tooltipWindow { this, 650 };
    Tab currentTab = Tab::roll;
    bool autoOpenSynthWindow = true;
    bool lastKnownLoaded = false;

    juce::TextButton rollTabButton { "Roll" };
    juce::TextButton settingsTabButton { "Settings" };

    juce::Viewport rollViewport;
    PianoRollComponent pianoRoll;
    KeyboardSidebar keyboardSidebar { pianoRoll, rollViewport };

    juce::TextButton zoomResetButton { "1:1" };
    juce::TextButton fullscreenButton { "Fullscreen" };
    bool isFullscreen = false;
    int  windowedW = 960, windowedH = 640;

    juce::Label loopLabel { {}, "Loop (bars)" };
    juce::Slider loopLengthSlider;
    juce::Label channelsLabel { {}, "MPE Channels" };
    juce::Slider channelsSlider;

    // --- Settings tab ---
    juce::Label pbRangeLabel { {}, "Pitch-bend range (semitones)" };
    juce::Slider pbRangeSlider;
    juce::Label pbRangeHelp;

    juce::Label themeLabel { {}, "Theme" };
    juce::ComboBox themeBox;
    void applyTheme(Theme::Id id, bool store);

    juce::Label scaleLabel { {}, "Scale" };
    juce::ComboBox scaleRootBox, scaleTypeBox;
    juce::TextButton snapToScaleButton { "Snap" };
    juce::TextButton importMidiButton { "Import MIDI" };

    juce::Label updateLabel { {}, "Updates" };
    juce::Label updateStatusLabel;
    juce::TextButton checkUpdateButton { "Check now" };
    juce::TextButton installUpdateButton { "Install update" };
    UpdateChecker updateChecker;
    void refreshUpdateUi();

    SynthLibrary synthLibrary;
    juce::ComboBox synthBox;
    juce::TextButton openHostedButton { "Open synth" };
    juce::TextButton freeRunButton { "Free run" };
    juce::TextButton forwardMidiButton { "Fwd host MIDI" };

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<HostedPluginWindow> hostedWindow;

    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MpePianoRollAudioProcessorEditor)
};
