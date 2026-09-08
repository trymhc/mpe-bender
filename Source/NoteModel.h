#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>

// A single breakpoint in a per-note expression curve (pitch bend, pressure, or timbre).
struct CurvePoint
{
    double beat = 0.0;   // position relative to the note's start, in beats
    float value = 0.0f;  // meaning depends on the curve: semitone offset, 0..1, etc.
};

// A breakpoint envelope sampled over the lifetime of a note. Linear interpolation
// between points; holds the last value past the final point.
class ExpressionCurve
{
public:
    explicit ExpressionCurve(float defaultValue) : defaultVal(defaultValue)
    {
        points.push_back({ 0.0, defaultValue });
    }

    float defaultValue() const { return defaultVal; }

    const std::vector<CurvePoint>& getPoints() const { return points; }

    void clearAndReset()
    {
        points.clear();
        points.push_back({ 0.0, defaultVal });
    }

    // Inserts or moves a point, keeping the list sorted by beat.
    void setPoint(double beat, float value)
    {
        beat = std::max(0.0, beat);
        auto it = std::find_if(points.begin(), points.end(), [&](const CurvePoint& p)
        {
            return std::abs(p.beat - beat) < 1.0e-6;
        });

        if (it != points.end())
        {
            it->value = value;
            return;
        }

        CurvePoint p { beat, value };
        auto insertAt = std::upper_bound(points.begin(), points.end(), p,
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });
        points.insert(insertAt, p);
    }

    void removePointNear(double beat, double tolerance = 0.05)
    {
        if (points.size() <= 1)
            return;

        points.erase(std::remove_if(points.begin(), points.end(), [&](const CurvePoint& p)
        {
            return std::abs(p.beat - beat) < tolerance && p.beat > 1.0e-6;
        }), points.end());
    }

    // Sample the curve at a beat offset from the note's start.
    float sample(double beat) const
    {
        if (points.empty())
            return defaultVal;

        if (beat <= points.front().beat)
            return points.front().value;

        if (beat >= points.back().beat)
            return points.back().value;

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto& a = points[i];
            const auto& b = points[i + 1];

            if (beat >= a.beat && beat <= b.beat)
            {
                if (b.beat - a.beat < 1.0e-9)
                    return b.value;

                auto t = (float) ((beat - a.beat) / (b.beat - a.beat));
                return a.value + t * (b.value - a.value);
            }
        }

        return points.back().value;
    }

private:
    float defaultVal;
    std::vector<CurvePoint> points;
};

// One note in the piano roll, with its own per-note MPE expression curves.
struct MpeNote
{
    juce::Uuid id;

    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int pitch = 60;           // MIDI note number, the note's base pitch
    float velocity = 0.8f;    // note-on velocity, 0..1
    float releaseVelocity = 0.5f;

    // Expression lanes. Values:
    //   pitchBend: semitone offset from base pitch (clamped to +/- pitchBendRangeSemitones)
    //   pressure : 0..1 (channel aftertouch)
    //   timbre   : 0..1, centered at 0.5 (CC74 / "slide")
    ExpressionCurve pitchBend { 0.0f };
    ExpressionCurve pressure  { 0.8f };
    ExpressionCurve timbre    { 0.5f };

    // Runtime-only playback state, not persisted.
    int assignedChannel = -1;
    bool isSounding = false;

    double endBeat() const { return startBeat + lengthBeats; }

    bool isActiveAt(double beat) const
    {
        return beat >= startBeat && beat < endBeat();
    }
};
