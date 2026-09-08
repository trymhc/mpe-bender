#include "MpeEngine.h"

namespace
{
    constexpr float pitchBendChangeThreshold = 0.02f;   // semitones
}

MpeEngine::MpeEngine()
{
    for (auto& c : channels)
        c = ChannelState();
}

void MpeEngine::setNumMemberChannels(int numChannels)
{
    numMemberChannels = juce::jlimit(1, 14, numChannels);
}

void MpeEngine::setPitchBendRangeSemitones(int semitones)
{
    pitchBendRangeSemitones = juce::jlimit(1, 96, semitones);
}

int MpeEngine::pitchBendTo14Bit(float semitoneOffset, int rangeSemitones)
{
    auto normalised = juce::jlimit(-1.0f, 1.0f, semitoneOffset / (float) rangeSemitones);
    int value = 8192 + juce::roundToInt(normalised * 8191.0f);
    return juce::jlimit(0, 16383, value);
}

void MpeEngine::sendZoneConfiguration(juce::MidiBuffer& buffer, int sampleOffset)
{
    // MPE Configuration Message (MCM) on the master channel: declares this a Lower Zone
    // with `numMemberChannels` member channels.
    buffer.addEvent(juce::MidiMessage::controllerEvent(masterChannel, 101, 0), sampleOffset);   // RPN MSB
    buffer.addEvent(juce::MidiMessage::controllerEvent(masterChannel, 100, 6), sampleOffset);   // RPN LSB = 6 (MCM)
    buffer.addEvent(juce::MidiMessage::controllerEvent(masterChannel, 6, numMemberChannels), sampleOffset);
    buffer.addEvent(juce::MidiMessage::controllerEvent(masterChannel, 38, 0), sampleOffset);
    buffer.addEvent(juce::MidiMessage::controllerEvent(masterChannel, 101, 127), sampleOffset); // null RPN
    buffer.addEvent(juce::MidiMessage::controllerEvent(masterChannel, 100, 127), sampleOffset);

    // Each member channel needs its own Pitch Bend Sensitivity RPN (0,0) set.
    for (int ch = 2; ch <= 1 + numMemberChannels; ++ch)
    {
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 101, 0), sampleOffset);
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 100, 0), sampleOffset);
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 6, pitchBendRangeSemitones), sampleOffset);
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 38, 0), sampleOffset);
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 101, 127), sampleOffset);
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 100, 127), sampleOffset);
    }
}

void MpeEngine::allNotesOff(juce::MidiBuffer& buffer)
{
    for (int ch = 2; ch <= 1 + numMemberChannels; ++ch)
    {
        if (channels[ch].inUse)
        {
            buffer.addEvent(juce::MidiMessage::noteOff(ch, juce::jlimit(0, 127, channels[ch].notePitch)), 0);
            freeChannel(ch);
        }
    }
}

int MpeEngine::allocateChannel(std::vector<MpeNote>& notes, const juce::Uuid& noteId,
                                juce::MidiBuffer& buffer, int sampleOffset)
{
    for (int ch = 2; ch <= 1 + numMemberChannels; ++ch)
    {
        if (!channels[ch].inUse)
        {
            channels[ch] = ChannelState();
            channels[ch].inUse = true;
            channels[ch].noteId = noteId;
            channels[ch].allocatedOrder = ++allocationCounter;
            return ch;
        }
    }

    // No free channel: steal the oldest one (basic voice stealing).
    int oldest = -1;
    juce::uint32 oldestOrder = 0xFFFFFFFF;
    for (int ch = 2; ch <= 1 + numMemberChannels; ++ch)
    {
        if (channels[ch].allocatedOrder < oldestOrder)
        {
            oldestOrder = channels[ch].allocatedOrder;
            oldest = ch;
        }
    }

    if (oldest > 0)
    {
        for (auto& n : notes)
        {
            if (n.assignedChannel == oldest && n.isSounding)
            {
                buffer.addEvent(juce::MidiMessage::noteOff(oldest, juce::jlimit(0, 127, n.pitch),
                                                             n.releaseVelocity), sampleOffset);
                noteOffCount.fetch_add(1, std::memory_order_relaxed);
                n.isSounding = false;
                n.assignedChannel = -1;
                break;
            }
        }

        channels[oldest] = ChannelState();
        channels[oldest].inUse = true;
        channels[oldest].noteId = noteId;
        channels[oldest].allocatedOrder = ++allocationCounter;
    }

    return oldest;
}

void MpeEngine::freeChannel(int channelIndex)
{
    if (channelIndex >= 2 && channelIndex < (int) channels.size())
        channels[channelIndex] = ChannelState();
}

void MpeEngine::triggerNoteOn(MpeNote& note, std::vector<MpeNote>& notes, juce::MidiBuffer& buffer,
                               double blockStartBeat, double samplesPerBeat)
{
    int sampleOffset = juce::roundToInt((note.startBeat - blockStartBeat) * samplesPerBeat);
    sampleOffset = juce::jmax(0, sampleOffset);

    int ch = allocateChannel(notes, note.id, buffer, sampleOffset);
    if (ch < 0)
        return; // shouldn't happen with stealing enabled, but guard anyway

    note.assignedChannel = ch;
    note.isSounding = true;
    channels[ch].notePitch = note.pitch;

    auto pb = note.bendOffsetAt(0.0);

    buffer.addEvent(juce::MidiMessage::pitchWheel(ch, pitchBendTo14Bit(pb, pitchBendRangeSemitones)), sampleOffset);
    buffer.addEvent(juce::MidiMessage::noteOn(ch, juce::jlimit(0, 127, note.pitch), note.velocity), sampleOffset);
    noteOnCount.fetch_add(1, std::memory_order_relaxed);

    channels[ch].lastPitchBendSemitones = pb;
}

void MpeEngine::triggerNoteOff(MpeNote& note, juce::MidiBuffer& buffer,
                                double blockStartBeat, double samplesPerBeat)
{
    if (!note.isSounding || note.assignedChannel < 0)
        return;

    int sampleOffset = juce::roundToInt((note.endBeat() - blockStartBeat) * samplesPerBeat);
    sampleOffset = juce::jmax(0, sampleOffset);

    buffer.addEvent(juce::MidiMessage::noteOff(note.assignedChannel, juce::jlimit(0, 127, note.pitch),
                                                 note.releaseVelocity), sampleOffset);
    noteOffCount.fetch_add(1, std::memory_order_relaxed);

    freeChannel(note.assignedChannel);
    note.isSounding = false;
    note.assignedChannel = -1;
}

void MpeEngine::updateExpression(MpeNote& note, juce::MidiBuffer& buffer, double blockStartBeat)
{
    if (!note.isSounding || note.assignedChannel < 0)
        return;

    int ch = note.assignedChannel;
    double relBeat = blockStartBeat - note.startBeat;
    int sampleOffset = 0; // block-start-relative update; good enough at control rate

    auto pb = note.bendOffsetAt(relBeat);

    if (std::abs(pb - channels[ch].lastPitchBendSemitones) > pitchBendChangeThreshold)
    {
        buffer.addEvent(juce::MidiMessage::pitchWheel(ch, pitchBendTo14Bit(pb, pitchBendRangeSemitones)), sampleOffset);
        channels[ch].lastPitchBendSemitones = pb;
    }
}

void MpeEngine::renderBlock(std::vector<MpeNote>& notes,
                             juce::MidiBuffer& buffer,
                             double blockStartBeat,
                             double blockEndBeat,
                             double samplesPerBeat)
{
    for (auto& note : notes)
    {
        if (!note.isSounding)
        {
            if (note.startBeat >= blockStartBeat && note.startBeat < blockEndBeat)
                triggerNoteOn(note, notes, buffer, blockStartBeat, samplesPerBeat);
        }
        else if (note.startBeat < blockStartBeat)
        {
            updateExpression(note, buffer, blockStartBeat);
        }

        if (note.isSounding && note.endBeat() >= blockStartBeat && note.endBeat() < blockEndBeat)
            triggerNoteOff(note, buffer, blockStartBeat, samplesPerBeat);
    }
}
