#include "PluginEditor.h"

MpePianoRollAudioProcessorEditor::MpePianoRollAudioProcessorEditor(MpePianoRollAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), pianoRoll(p)
{
    setLookAndFeel(&flatLnf);

    setResizable(true, true);
    setSize(960, 640);
    setResizeLimits(720, 360, 2400, 1600);

    for (auto* b : { &rollTabButton, &settingsTabButton })
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(2001);
        addAndMakeVisible(*b);
    }
    rollTabButton.setToggleState(true, juce::dontSendNotification);
    rollTabButton.onClick     = [this] { showTab(Tab::roll); };
    settingsTabButton.onClick  = [this] { showTab(Tab::settings); };

    rollViewport.setViewedComponent(&pianoRoll, false);
    rollViewport.setScrollBarsShown(true, true);
    addAndMakeVisible(rollViewport);
    addAndMakeVisible(keyboardSidebar);   // on top of the viewport's left edge

    addAndMakeVisible(zoomResetButton);
    zoomResetButton.onClick = [this] { pianoRoll.resetZoom(); };

    auto setupSlider = [this](juce::Slider& s, juce::Label& label, double min, double max, double value, double step)
    {
        s.setRange(min, max, step);
        s.setValue(value, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 20);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.9f));
        addAndMakeVisible(s);
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        addAndMakeVisible(label);
    };

    setupSlider(loopLengthSlider, loopLabel, 1.0, 32.0, processor.getLoopLengthBeats() / 4.0, 1.0);
    loopLengthSlider.onValueChange = [this]
    {
        processor.setLoopLengthBeats(loopLengthSlider.getValue() * 4.0);   // slider is in bars (4/4)
        pianoRoll.updateContentSize();
    };

    setupSlider(pbRangeSlider, pbRangeLabel, 1.0, 96.0, processor.getPitchBendRangeSemitones(), 1.0);
    pbRangeSlider.onValueChange = [this]
    {
        processor.setPitchBendRangeSemitones((int) pbRangeSlider.getValue());
        pianoRoll.repaint();
    };
    pbRangeLabel.setJustificationType(juce::Justification::centredLeft);

    pbRangeHelp.setJustificationType(juce::Justification::topLeft);
    pbRangeHelp.setFont(12.0f);
    pbRangeHelp.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    pbRangeHelp.setText("Must match the pitch-bend range set in the hosted synth. In Serum 2, "
                        "turn MPE on and set the same value there. Default 48.",
                        juce::dontSendNotification);
    addAndMakeVisible(pbRangeHelp);

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

    showTab(Tab::roll);
    refreshHostedUi();
    startTimerHz(10);
}

MpePianoRollAudioProcessorEditor::~MpePianoRollAudioProcessorEditor()
{
    stopTimer();
    hostedWindow.reset();
    setLookAndFeel(nullptr);
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
    openHostedButton.setButtonText(hostedWindow != nullptr ? "Close synth" : "Open synth");

    hostedStatusLabel.setText(loaded ? "Synth: " + processor.getHostedPlugin().getDisplayName()
                                     : "Synth: none",
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
    g.fillAll(Theme::panel);
}

void MpePianoRollAudioProcessorEditor::showTab(Tab t)
{
    currentTab = t;
    const bool roll = t == Tab::roll;

    juce::Component* rollBits[] = { &rollViewport, &keyboardSidebar, &zoomResetButton,
                                   &loopLabel, &loopLengthSlider, &channelsLabel, &channelsSlider,
                                   &loadHostedButton, &openHostedButton, &forwardMidiButton,
                                   &hostedStatusLabel, &statusLabel };
    for (auto* c : rollBits)
        c->setVisible(roll);

    juce::Component* settingsBits[] = { &pbRangeLabel, &pbRangeSlider, &pbRangeHelp };
    for (auto* c : settingsBits)
        c->setVisible(! roll);

    (roll ? rollTabButton : settingsTabButton).setToggleState(true, juce::dontSendNotification);
    resized();
}

void MpePianoRollAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto tabBar = area.removeFromTop(24).reduced(6, 3);
    rollTabButton.setBounds(tabBar.removeFromLeft(56));
    tabBar.removeFromLeft(3);
    settingsTabButton.setBounds(tabBar.removeFromLeft(66));

    if (currentTab == Tab::settings)
    {
        auto s = area.reduced(18, 14);
        pbRangeLabel.setBounds(s.removeFromTop(20));
        s.removeFromTop(4);
        pbRangeSlider.setBounds(s.removeFromTop(24).withWidth(juce::jmin(360, s.getWidth())));
        s.removeFromTop(8);
        pbRangeHelp.setBounds(s.removeFromTop(40).withWidth(juce::jmin(420, s.getWidth())));
        return;
    }

    auto toolbar = area.removeFromTop(30).reduced(6, 3);
    zoomResetButton.setBounds(toolbar.removeFromRight(38));
    toolbar.removeFromRight(14);

    auto placeControl = [&toolbar](juce::Label& label, juce::Slider& slider, int labelWidth, int sliderWidth)
    {
        label.setBounds(toolbar.removeFromLeft(labelWidth));
        slider.setBounds(toolbar.removeFromLeft(sliderWidth));
        toolbar.removeFromLeft(16);
    };
    placeControl(loopLabel, loopLengthSlider, 74, 130);
    placeControl(channelsLabel, channelsSlider, 92, 118);

    auto synthBar = area.removeFromTop(28).reduced(6, 2);
    loadHostedButton.setBounds(synthBar.removeFromLeft(110));
    synthBar.removeFromLeft(6);
    openHostedButton.setBounds(synthBar.removeFromLeft(100));
    synthBar.removeFromLeft(6);
    forwardMidiButton.setBounds(synthBar.removeFromLeft(110));
    synthBar.removeFromLeft(10);
    hostedStatusLabel.setBounds(synthBar);

    statusLabel.setBounds(area.removeFromTop(18).reduced(6, 1));

    rollViewport.setBounds(area);
    pianoRoll.updateContentSize();

    const int sbThick = rollViewport.getScrollBarThickness();
    keyboardSidebar.setBounds(area.getX(), area.getY(),
                              PianoRollComponent::keyboardWidth, area.getHeight() - sbThick);
}
