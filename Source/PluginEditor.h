#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PianoRollComponent.h"
#include "HostedPluginWindow.h"
#include "UiTheme.h"
#include <memory>

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

    MpePianoRollAudioProcessor& processor;
    FlatLookAndFeel flatLnf;

    juce::Viewport rollViewport;
    PianoRollComponent pianoRoll;

    juce::TextButton zoomResetButton { "1:1" };

    juce::Label loopLabel { {}, "Loop (bars)" };
    juce::Slider loopLengthSlider;
    juce::Label pbRangeLabel { {}, "PB Range (st)" };
    juce::Slider pbRangeSlider;
    juce::Label channelsLabel { {}, "MPE Channels" };
    juce::Slider channelsSlider;

    juce::TextButton loadHostedButton { "Load Serum 2..." };
    juce::TextButton openHostedButton { "Open synth UI" };
    juce::TextButton forwardMidiButton { "Fwd host MIDI" };
    juce::Label hostedStatusLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<HostedPluginWindow> hostedWindow;

    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MpePianoRollAudioProcessorEditor)
};
