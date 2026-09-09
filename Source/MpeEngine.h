#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "NoteModel.h"
#include <array>
#include <vector>

// Generates MPE-compliant MIDI (note on/off + per-note pitch bend) from a list of
// notes with per-note bend curves, driven by the host's playhead position. Owns the
// MPE member-channel pool for a single zone.
class MpeEngine
{
public:
    MpeEngine();

    // Lower Zone: master channel 1, member channels 2..(1+numMemberChannels).
    void setNumMemberChannels(int numChannels);
    void setPitchBendRangeSemitones(int semitones);

    int getNumMemberChannels() const { return numMemberChannels; }
    static constexpr int getMaxMemberChannels() { return maxMemberChannels; }
    int getPitchBendRangeSemitones() const { return pitchBendRangeSemitones; }

    // Call once when playback starts (or the config changes) to (re)send the MPE
    // Configuration Message and per-channel pitch bend range RPNs.
    void sendZoneConfiguration(juce::MidiBuffer& buffer, int sampleOffset = 0);

    // Call when transport stops / plugin is reset: force note-offs for anything sounding.
    void allNotesOff(juce::MidiBuffer& buffer);

    // Diagnostics: total MIDI note-on/off messages actually written to a buffer,
    // so the editor can show whether the engine is really emitting MIDI.
    juce::uint64 getNoteOnCount() const { return noteOnCount.load(std::memory_order_relaxed); }
    juce::uint64 getNoteOffCount() const { return noteOffCount.load(std::memory_order_relaxed); }

    // Render MIDI for the half-open beat range [blockStartBeat, blockEndBeat) into buffer.
    // notes must be sorted by nothing in particular; all notes are scanned each block.
    void renderBlock(std::vector<MpeNote>& notes,
                      juce::MidiBuffer& buffer,
                      double blockStartBeat,
                      double blockEndBeat,
                      double samplesPerBeat);

private:
    struct ChannelState
    {
        bool inUse = false;
        juce::Uuid noteId;
        int notePitch = -1;
        float lastPitchBendSemitones = 0.0f;
        juce::uint32 allocatedOrder = 0;
    };

    void triggerNoteOn(MpeNote& note, std::vector<MpeNote>& notes, juce::MidiBuffer& buffer,
                        double blockStartBeat, double samplesPerBeat);
    void triggerNoteOff(MpeNote& note, juce::MidiBuffer& buffer,
                         double blockStartBeat, double samplesPerBeat);
    void updateExpression(MpeNote& note, juce::MidiBuffer& buffer, double blockStartBeat);

    int allocateChannel(std::vector<MpeNote>& notes, const juce::Uuid& noteId,
                         juce::MidiBuffer& buffer, int sampleOffset);
    void freeChannel(int channelIndex);
    static int pitchBendTo14Bit(float semitoneOffset, int rangeSemitones);

    static constexpr int masterChannel = 1;
    static constexpr int maxMemberChannels = 15;   // lower-zone MPE ceiling: channels 2..16
    int numMemberChannels = maxMemberChannels;
    int pitchBendRangeSemitones = 48;           // common MPE default; matches many synths incl. Serum 2

    std::array<ChannelState, 17> channels;      // index 0 unused; 1 = master; 2..16 = members
    juce::uint32 allocationCounter = 0;

    std::atomic<juce::uint64> noteOnCount { 0 };
    std::atomic<juce::uint64> noteOffCount { 0 };
};
