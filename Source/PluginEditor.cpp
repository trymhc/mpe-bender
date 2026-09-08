#include "PluginEditor.h"

MpePianoRollAudioProcessorEditor::MpePianoRollAudioProcessorEditor(MpePianoRollAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), pianoRoll(p)
{
    setResizable(true, true);
    setSize(960, 640);
    setResizeLimits(600, 360, 2400, 1600);

    rollViewport.setViewedComponent(&pianoRoll, false);
    rollViewport.setScrollBarsShown(true, true);
    addAndMakeVisible(rollViewport);

    auto setupSlider = [this](juce::Slider& s, juce::Label& label, double min, double max, double value, double step)
    {
        s.setRange(min, max, step);
        s.setValue(value, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        addAndMakeVisible(s);
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        addAndMakeVisible(label);
    };

    setupSlider(loopLengthSlider, loopLabel, 1.0, 64.0, processor.getLoopLengthBeats(), 1.0);
    loopLengthSlider.onValueChange = [this]
    {
        processor.setLoopLengthBeats(loopLengthSlider.getValue());
        pianoRoll.setSize(PianoRollComponent::keyboardWidth
                               + (int) (processor.getLoopLengthBeats() * PianoRollComponent::pixelsPerBeat),
                           pianoRoll.getHeight());
    };

    setupSlider(pbRangeSlider, pbRangeLabel, 1.0, 96.0, processor.getPitchBendRangeSemitones(), 1.0);
    pbRangeSlider.onValueChange = [this]
    {
        processor.setPitchBendRangeSemitones((int) pbRangeSlider.getValue());
        pianoRoll.repaint();
    };

    setupSlider(channelsSlider, channelsLabel, 1.0, 14.0, processor.getNumMemberChannels(), 1.0);
    channelsSlider.onValueChange = [this]
    {
        processor.setNumMemberChannels((int) channelsSlider.getValue());
    };

    // --- hosted synth controls ---
    addAndMakeVisible(loadHostedButton);
    loadHostedButton.onClick = [this] { chooseHostedPlugin(); };

    addAndMakeVisible(openHostedButton);
    openHostedButton.onClick = [this] { toggleHostedWindow(); };

    addAndMakeVisible(forwardMidiButton);
    forwardMidiButton.setClickingTogglesState(true);
    forwardMidiButton.setToggleState(processor.getForwardHostMidi(), juce::dontSendNotification);
    forwardMidiButton.onClick = [this]
    {
        processor.setForwardHostMidi(forwardMidiButton.getToggleState());
    };

    hostedStatusLabel.setJustificationType(juce::Justification::centredLeft);
    hostedStatusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    addAndMakeVisible(hostedStatusLabel);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(13.0f);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    addAndMakeVisible(statusLabel);

    helpLabel.setJustificationType(juce::Justification::centredRight);
    helpLabel.setFont(11.0f);
    helpLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    helpLabel.setText("click empty: add note   -   drag body: move   -   drag edge: length   -   "
                      "ctrl-click ribbon: add bend point   -   drag point: bend   -   "
                      "dbl-click point: remove   -   drag diamond: curve   -   right-click: delete",
                      juce::dontSendNotification);
    addAndMakeVisible(helpLabel);

    refreshHostedUi();
    startTimerHz(10);
}

MpePianoRollAudioProcessorEditor::~MpePianoRollAudioProcessorEditor()
{
    stopTimer();
    hostedWindow.reset();
}

void MpePianoRollAudioProcessorEditor::chooseHostedPlugin()
{
    auto start = processor.getHostedPlugin().isLoaded()
                     ? processor.getHostedPlugin().getFile()
                     : MpePianoRollAudioProcessor::findLikelySerumFile();

    if (start == juce::File())
        start = juce::File("C:/Program Files/Common Files/VST3");

    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose a VST3 synth to host (Serum 2)", start, "*.vst3");

    auto browserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles
                      | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(browserFlags, [this](const juce::FileChooser& fc)
    {
        auto result = fc.getResult();
        if (result == juce::File())
            return;

        hostedWindow.reset();   // drop any editor for the old instance

        auto error = processor.loadHostedPlugin(result);
        if (error.isNotEmpty())
        {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Couldn't load that plugin", error);
        }

        refreshHostedUi();
    });
}

void MpePianoRollAudioProcessorEditor::toggleHostedWindow()
{
    if (hostedWindow != nullptr)
    {
        hostedWindow.reset();
        refreshHostedUi();
        return;
    }

    auto* instance = processor.getHostedPlugin().getInstance();
    if (instance == nullptr)
        return;

    hostedWindow = std::make_unique<HostedPluginWindow>(*instance);
    hostedWindow->onRequestClose = [this]
    {
        juce::MessageManager::callAsync([this]
        {
            hostedWindow.reset();
            refreshHostedUi();
        });
    };

    refreshHostedUi();
}

void MpePianoRollAudioProcessorEditor::refreshHostedUi()
{
    const bool loaded = processor.getHostedPlugin().isLoaded();

    openHostedButton.setEnabled(loaded);
    openHostedButton.setButtonText(hostedWindow != nullptr ? "Close synth UI" : "Open synth UI");

    if (loaded)
        hostedStatusLabel.setText("Synth: " + processor.getHostedPlugin().getDisplayName(),
                                  juce::dontSendNotification);
    else
        hostedStatusLabel.setText("Synth: none loaded - click \"Load Serum 2...\"",
                                  juce::dontSendNotification);

    forwardMidiButton.setToggleState(processor.getForwardHostMidi(), juce::dontSendNotification);
}

void MpePianoRollAudioProcessorEditor::timerCallback()
{
    juce::String statusText;
    switch (processor.getUiTransportStatus())
    {
        case MpePianoRollAudioProcessor::TransportStatus::noPlayHeadObject:
            statusText = "no playhead from host";
            break;
        case MpePianoRollAudioProcessor::TransportStatus::noPositionInfo:
            statusText = "playhead exists but has no position info";
            break;
        case MpePianoRollAudioProcessor::TransportStatus::stopped:
            statusText = "stopped";
            break;
        case MpePianoRollAudioProcessor::TransportStatus::missingTempoOrPpq:
            statusText = "playing, but missing tempo/beat position from host";
            break;
        case MpePianoRollAudioProcessor::TransportStatus::playing:
            statusText = "playing @ " + juce::String(processor.getUiBpm(), 1) + " BPM, beat "
                       + juce::String(processor.getUiPlayheadBeat(), 2);
            break;
    }

    statusLabel.setText("MIDI notes out: " + juce::String(processor.getUiNoteOnCount())
                             + " on / " + juce::String(processor.getUiNoteOffCount()) + " off"
                             + "   |   transport: " + statusText,
                         juce::dontSendNotification);

    refreshHostedUi();
}

void MpePianoRollAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121214));
}

void MpePianoRollAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto toolbar = area.removeFromTop(30).reduced(6, 3);

    auto placeControl = [&toolbar](juce::Label& label, juce::Slider& slider, int labelWidth, int sliderWidth)
    {
        label.setBounds(toolbar.removeFromLeft(labelWidth));
        slider.setBounds(toolbar.removeFromLeft(sliderWidth));
        toolbar.removeFromLeft(12);
    };

    placeControl(loopLabel, loopLengthSlider, 80, 130);
    placeControl(pbRangeLabel, pbRangeSlider, 84, 130);
    placeControl(channelsLabel, channelsSlider, 90, 120);

    auto synthBar = area.removeFromTop(28).reduced(6, 2);
    loadHostedButton.setBounds(synthBar.removeFromLeft(120));
    synthBar.removeFromLeft(6);
    openHostedButton.setBounds(synthBar.removeFromLeft(110));
    synthBar.removeFromLeft(6);
    forwardMidiButton.setBounds(synthBar.removeFromLeft(110));
    synthBar.removeFromLeft(10);
    hostedStatusLabel.setBounds(synthBar);

    auto infoBar = area.removeFromTop(18).reduced(6, 1);
    statusLabel.setBounds(infoBar.removeFromLeft(infoBar.getWidth() / 3));
    helpLabel.setBounds(infoBar);

    rollViewport.setBounds(area);
}
