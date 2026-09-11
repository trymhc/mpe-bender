#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "NoteModel.h"
#include <vector>
#include <algorithm>

// Import a standard .mid file into the piano roll, so you can bring in a clip
// written elsewhere instead of drawing it from scratch.
namespace MidiIo
{
    // Reads every note-on/off pair across every track of `file` into `outNotes`,
    // flattened onto one timeline in beats (quarter notes). Notes come in flat -
    // any pitch bend / CC data already in the file is not interpreted. Returns ""
    // on success, or a message to show the user on failure.
    inline juce::String importFile(const juce::File& file, std::vector<MpeNote>& outNotes)
    {
        outNotes.clear();

        juce::FileInputStream stream(file);
        if (! stream.openedOk())
            return "Couldn't open that file.";

        juce::MidiFile midiFile;
        if (! midiFile.readFrom(stream) || midiFile.getNumTracks() == 0)
            return "That doesn't look like a MIDI file.";

        const short timeFormat = midiFile.getTimeFormat();
        if (timeFormat <= 0)
            return "SMPTE-timecode MIDI files aren't supported - re-export it from your DAW "
                   "using ticks-per-quarter-note timing.";
        const double ticksPerQuarter = (double) timeFormat;

        juce::MidiMessageSequence merged;
        for (int t = 0; t < midiFile.getNumTracks(); ++t)
            merged.addSequence(*midiFile.getTrack(t), 0.0);
        merged.updateMatchedPairs();

        for (int i = 0; i < merged.getNumEvents(); ++i)
        {
            auto* ev = merged.getEventPointer(i);
            if (! ev->message.isNoteOn())
                continue;

            const double startTicks = ev->message.getTimeStamp();
            const double endTicks = ev->noteOffObject != nullptr
                                        ? ev->noteOffObject->message.getTimeStamp()
                                        : startTicks + ticksPerQuarter;

            MpeNote n;
            n.startBeat = startTicks / ticksPerQuarter;
            n.lengthBeats = juce::jmax(0.05, (endTicks - startTicks) / ticksPerQuarter);
            n.pitch = juce::jlimit(0, 127, ev->message.getNoteNumber());
            n.velocity = juce::jlimit(0.05f, 1.0f, ev->message.getFloatVelocity());
            n.bend.clearAndReset();
            n.bend.conformEnd(n.lengthBeats);
            outNotes.push_back(std::move(n));
        }

        if (outNotes.empty())
            return "No notes found in that file.";

        std::stable_sort(outNotes.begin(), outNotes.end(),
            [](const MpeNote& a, const MpeNote& b) { return a.startBeat < b.startBeat; });

        return {};
    }

}
