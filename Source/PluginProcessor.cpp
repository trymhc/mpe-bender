#include "PluginProcessor.h"
#include "PluginEditor.h"

MpePianoRollAudioProcessor::MpePianoRollAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // The standalone has no host feeding it notes, so default it to free-run;
    // inside a DAW default to gated (silent until the channel sends a note).
    freeRun.store(wrapperType == wrapperType_Standalone, std::memory_order_relaxed);

    // Try to bring up a synth on its own (the last one you used, else Serum 2), so a
    // fresh instance is ready to play without clicking "Load ...". Deferred to the
    // message thread (can't create a VST3 instance here); skipped if the host
    // restores a saved synth first.
    triggerAsyncUpdate();
}

juce::File MpePianoRollAudioProcessor::rememberedSynthFile()
{
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                 .getChildFile("MPE Bender").getChildFile("last_synth.txt");
    if (f.existsAsFile())
    {
        juce::File synth(f.loadFileAsString().trim());
        if (synth.exists())
            return synth;
    }
    return {};
}

void MpePianoRollAudioProcessor::rememberSynthFile(const juce::File& synth)
{
    if (synth == juce::File())
        return;
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("MPE Bender");
    dir.createDirectory();
    dir.getChildFile("last_synth.txt").replaceWithText(synth.getFullPathName());
}

bool MpePianoRollAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MpePianoRollAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;
    wasPlaying = false;
    zoneConfigSent = false;

    hostedPlugin.prepare(sampleRate, samplesPerBlock);
}

void MpePianoRollAudioProcessor::releaseResources()
{
    hostedPlugin.releaseResources();
}

// ---------------------------------------------------------------------------
//  Hosted synth (Serum 2)
// ---------------------------------------------------------------------------

juce::File MpePianoRollAudioProcessor::findLikelySerumFile()
{
    juce::Array<juce::File> roots;
    roots.add(juce::File("C:/Program Files/Common Files/VST3"));
    roots.add(juce::File("C:/Program Files/VstPlugins"));
    roots.add(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
                  .getChildFile("Common Files").getChildFile("VST3"));

    auto scan = [](const juce::File& root, bool requireTwo) -> juce::File
    {
        if (! root.isDirectory())
            return {};

        for (auto& f : root.findChildFiles(juce::File::findFilesAndDirectories, true, "*.vst3"))
        {
            auto name = f.getFileNameWithoutExtension().toLowerCase();
            if (! name.contains("serum"))
                continue;
            if (! requireTwo || name.contains("2") || name.contains(" ii"))
                return f;
        }
        return {};
    };

    for (auto& root : roots)
        if (auto f = scan(root, true); f != juce::File())
            return f;

    for (auto& root : roots)
        if (auto f = scan(root, false); f != juce::File())
            return f;

    return {};
}

juce::String MpePianoRollAudioProcessor::loadHostedPlugin(const juce::File& vst3File)
{
    pendingAutoLoad = false;
    auto err = hostedPlugin.load(vst3File, currentSampleRate, currentBlockSize);
    if (err.isEmpty())
    {
        hostedPlugin.setPlayHead(getPlayHead());
        zoneConfigSent = false;   // (re)announce MPE to the freshly loaded synth
        rememberSynthFile(vst3File);
    }
    return err;
}

void MpePianoRollAudioProcessor::unloadHostedPlugin()
{
    hostedPlugin.unload();
}

void MpePianoRollAudioProcessor::handleAsyncUpdate()
{
    juce::File file;
    juce::MemoryBlock hostedState;
    bool havePending = false;

    {
        const juce::ScopedLock sl(pendingLock);
        havePending = hasPendingHostedLoad;
        if (havePending)
        {
            file = pendingHostedFile;
            hostedState = pendingHostedState;
            hasPendingHostedLoad = false;
        }
    }

    // The host is restoring a saved synth (with its patch): honour it.
    if (havePending && file != juce::File())
    {
        pendingAutoLoad = false;
        auto err = hostedPlugin.loadWithState(file, currentSampleRate, currentBlockSize, hostedState);
        juce::ignoreUnused(err);
        hostedPlugin.setPlayHead(getPlayHead());
        zoneConfigSent = false;
        return;
    }

    // Fresh instance, nothing to restore: auto-load a synth once - the last one you
    // used, else Serum 2 if it can be found. Any VST3 instrument works.
    if (pendingAutoLoad && ! hostedPlugin.isLoaded())
    {
        pendingAutoLoad = false;
        auto synth = rememberedSynthFile();
        if (synth == juce::File())
            synth = findLikelySerumFile();
        if (synth != juce::File())
        {
            auto e = hostedPlugin.load(synth, currentSampleRate, currentBlockSize);
            if (e.isEmpty())
            {
                hostedPlugin.setPlayHead(getPlayHead());
                zoneConfigSent = false;
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  Config setters
// ---------------------------------------------------------------------------

void MpePianoRollAudioProcessor::setLoopLengthBeats(double beats)
{
    loopLengthBeats = juce::jmax(0.25, beats);
}

void MpePianoRollAudioProcessor::setPitchBendRangeSemitones(int semitones)
{
    engine.setPitchBendRangeSemitones(semitones);
    zoneConfigSent = false;
}

void MpePianoRollAudioProcessor::setNumMemberChannels(int numChannels)
{
    engine.setNumMemberChannels(numChannels);
    zoneConfigSent = false;
}

// ---------------------------------------------------------------------------
//  Undo / redo
// ---------------------------------------------------------------------------

void MpePianoRollAudioProcessor::commitUndo(std::vector<MpeNote> before)
{
    undoStack.push_back(std::move(before));
    if (undoStack.size() > maxUndo)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

bool MpePianoRollAudioProcessor::undo()
{
    if (undoStack.empty())
        return false;

    auto restore = std::move(undoStack.back());
    undoStack.pop_back();

    {
        juce::ScopedLock sl(notesLock);
        redoStack.push_back(notes);
        for (auto& n : restore) { n.isSounding = false; n.assignedChannel = -1; }
        notes = std::move(restore);
    }
    pendingHardReset.store(true, std::memory_order_relaxed);
    return true;
}

bool MpePianoRollAudioProcessor::redo()
{
    if (redoStack.empty())
        return false;

    auto restore = std::move(redoStack.back());
    redoStack.pop_back();

    {
        juce::ScopedLock sl(notesLock);
        undoStack.push_back(notes);
        for (auto& n : restore) { n.isSounding = false; n.assignedChannel = -1; }
        notes = std::move(restore);
    }
    pendingHardReset.store(true, std::memory_order_relaxed);
    return true;
}

void MpePianoRollAudioProcessor::hardResetPlayback(juce::MidiBuffer& midiBuffer)
{
    juce::ScopedLock sl(notesLock);
    engine.allNotesOff(midiBuffer);
    for (auto& n : notes)
    {
        n.isSounding = false;
        n.assignedChannel = -1;
    }
}

// ---------------------------------------------------------------------------
//  Audio
// ---------------------------------------------------------------------------

void MpePianoRollAudioProcessor::processBlock(juce::AudioBuffer<float>& audioBuffer,
                                              juce::MidiBuffer& midiBuffer)
{
    juce::ScopedNoDenormals noDenormals;
    uiProcessBlockCount.fetch_add(1, std::memory_order_relaxed);

    const int numSamples = audioBuffer.getNumSamples();

    juce::MidiBuffer forSynth;

    if (pendingHardReset.exchange(false, std::memory_order_relaxed))
        hardResetPlayback(forSynth);   // undo/redo swapped the note list out from under the engine

    // ---- gate: unless in free-run, the loop only plays while the host feeds us notes ----
    for (const auto meta : midiBuffer)
    {
        const auto msg = meta.getMessage();
        if      (msg.isNoteOn())                              ++heldHostNotes;
        else if (msg.isNoteOff())                             heldHostNotes = juce::jmax(0, heldHostNotes - 1);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())  heldHostNotes = 0;
    }
    const bool free     = freeRun.load(std::memory_order_relaxed);
    const bool gateOpen = free || heldHostNotes > 0;

    // In free-run the host's own MIDI can be layered onto the synth; in gate mode the
    // incoming notes are pure triggers and never sound directly.
    if (free && forwardHostMidi.load(std::memory_order_relaxed))
        forSynth.addEvents(midiBuffer, 0, numSamples, 0);

    uiGateOpen.store(gateOpen, std::memory_order_relaxed);
    if (gateWasOpen && ! gateOpen)
        hardResetPlayback(forSynth);   // gate just closed - release anything still sounding
    gateWasOpen = gateOpen;

    // ---- generate MPE MIDI from the piano roll, driven by the host transport ----
    auto* transport = getPlayHead();

    auto bail = [&](TransportStatus status, bool playing)
    {
        uiTransportStatus.store(status, std::memory_order_relaxed);
        uiIsPlaying.store(playing, std::memory_order_relaxed);
    };

    if (transport == nullptr)
    {
        bail(TransportStatus::noPlayHeadObject, false);
    }
    else if (auto position = transport->getPosition(); ! position.hasValue())
    {
        bail(TransportStatus::noPositionInfo, false);
    }
    else
    {
        const bool isPlaying = position->getIsPlaying();
        const auto bpmOpt = position->getBpm();
        const auto ppqOpt = position->getPpqPosition();
        uiIsPlaying.store(isPlaying, std::memory_order_relaxed);

        if (! isPlaying)
        {
            uiTransportStatus.store(TransportStatus::stopped, std::memory_order_relaxed);
            if (wasPlaying)
                hardResetPlayback(forSynth);   // release anything still sounding
            wasPlaying = false;
            heldHostNotes = 0;   // forget stuck triggers when the transport stops
        }
        else
        {
            // (Re)announce MPE: on the first playing block, and again a few times
            // over the next ~1.5 s so a late-loading synth (e.g. Serum) still
            // picks up its zone + per-channel pitch-bend range.
            if (! zoneConfigSent)
            {
                configResends = 6;
                configResendCountdown = 0;
                zoneConfigSent = true;
            }
            if (configResends > 0 && --configResendCountdown <= 0)
            {
                engine.sendZoneConfiguration(forSynth, 0);
                --configResends;
                configResendCountdown = juce::jmax(1, (int) (currentSampleRate * 0.25 / juce::jmax(1, numSamples)));
            }
            wasPlaying = true;

            if (! bpmOpt.hasValue() || ! ppqOpt.hasValue())
            {
                uiTransportStatus.store(TransportStatus::missingTempoOrPpq, std::memory_order_relaxed);
            }
            else
            {
                const double bpm = *bpmOpt;
                const double ppq = *ppqOpt;

                if (bpm > 0.0 && numSamples > 0)
                {
                    uiTransportStatus.store(TransportStatus::playing, std::memory_order_relaxed);
                    uiBpm.store(bpm, std::memory_order_relaxed);

                    const double samplesPerBeat = (60.0 / bpm) * currentSampleRate;
                    if (samplesPerBeat > 0.0 && gateOpen)
                    {
                        const double blockStartBeat = ppq;
                        const double blockEndBeat = blockStartBeat + (double) numSamples / samplesPerBeat;

                        const double loopLen = loopLengthBeats;
                        double localStart = std::fmod(blockStartBeat, loopLen);
                        if (localStart < 0.0)
                            localStart += loopLen;
                        const double localEnd = localStart + (blockEndBeat - blockStartBeat);

                        uiPlayheadBeat.store(localStart, std::memory_order_relaxed);

                        juce::ScopedLock sl(notesLock);
                        if (localEnd <= loopLen)
                        {
                            engine.renderBlock(notes, forSynth, localStart, localEnd, samplesPerBeat);
                        }
                        else
                        {
                            engine.renderBlock(notes, forSynth, localStart, loopLen, samplesPerBeat);

                            const int seg1Samples = juce::roundToInt((loopLen - localStart) * samplesPerBeat);
                            const double wrappedEnd = localEnd - loopLen;

                            juce::MidiBuffer scratch;
                            engine.renderBlock(notes, scratch, 0.0, wrappedEnd, samplesPerBeat);
                            forSynth.addEvents(scratch, 0, -1, seg1Samples);
                        }
                    }
                }
            }
        }
    }

    // ---- run the hosted synth; its audio becomes our output ----
    hostedPlugin.process(audioBuffer, forSynth, getPlayHead());

    // We are an instrument, not a MIDI producer - don't leak MIDI back to the host.
    midiBuffer.clear();
}

juce::AudioProcessorEditor* MpePianoRollAudioProcessor::createEditor()
{
    return new MpePianoRollAudioProcessorEditor(*this);
}

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------

void MpePianoRollAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("MpePianoRollState");
    state.setProperty("loopLengthBeats", loopLengthBeats, nullptr);
    state.setProperty("pitchBendRange", engine.getPitchBendRangeSemitones(), nullptr);
    state.setProperty("numMemberChannels", engine.getNumMemberChannels(), nullptr);
    state.setProperty("forwardHostMidi", forwardHostMidi.load(std::memory_order_relaxed), nullptr);
    state.setProperty("freeRun", freeRun.load(std::memory_order_relaxed), nullptr);
    state.setProperty("themeId", themeId, nullptr);
    state.setProperty("scaleRoot", scaleRoot, nullptr);
    state.setProperty("scaleType", scaleType, nullptr);
    state.setProperty("snapToScale", snapToScale, nullptr);

    if (hostedPlugin.isLoaded())
    {
        state.setProperty("hostedFile", hostedPlugin.getFile().getFullPathName(), nullptr);

        juce::MemoryBlock hostedBlock;
        hostedPlugin.getHostedState(hostedBlock);
        state.setProperty("hostedState", hostedBlock.toBase64Encoding(), nullptr);
    }

    juce::ValueTree notesTree("Notes");
    {
        juce::ScopedLock sl(notesLock);
        for (auto& n : notes)
        {
            juce::ValueTree nt("Note");
            nt.setProperty("id", n.id.toString(), nullptr);
            nt.setProperty("startBeat", n.startBeat, nullptr);
            nt.setProperty("lengthBeats", n.lengthBeats, nullptr);
            nt.setProperty("pitch", n.pitch, nullptr);
            nt.setProperty("velocity", n.velocity, nullptr);
            nt.setProperty("releaseVelocity", n.releaseVelocity, nullptr);
            if (n.muted) nt.setProperty("muted", 1, nullptr);
            nt.setProperty("shape", (int) n.shape, nullptr);
            nt.setProperty("shapeCycles", n.shapeCycles, nullptr);
            nt.setProperty("shapeSkew", n.shapeSkew, nullptr);
            nt.setProperty("shapeAmpStart", n.shapeAmpStart, nullptr);
            nt.setProperty("shapeAmpEnd", n.shapeAmpEnd, nullptr);
            if (n.hasShapeRange())
            {
                nt.setProperty("shapeFromBeat", n.shapeFromBeat, nullptr);
                nt.setProperty("shapeToBeat", n.shapeToBeat, nullptr);
            }

            juce::ValueTree ct("Bend");
            for (auto& p : n.bend.getPoints())
            {
                juce::ValueTree pt("Pt");
                pt.setProperty("beat", p.beat, nullptr);
                pt.setProperty("value", p.value, nullptr);
                if (p.shaper)
                    pt.setProperty("shaper", 1, nullptr);
                ct.appendChild(pt, nullptr);
            }
            nt.appendChild(ct, nullptr);

            notesTree.appendChild(nt, nullptr);
        }
    }
    state.appendChild(notesTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void MpePianoRollAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    if (! state.isValid())
        return;

    loopLengthBeats = state.getProperty("loopLengthBeats", 32.0);
    engine.setPitchBendRangeSemitones((int) state.getProperty("pitchBendRange", 48));
    engine.setNumMemberChannels((int) state.getProperty("numMemberChannels", 15));
    forwardHostMidi.store((bool) state.getProperty("forwardHostMidi", true), std::memory_order_relaxed);
    freeRun.store((bool) state.getProperty("freeRun", false), std::memory_order_relaxed);
    themeId = (int) state.getProperty("themeId", 0);
    scaleRoot = ((((int) state.getProperty("scaleRoot", 0)) % 12) + 12) % 12;
    scaleType = (int) state.getProperty("scaleType", 0);
    snapToScale = (bool) state.getProperty("snapToScale", false);
    zoneConfigSent = false;
    undoStack.clear();
    redoStack.clear();

    std::vector<MpeNote> loaded;
    auto notesTree = state.getChildWithName("Notes");
    for (int i = 0; i < notesTree.getNumChildren(); ++i)
    {
        auto nt = notesTree.getChild(i);
        MpeNote n;
        n.id = juce::Uuid(nt.getProperty("id").toString());
        n.startBeat = nt.getProperty("startBeat", 0.0);
        n.lengthBeats = nt.getProperty("lengthBeats", 1.0);
        n.pitch = nt.getProperty("pitch", 60);
        n.velocity = (float) (double) nt.getProperty("velocity", 0.8);
        n.releaseVelocity = (float) (double) nt.getProperty("releaseVelocity", 0.5);
        n.muted = (bool) nt.getProperty("muted", false);
        n.shape = (BendShape) (int) nt.getProperty("shape", 0);
        n.shapeCycles = (float) (double) nt.getProperty("shapeCycles", 4.0);
        if (auto p = nt.getProperty("shapeCyclePeriod", juce::var()); ! p.isVoid())   // 0.8-only field -> count
            n.shapeCycles = (float) juce::jmax(1.0, n.lengthBeats / juce::jmax(0.03125, (double) p));
        n.shapeCycles = std::round(n.shapeCycles * 2.0f) / 2.0f;
        n.shapeSkew     = (float) (double) nt.getProperty("shapeSkew", 1.0);
        n.shapeAmpStart = (float) (double) nt.getProperty("shapeAmpStart", 0.0);
        n.shapeAmpEnd   = (float) (double) nt.getProperty("shapeAmpEnd", 2.0);
        n.shapeFromBeat = (double) nt.getProperty("shapeFromBeat", -1.0);
        n.shapeToBeat   = (double) nt.getProperty("shapeToBeat", -1.0);

        auto deserialiseCurve = [&](ExpressionCurve& curve, const char* tagName)
        {
            auto ct = nt.getChildWithName(tagName);
            if (! ct.isValid())
                return;
            curve.clearAndReset();
            for (int p = 0; p < ct.getNumChildren(); ++p)
            {
                auto pt = ct.getChild(p);
                curve.setPoint((double) pt.getProperty("beat", 0.0),
                               (float) (double) pt.getProperty("value", 0.0),
                               (bool) pt.getProperty("shaper", false));
            }
        };

        deserialiseCurve(n.bend, "Bend");
        deserialiseCurve(n.bend, "PitchBend");   // accept the pre-0.3 tag name too
        n.lengthBeats = n.bend.conformEnd(n.lengthBeats);

        loaded.push_back(n);
    }

    {
        juce::ScopedLock sl(notesLock);
        notes = std::move(loaded);
    }

    // Defer the hosted-plugin load to the message thread - creating a VST3 instance
    // must not happen on whatever thread the host calls setStateInformation from.
    juce::String hostedPath = state.getProperty("hostedFile", juce::String());
    if (hostedPath.isNotEmpty())
    {
        const juce::ScopedLock sl(pendingLock);
        pendingHostedFile = juce::File(hostedPath);
        pendingHostedState.reset();
        pendingHostedState.fromBase64Encoding(state.getProperty("hostedState", juce::String()).toString());
        hasPendingHostedLoad = true;
        triggerAsyncUpdate();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MpePianoRollAudioProcessor();
}
