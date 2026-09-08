#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

// A bend point in a note's pitch curve. Points are joined by straight lines to
// form the note's "chord"; a parametric shape (see MpeNote::shape) can then ride
// on top of that chord.
struct CurvePoint
{
    double beat = 0.0;    // position relative to the note's start, in beats
    float value = 0.0f;   // semitone offset from the note's base pitch
};

// Piecewise-linear pitch envelope through a set of bend points.
class ExpressionCurve
{
public:
    explicit ExpressionCurve(float defaultValue = 0.0f) : defaultVal(defaultValue)
    {
        points.push_back({ 0.0, defaultValue });
    }

    float defaultValue() const { return defaultVal; }
    const std::vector<CurvePoint>& getPoints() const { return points; }
    int size() const { return (int) points.size(); }

    void clearAndReset()
    {
        points.clear();
        points.push_back({ 0.0, defaultVal });
    }

    int addPoint(double beat, float value)
    {
        CurvePoint p { std::max(0.0, beat), value };
        auto insertAt = std::upper_bound(points.begin(), points.end(), p,
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });
        auto it = points.insert(insertAt, p);
        return (int) std::distance(points.begin(), it);
    }

    // Move point `index` in time + value, re-sorting. Point 0 is pinned to beat 0.
    int movePoint(int index, double beat, float value)
    {
        if (index < 0 || index >= (int) points.size())
            return index;
        if (index == 0)
            beat = 0.0;

        const double nb = std::max(0.0, beat);
        points[(size_t) index] = { nb, value };
        std::stable_sort(points.begin(), points.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });

        for (int i = 0; i < (int) points.size(); ++i)
            if (points[(size_t) i].beat == nb && points[(size_t) i].value == value)
                return i;
        return index;
    }

    void removePoint(int index)
    {
        if ((int) points.size() <= 2)
            return;   // keep the first + last points - they span the note
        if (index > 0 && index < (int) points.size() - 1)
            points.erase(points.begin() + index);
    }

    int lastIndex() const { return (int) points.size() - 1; }
    double lastBeat() const { return points.empty() ? 0.0 : points.back().beat; }
    double firstBeat() const { return points.empty() ? 0.0 : points.front().beat; }
    double beatBefore(int index) const
    {
        return (index > 0 && index < (int) points.size()) ? points[(size_t) (index - 1)].beat : 0.0;
    }

    // Ensure a trailing point marks the note end. Returns the implied note length.
    double conformEnd(double lengthBeats)
    {
        if (points.empty())
            points.push_back({ 0.0, defaultVal });

        if ((int) points.size() < 2)
        {
            points.push_back({ std::max(0.25, lengthBeats), points.back().value });
            return points.back().beat;
        }

        if (points.back().beat < lengthBeats - 1.0e-6)
        {
            points.push_back({ lengthBeats, points.back().value });
            return lengthBeats;
        }
        return points.back().beat;
    }

    void setPoint(double beat, float value)
    {
        beat = std::max(0.0, beat);
        for (auto& p : points)
            if (std::abs(p.beat - beat) < 1.0e-6) { p.value = value; return; }
        addPoint(beat, value);
    }

    // Linear value at a beat offset from the note start (holds the end values).
    float sample(double beat) const
    {
        if (points.empty())
            return defaultVal;
        if (beat <= points.front().beat)
            return points.front().value;
        if (beat >= points.back().beat)
            return points.back().value;

        size_t i = 0;
        while (i + 1 < points.size() && points[i + 1].beat <= beat)
            ++i;
        if (i + 1 >= points.size())
            return points.back().value;

        const auto& L = points[i];
        const auto& R = points[i + 1];
        const double span = R.beat - L.beat;
        if (span < 1.0e-9)
            return R.value;
        const float t = (float) ((beat - L.beat) / span);
        return L.value + t * (R.value - L.value);
    }

private:
    float defaultVal;
    std::vector<CurvePoint> points;
};

enum class BendShape { straight = 0, sine = 1, triangle = 2 };

// One note in the piano roll. Its pitch over time is `pitch` (the base key) plus
// the `bend` chord (piecewise-linear through the bend points), plus - when `shape`
// is not straight - a sine/triangle wave riding on the chord whose cycle count,
// horizontal skew and start/end amplitude are all editable.
struct MpeNote
{
    juce::Uuid id;

    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int pitch = 60;
    float velocity = 0.8f;
    float releaseVelocity = 0.5f;

    ExpressionCurve bend { 0.0f };

    BendShape shape = BendShape::straight;
    float shapeCycles   = 4.0f;    // cycle count between the two bend points; always a multiple
                                   // of 0.5 so the wave returns to the chord at the end point
    float shapeSkew     = 1.0f;    // phase warp: >1 bunches cycles toward the end
    float shapeAmpStart = 0.0f;    // wave amplitude (semitones) at the start point
    float shapeAmpEnd   = 2.0f;    // wave amplitude (semitones) at the end point

    float shapeCycleCount() const { return juce::jmax(1.0f, shapeCycles); }

    int assignedChannel = -1;
    bool isSounding = false;

    double endBeat() const { return startBeat + lengthBeats; }
    bool isActiveAt(double beat) const { return beat >= startBeat && beat < endBeat(); }

    // Full semitone offset at a note-relative beat: chord + parametric wave.
    float bendOffsetAt(double beatOffset) const
    {
        const float chord = bend.sample(beatOffset);
        if (shape == BendShape::straight)
            return chord;

        const double first = bend.firstBeat();
        const double last  = std::max(bend.lastBeat(), first + 1.0e-6);
        const float t = (float) juce::jlimit(0.0, 1.0, (beatOffset - first) / (last - first));

        const float amp    = shapeAmpStart + t * (shapeAmpEnd - shapeAmpStart);
        const float skew   = juce::jlimit(0.2f, 5.0f, shapeSkew);
        const float phase  = std::pow(t, skew) * shapeCycleCount();

        float w;
        if (shape == BendShape::sine)
        {
            w = std::sin(phase * juce::MathConstants<float>::twoPi);
        }
        else
        {
            const float p = phase - std::floor(phase);   // 0..1
            w = (p < 0.25f) ? 4.0f * p
              : (p < 0.75f) ? 2.0f - 4.0f * p
                            : 4.0f * p - 4.0f;            // 0 -> 1 -> 0 -> -1 -> 0
        }
        return chord + w * amp;
    }

    float bendAtAbsBeat(double absBeat) const { return bendOffsetAt(absBeat - startBeat); }
    float pitchAtAbsBeat(double absBeat) const { return (float) pitch + bendAtAbsBeat(absBeat); }
};
