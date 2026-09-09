#include "PluginEditor.h"

MpePianoRollAudioProcessorEditor::MpePianoRollAudioProcessorEditor(MpePianoRollAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), pianoRoll(p)
{
    Theme::apply((Theme::Id) juce::jlimit(0, 2, processor.getThemeId()));
    flatLnf.syncColours();
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

    zoomResetButton.onClick = [this] { pianoRoll.resetZoom(); };
    zoomResetButton.setTooltip("Reset the piano-roll zoom to 1:1.");

    auto setupSlider = [this](juce::Slider& s, juce::Label& label, double min, double max, double value, double step)
    {
        s.setRange(min, max, step);
        s.setValue(value, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 20);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId, Theme::text);
        addAndMakeVisible(s);
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible(label);
    };

    // A plain editable number with tiny +/- steppers (no track to drag).
    auto setupNumber = [this](juce::Slider& s, juce::Label& label, double min, double max, double value)
    {
        s.setRange(min, max, 1.0);
        s.setValue(value, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::IncDecButtons);
        s.setIncDecButtonsMode(juce::Slider::incDecButtonsNotDraggable);
        s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 22);
        s.setColour(juce::Slider::textBoxOutlineColourId, Theme::separator);
        s.setColour(juce::Slider::textBoxBackgroundColourId, Theme::field);
        s.setColour(juce::Slider::textBoxTextColourId, Theme::text);
        addAndMakeVisible(s);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible(label);
    };

    setupNumber(loopLengthSlider, loopLabel, 4.0, 16.0,
                juce::jlimit(4.0, 16.0, processor.getLoopLengthBeats() / 4.0));
    processor.setLoopLengthBeats(loopLengthSlider.getValue() * 4.0);
    loopLengthSlider.onValueChange = [this]
    {
        processor.setLoopLengthBeats(loopLengthSlider.getValue() * 4.0);   // number is in bars (4/4)
        pianoRoll.updateContentSize();
    };

    setupNumber(pbRangeSlider, pbRangeLabel, 1.0, 96.0, processor.getPitchBendRangeSemitones());
    pbRangeSlider.onValueChange = [this]
    {
        processor.setPitchBendRangeSemitones((int) pbRangeSlider.getValue());
        pianoRoll.repaint();
    };

    pbRangeHelp.setJustificationType(juce::Justification::topLeft);
    pbRangeHelp.setFont(12.0f);
    pbRangeHelp.setColour(juce::Label::textColourId, Theme::textDim);
    pbRangeHelp.setText("Must match the pitch-bend range set in the hosted synth. In Serum 2, "
                        "turn MPE on and set the same value there. Default 48.",
                        juce::dontSendNotification);
    addAndMakeVisible(pbRangeHelp);

    themeLabel.setJustificationType(juce::Justification::centredLeft);
    themeLabel.setColour(juce::Label::textColourId, Theme::text);
    addAndMakeVisible(themeLabel);

    themeBox.addItem("Light",    (int) Theme::Id::light    + 1);
    themeBox.addItem("Graphite", (int) Theme::Id::graphite + 1);
    themeBox.addItem("Dark",     (int) Theme::Id::dark     + 1);
    themeBox.setSelectedId(juce::jlimit(0, 2, processor.getThemeId()) + 1, juce::dontSendNotification);
    themeBox.onChange = [this]
    {
        applyTheme((Theme::Id) juce::jlimit(0, 2, themeBox.getSelectedId() - 1), true);
    };
    addAndMakeVisible(themeBox);

    // --- scale viewer ---
    scaleLabel.setJustificationType(juce::Justification::centredLeft);
    scaleLabel.setColour(juce::Label::textColourId, Theme::text);
    addAndMakeVisible(scaleLabel);

    for (int i = 0; i < 12; ++i)
        scaleRootBox.addItem(Scale::rootName(i), i + 1);
    scaleRootBox.setSelectedId(processor.getScaleRoot() + 1, juce::dontSendNotification);
    scaleRootBox.onChange = [this]
    {
        processor.setScaleRoot(scaleRootBox.getSelectedId() - 1);
        pianoRoll.repaint();
    };
    addAndMakeVisible(scaleRootBox);

    for (int i = 0; i < Scale::numTypes; ++i)
        scaleTypeBox.addItem(Scale::typeName(i), i + 1);
    scaleTypeBox.setSelectedId(juce::jlimit(0, (int) Scale::numTypes - 1, processor.getScaleType()) + 1,
                               juce::dontSendNotification);
    scaleTypeBox.onChange = [this]
    {
        processor.setScaleType(scaleTypeBox.getSelectedId() - 1);
        pianoRoll.repaint();
    };
    addAndMakeVisible(scaleTypeBox);

    snapToScaleButton.setButtonText("Snap");
    snapToScaleButton.setTooltip("Snap new and moved notes to the selected scale.");
    snapToScaleButton.setToggleState(processor.getSnapToScale(), juce::dontSendNotification);
    snapToScaleButton.setColour(juce::ToggleButton::textColourId, Theme::textDim);
    snapToScaleButton.onClick = [this] { processor.setSnapToScale(snapToScaleButton.getToggleState()); };
    addAndMakeVisible(snapToScaleButton);

    scaleRootBox.setTooltip("Scale root note.");
    scaleTypeBox.setTooltip("Highlight this scale in the piano roll (Chromatic = off).");

    // --- auto-updater ---
    updateLabel.setJustificationType(juce::Justification::centredLeft);
    updateLabel.setColour(juce::Label::textColourId, Theme::text);
    addAndMakeVisible(updateLabel);

    updateStatusLabel.setJustificationType(juce::Justification::centredLeft);
    updateStatusLabel.setFont(12.0f);
    updateStatusLabel.setColour(juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible(updateStatusLabel);

    checkUpdateButton.onClick = [this] { updateChecker.check(true); };
    addAndMakeVisible(checkUpdateButton);

    installUpdateButton.onClick = [this] { updateChecker.installStagedUpdate(); };
    addAndMakeVisible(installUpdateButton);

    updateChecker.onChanged = [this] { refreshUpdateUi(); };
    refreshUpdateUi();
    updateChecker.checkOnStartup();

    setupSlider(channelsSlider, channelsLabel, 1.0, 14.0, processor.getNumMemberChannels(), 1.0);
    channelsLabel.setJustificationType(juce::Justification::centredLeft);
    channelsSlider.setTooltip("How many MPE member channels to spread notes across "
                              "(more = more simultaneous independent bends).");
    channelsSlider.onValueChange = [this]
    {
        processor.setNumMemberChannels((int) channelsSlider.getValue());
    };

    // --- hosted synth controls ---
    addAndMakeVisible(synthBox);
    synthBox.setTextWhenNothingSelected("No synth");
    synthBox.setTooltip("Your saved synths - pick one to load it instantly. "
                        "Use \"Load new synth...\" to add one; each synth you load is offered "
                        "for the list. Rename / remove entries from the bottom of this menu.");
    synthBox.onChange = [this]
    {
        const int id = synthBox.getSelectedId();
        const auto& es = synthLibrary.entries();

        if (id >= 1 && id <= (int) es.size())
        {
            loadSynthEntry(es[(size_t) id - 1]);
            return;
        }

        const juce::File cur = processor.getHostedPlugin().isLoaded()
                                   ? processor.getHostedPlugin().getFile() : juce::File();
        const auto* active = synthLibrary.findByFile(cur);

        if (id == 100)
        {
            chooseHostedPlugin();
        }
        else if (id == 103 && cur != juce::File())
        {
            promptSynthName(cur, synthLibrary.suggestName(cur),
                            [this, cur](juce::String nm) { synthLibrary.add(nm, cur); refreshSynthBox(); });
        }
        else if (id == 101 && active != nullptr)
        {
            const juce::String old = active->name;
            promptSynthName(cur, old,
                            [this, old](juce::String nm) { synthLibrary.rename(old, nm); refreshSynthBox(); });
        }
        else if (id == 102 && active != nullptr)
        {
            synthLibrary.removeByName(active->name);
        }
        refreshSynthBox();
    };

    addAndMakeVisible(openHostedButton);
    openHostedButton.onClick = [this] { toggleHostedWindow(); };
    openHostedButton.setTooltip("Show or hide the hosted synth's own editor window.");

    addAndMakeVisible(freeRunButton);
    freeRunButton.setClickingTogglesState(true);
    freeRunButton.setToggleState(processor.getFreeRun(), juce::dontSendNotification);
    freeRunButton.onClick = [this] { processor.setFreeRun(freeRunButton.getToggleState()); };
    freeRunButton.setTooltip("OFF (default): the piano-roll loop only plays while the host sends "
                             "MPE Bender a note - so a disabled / empty channel stays silent, and "
                             "you gate the loop with a note in your DAW's pattern.\n"
                             "ON: the loop always plays with the transport.");

    addAndMakeVisible(forwardMidiButton);
    forwardMidiButton.setClickingTogglesState(true);
    forwardMidiButton.setToggleState(processor.getForwardHostMidi(), juce::dontSendNotification);
    forwardMidiButton.onClick = [this]
    {
        processor.setForwardHostMidi(forwardMidiButton.getToggleState());
    };
    forwardMidiButton.setTooltip("Free-run only: also pass MIDI from the host straight into the "
                                 "synth so you can play it live over the loop. In gate mode the "
                                 "incoming notes are silent triggers.");

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(13.0f);
    statusLabel.setColour(juce::Label::textColourId, Theme::text);
    addAndMakeVisible(statusLabel);

    addAndMakeVisible(fullscreenButton);
    fullscreenButton.onClick = [this] { toggleFullscreen(); };
    fullscreenButton.setTooltip("Grow the editor to fill the screen (toggle).");

    addAndMakeVisible(zoomResetButton);   // added last -> stays on top of the viewport

    showTab(Tab::roll);
    refreshSynthBox();
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
        "Choose a VST3 instrument to host (Serum 2, Vital, Pigments, ...)", start, "*.vst3");

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
            refreshHostedUi();
            return;
        }

        refreshHostedUi();

        // offer to bookmark it in the quick-switch list
        if (synthLibrary.findByFile(result) == nullptr)
            promptSynthName(result, synthLibrary.suggestName(result),
                            [this, result](juce::String nm) { synthLibrary.add(nm, result); refreshSynthBox(); });
    });
}

void MpePianoRollAudioProcessorEditor::loadSynthEntry(const SynthLibrary::Entry& e)
{
    if (! e.file.existsAsFile() && ! e.file.isDirectory())
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Synth not found",
            "\"" + e.name + "\" isn't there any more:\n" + e.file.getFullPathName());
        refreshSynthBox();
        return;
    }

    hostedWindow.reset();
    auto err = processor.loadHostedPlugin(e.file);
    if (err.isNotEmpty())
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Couldn't load \"" + e.name + "\"", err);

    refreshHostedUi();
}

void MpePianoRollAudioProcessorEditor::refreshSynthBox()
{
    synthBox.clear(juce::dontSendNotification);

    const auto& es = synthLibrary.entries();
    for (int i = 0; i < (int) es.size(); ++i)
        synthBox.addItem(es[(size_t) i].name, i + 1);

    const bool loaded = processor.getHostedPlugin().isLoaded();
    const juce::File cur = loaded ? processor.getHostedPlugin().getFile() : juce::File();
    const auto* active = synthLibrary.findByFile(cur);

    synthBox.addSeparator();
    synthBox.addItem("Load new synth...", 100);
    if (loaded && active == nullptr)
        synthBox.addItem("Save current synth to list...", 103);
    if (active != nullptr)
    {
        synthBox.addItem("Rename \"" + active->name + "\"...", 101);
        synthBox.addItem("Remove \"" + active->name + "\"", 102);
    }

    if (active != nullptr)
    {
        for (int i = 0; i < (int) es.size(); ++i)
            if (es[(size_t) i].name == active->name)
                synthBox.setSelectedId(i + 1, juce::dontSendNotification);
    }
    else
    {
        synthBox.setText(loaded ? processor.getHostedPlugin().getDisplayName() : juce::String(),
                         juce::dontSendNotification);
    }
}

void MpePianoRollAudioProcessorEditor::promptSynthName(juce::File file, juce::String initialName,
                                                       std::function<void(juce::String)> onAccept)
{
    juce::ignoreUnused(file);
    auto* aw = new juce::AlertWindow("Synth list",
                                     "Name this synth for the quick-switch menu:",
                                     juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", initialName, {}, false);
    aw->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> safe(aw);
    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe, onAccept = std::move(onAccept)](int result)
        {
            if (result == 1 && safe != nullptr)
            {
                auto nm = safe->getTextEditorContents("name").trim();
                if (nm.isNotEmpty())
                    onAccept(nm);
            }
        }), true);
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

    freeRunButton.setToggleState(processor.getFreeRun(), juce::dontSendNotification);
    forwardMidiButton.setToggleState(processor.getForwardHostMidi(), juce::dontSendNotification);
    forwardMidiButton.setEnabled(processor.getFreeRun());

    if (loaded != lastKnownLoaded)   // synth appeared / disappeared -> reflect it in the list
    {
        lastKnownLoaded = loaded;
        refreshSynthBox();
    }

    // Serum loads itself asynchronously - pop its window once it's actually up.
    if (autoOpenSynthWindow && loaded && hostedWindow == nullptr)
    {
        autoOpenSynthWindow = false;
        toggleHostedWindow();
    }
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

    const juce::String gate = processor.getFreeRun() ? "free run"
                            : processor.getUiGateOpen() ? "playing (note held)"
                                                        : "waiting for a note from the DAW";
    statusLabel.setText("notes out: " + juce::String(processor.getUiNoteOnCount())
                             + " / " + juce::String(processor.getUiNoteOffCount())
                             + "   |   " + gate
                             + "   |   " + statusText,
                         juce::dontSendNotification);

    refreshHostedUi();

    if (currentTab == Tab::settings)
        refreshUpdateUi();
}

void MpePianoRollAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panel);

    if (currentTab == Tab::roll)
    {
        // separator between the top bar and the piano roll
        g.setColour(Theme::separator);
        g.fillRect(0, rollViewport.getY() - 3, getWidth(), 2);
    }

    // "prodcoldie" credit, top-right of the tab bar
    g.setColour(Theme::textDim);
    g.setFont(11.0f);
    g.drawText("prodcoldie", getWidth() - 98, 4, 92, 18, juce::Justification::centredRight);
}

void MpePianoRollAudioProcessorEditor::refreshUpdateUi()
{
    updateStatusLabel.setText(updateChecker.getStatusText(), juce::dontSendNotification);

    const auto st = updateChecker.getState();
    const bool busy  = st == UpdateChecker::State::checking || st == UpdateChecker::State::downloading;
    const bool ready = st == UpdateChecker::State::readyToInstall;

    checkUpdateButton.setEnabled(updateChecker.isConfigured() && ! busy);
    installUpdateButton.setEnabled(ready);
    installUpdateButton.setVisible(ready || st == UpdateChecker::State::updateAvailable);
}

void MpePianoRollAudioProcessorEditor::applyTheme(Theme::Id id, bool store)
{
    Theme::apply(id);
    flatLnf.syncColours();
    if (store)
        processor.setThemeId((int) id);

    themeBox.setSelectedId((int) id + 1, juce::dontSendNotification);

    for (juce::Label* l : { &loopLabel, &channelsLabel, &pbRangeLabel, &pbRangeHelp, &updateStatusLabel })
        l->setColour(juce::Label::textColourId, Theme::textDim);
    for (juce::Label* l : { &statusLabel, &themeLabel, &scaleLabel, &updateLabel })
        l->setColour(juce::Label::textColourId, Theme::text);
    snapToScaleButton.setColour(juce::ToggleButton::textColourId, Theme::textDim);

    for (juce::Slider* s : { &loopLengthSlider, &pbRangeSlider })
    {
        s->setColour(juce::Slider::textBoxOutlineColourId, Theme::separator);
        s->setColour(juce::Slider::textBoxBackgroundColourId, Theme::field);
        s->setColour(juce::Slider::textBoxTextColourId, Theme::text);
    }

    sendLookAndFeelChange();   // pushes the new palette into every child component
    pianoRoll.repaint();
    keyboardSidebar.repaint();
    repaint();
}

void MpePianoRollAudioProcessorEditor::showTab(Tab t)
{
    currentTab = t;
    const bool roll = t == Tab::roll;

    juce::Component* rollBits[] = { &rollViewport, &keyboardSidebar,
                                   &loopLabel, &loopLengthSlider,
                                   &scaleLabel, &scaleRootBox, &scaleTypeBox, &snapToScaleButton,
                                   &synthBox, &openHostedButton, &freeRunButton, &forwardMidiButton,
                                   &statusLabel };
    for (auto* c : rollBits)
        c->setVisible(roll);

    juce::Component* settingsBits[] = { &pbRangeLabel, &pbRangeSlider, &pbRangeHelp,
                                       &channelsLabel, &channelsSlider,
                                       &themeLabel, &themeBox,
                                       &updateLabel, &updateStatusLabel, &checkUpdateButton, &installUpdateButton };
    for (auto* c : settingsBits)
        c->setVisible(! roll);
    if (! roll)
        refreshUpdateUi();   // may hide installUpdateButton again

    zoomResetButton.setVisible(true);    // detached, shown on both tabs
    fullscreenButton.setVisible(true);

    (roll ? rollTabButton : settingsTabButton).setToggleState(true, juce::dontSendNotification);
    resized();
}

void MpePianoRollAudioProcessorEditor::toggleFullscreen()
{
    if (! isFullscreen)
    {
        windowedW = getWidth();
        windowedH = getHeight();
        auto area = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
            area = d->userArea;
        setSize(juce::jlimit(720, 2400, area.getWidth()  - 8),
                juce::jlimit(360, 1600, area.getHeight() - 8));
        isFullscreen = true;
    }
    else
    {
        setSize(windowedW, windowedH);
        isFullscreen = false;
    }
    fullscreenButton.setButtonText(isFullscreen ? "Exit full" : "Fullscreen");
}

void MpePianoRollAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto tabBar = area.removeFromTop(24).reduced(6, 3);
    rollTabButton.setBounds(tabBar.removeFromLeft(56));
    tabBar.removeFromLeft(3);
    settingsTabButton.setBounds(tabBar.removeFromLeft(66));
    tabBar.removeFromRight(96);   // "prodcoldie" credit sits here
    fullscreenButton.setBounds(tabBar.removeFromRight(78));

    // "1:1" zoom reset - detached, pinned to the editor's bottom-right corner, both tabs
    zoomResetButton.setBounds(getWidth() - 4 - 38, getHeight() - 4 - 20, 38, 20);

    if (currentTab == Tab::settings)
    {
        auto s = area.reduced(18, 12);
        const int labelW = 180;
        auto row = [&s, labelW](juce::Label& label, juce::Component& ctl, int ctlW, int ctlH)
        {
            auto r = s.removeFromTop(juce::jmax(22, ctlH));
            label.setBounds(r.removeFromLeft(labelW));
            ctl.setBounds(r.removeFromLeft(ctlW).withSizeKeepingCentre(ctlW, ctlH));
            s.removeFromTop(9);
        };

        row(pbRangeLabel, pbRangeSlider, 96, 24);
        pbRangeHelp.setBounds(s.removeFromTop(36).withWidth(juce::jmin(440, s.getWidth())));
        s.removeFromTop(12);
        row(channelsLabel, channelsSlider, 190, 24);
        row(themeLabel, themeBox, 150, 26);
        s.removeFromTop(14);

        updateLabel.setBounds(s.removeFromTop(20));
        s.removeFromTop(2);
        updateStatusLabel.setBounds(s.removeFromTop(18).withWidth(juce::jmin(460, s.getWidth())));
        s.removeFromTop(8);
        {
            auto r = s.removeFromTop(26);
            checkUpdateButton.setBounds(r.removeFromLeft(96).withHeight(24));
            r.removeFromLeft(8);
            installUpdateButton.setBounds(r.removeFromLeft(120).withHeight(24));
        }
        return;
    }

    auto toolbar = area.removeFromTop(30).reduced(6, 3);
    loopLabel.setBounds(toolbar.removeFromLeft(74));
    loopLengthSlider.setBounds(toolbar.removeFromLeft(92));
    toolbar.removeFromLeft(16);
    scaleLabel.setBounds(toolbar.removeFromLeft(40));
    scaleRootBox.setBounds(toolbar.removeFromLeft(52).withSizeKeepingCentre(52, 24));
    toolbar.removeFromLeft(4);
    scaleTypeBox.setBounds(toolbar.removeFromLeft(150).withSizeKeepingCentre(150, 24));
    toolbar.removeFromLeft(8);
    snapToScaleButton.setBounds(toolbar.removeFromLeft(72));

    auto synthBar = area.removeFromTop(28).reduced(6, 2);
    synthBox.setBounds(synthBar.removeFromLeft(196).withSizeKeepingCentre(196, 24));
    synthBar.removeFromLeft(8);
    openHostedButton.setBounds(synthBar.removeFromLeft(96));
    synthBar.removeFromLeft(6);
    freeRunButton.setBounds(synthBar.removeFromLeft(84));
    synthBar.removeFromLeft(6);
    forwardMidiButton.setBounds(synthBar.removeFromLeft(104));

    statusLabel.setBounds(area.removeFromTop(18).reduced(6, 1));
    area.removeFromTop(4);   // room for the separator line

    rollViewport.setBounds(area);
    pianoRoll.updateContentSize();

    const int sbThick = rollViewport.getScrollBarThickness();
    keyboardSidebar.setBounds(area.getX(), area.getY(),
                              PianoRollComponent::keyboardWidth, area.getHeight() - sbThick);

    zoomResetButton.toFront(false);   // keep it above the viewport after every relayout
}
