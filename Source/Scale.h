#pragma once

#include <juce_core/juce_core.h>

// Musical-scale helper for the scale viewer. A scale is a 12-bit mask of which
// semitones (relative to the root) are in the scale; bit 0 = root.
namespace Scale
{
    enum Type
    {
        chromatic = 0, major, minorNatural, minorHarmonic, minorMelodic,
        dorian, phrygian, lydian, mixolydian, locrian,
        majorPentatonic, minorPentatonic, blues, wholeTone,
        numTypes
    };

    inline juce::String typeName(int t)
    {
        static const char* names[numTypes] = {
            "Chromatic (off)", "Major", "Minor (natural)", "Minor (harmonic)", "Minor (melodic)",
            "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
            "Major pentatonic", "Minor pentatonic", "Blues", "Whole tone"
        };
        return names[juce::jlimit(0, (int) numTypes - 1, t)];
    }

    inline juce::String rootName(int r)
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return n[((r % 12) + 12) % 12];
    }

    // bit i (i = 0..11) set -> semitone i above the root is in the scale
    inline juce::uint16 mask(int t)
    {
        switch (t)
        {
            case major:           return 0b101010110101;
            case minorNatural:    return 0b010110101101;
            case minorHarmonic:   return 0b100110101101;
            case minorMelodic:    return 0b101010101101;
            case dorian:          return 0b011010101101;
            case phrygian:        return 0b010110101011;
            case lydian:          return 0b101011010101;
            case mixolydian:      return 0b011010110101;
            case locrian:         return 0b010101101011;
            case majorPentatonic: return 0b001010010101;
            case minorPentatonic: return 0b010010101001;
            case blues:           return 0b010011101001;
            case wholeTone:       return 0b010101010101;
            case chromatic:
            default:              return 0b111111111111;
        }
    }

    inline bool contains(int t, int root, int pitch)
    {
        const int d = (((pitch - root) % 12) + 12) % 12;
        return (mask(t) >> d) & 1;
    }

    // Nearest in-scale pitch to `pitch` (ties round up).
    inline int snap(int t, int root, int pitch)
    {
        if (contains(t, root, pitch))
            return pitch;
        for (int step = 1; step <= 6; ++step)
        {
            if (contains(t, root, pitch + step)) return pitch + step;
            if (contains(t, root, pitch - step)) return pitch - step;
        }
        return pitch;
    }
}
