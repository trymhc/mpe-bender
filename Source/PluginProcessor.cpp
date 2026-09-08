#include "PluginProcessor.h"
#include "PluginEditor.h"

MpePianoRollAudioProcessor::MpePianoRollAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // A couple of demo notes so the roll isn't empty on first load: the first one
    // bends up two semitones over its length, the second sits flat.
    MpeNote a;
    a.startBeat = 0.0;
    a.lengthBeats = 2.0;
    a.pitch = 60;
    a.bend.addPoint(1.0, 2.0f);
    a.bend.addPoint(2.0, 2.0f);
    a.bend.setTension(0, -0.5f);   // curved scoop up into the first point
    notes.push_back(a);

    MpeNote b;
    b.startBeat = 2.0;
    b.lengthBeats = 2.0;
    b.pitch = 64;
    notes.push_back(b);
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
    auto err = hostedPlugin.load(vst3File, currentSampleRate, currentBlockSize);
    if (err.isEmpty())
        hostedPlugin.setPlayHead(getPlayHead());
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

    {
        const juce::ScopedLock sl(pendingLock);
        if (! hasPendingHostedLoad)
            return;
        file = pendingHostedFile;
        hostedState = pendingHostedState;
        hasPendingHostedLoad = false;
    }

    if (file == juce::File())
        return;

    auto err = hostedPlugin.loadWithState(file, currentSampleRate, currentBlockSize, hostedState);
    juce::ignoreUnused(err);
    hostedPlugin.setPlayHead(getPlayHead());
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

    // MIDI stream we hand to the hosted synth: optionally the host's own MIDI, plus
    // the MPE stream our engine generates from the piano roll.
    juce::MidiBuffer forSynth;
    if (forwardHostMidi.load(std::memory_order_relaxed))
        forSynth.addEvents(midiBuffer, 0, numSamples, 0);

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
        }
        else
        {
            if (! zoneConfigSent)
            {
                engine.sendZoneConfiguration(forSynth, 0);
                zoneConfigSent = true;
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
                    if (samplesPerBeat > 0.0)
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

            auto serialiseCurve = [](const ExpressionCurve& c, const char* tagName)
            {
                juce::ValueTree ct(tagName);
                for (auto& p : c.getPoints())
                {
                    juce::ValueTree pt("Pt");
                    pt.setProperty("beat", p.beat, nullptr);
                    pt.setProperty("value", p.value, nullptr);
                    if (p.tension != 0.0f)
                        pt.setProperty("tension", p.tension, nullptr);
                    ct.appendChild(pt, nullptr);
                }
                return ct;
            };

            nt.appendChild(serialiseCurve(n.bend, "Bend"), nullptr);

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

    loopLengthBeats = state.getProperty("loopLengthBeats", 8.0);
    engine.setPitchBendRangeSemitones((int) state.getProperty("pitchBendRange", 48));
    engine.setNumMemberChannels((int) state.getProperty("numMemberChannels", 14));
    forwardHostMidi.store((bool) state.getProperty("forwardHostMidi", true), std::memory_order_relaxed);
    zoneConfigSent = false;

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
                               (float) (double) pt.getProperty("tension", 0.0));
            }
        };

        deserialiseCurve(n.bend, "Bend");
        deserialiseCurve(n.bend, "PitchBend");   // accept the pre-0.3 tag name too

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
