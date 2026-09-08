#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

// A single breakpoint in a note's pitch curve.
struct CurvePoint
{
    double beat = 0.0;    // position relative to the note's start, in beats
    float value = 0.0f;   // semitone offset from the note's base pitch (may be fractional / negative)
    float tension = 0.0f; // -1..+1 curve of the segment leaving this point; 0 = straight line
};

// Ease `t` (0..1) through a single-curvature "tension" bend, FL-automation style.
// tension > 0 : ease-in  (slow start, fast finish)
// tension < 0 : ease-out (fast start, slow finish)
inline float applyTension(float t, float tension)
{
    t = std::min(1.0f, std::max(0.0f, t));
    if (tension > 1.0e-4f)
        return std::pow(t, 1.0f + tension * 3.0f);
    if (tension < -1.0e-4f)
        return 1.0f - std::pow(1.0f - t, 1.0f - tension * 3.0f);
    return t;
}

// Given a normalised curve position `w` (0..1) that the segment should pass through
// at its time-midpoint, return the tension that produces it. Inverse of
// applyTension(0.5, tension).
inline float tensionForMidpoint(float w)
{
    w = std::min(0.999f, std::max(0.001f, w));
    const float l = std::log(0.5f);
    if (w <= 0.5f)
        return juce::jlimit(-1.0f, 1.0f, ((std::log(w) / l) - 1.0f) / 3.0f);
    return juce::jlimit(-1.0f, 1.0f, (1.0f - (std::log(1.0f - w) / l)) / 3.0f);
}

// A breakpoint envelope sampled over the lifetime of a note. Segments interpolate
// linearly unless the left point carries a tension; holds the end values beyond
// the first / last point.
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
    const CurvePoint& getPoint(int i) const { return points[(size_t) i]; }

    void clearAndReset()
    {
        points.clear();
        points.push_back({ 0.0, defaultVal });
    }

    // Insert a new point, keeping the list sorted by beat. Returns its index.
    int addPoint(double beat, float value)
    {
        CurvePoint p { std::max(0.0, beat), value };
        auto insertAt = std::upper_bound(points.begin(), points.end(), p,
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });
        auto it = points.insert(insertAt, p);
        return (int) std::distance(points.begin(), it);
    }

    // Move an existing point in time and value, re-sorting. Point 0 is the note's
    // start anchor - its beat is pinned to 0, only its value moves. Returns the
    // point's new index.
    int movePoint(int index, double beat, float value)
    {
        if (index < 0 || index >= (int) points.size())
            return index;

        if (index == 0)
            beat = 0.0;

        const float keepTension = points[(size_t) index].tension;
        points[(size_t) index] = { std::max(0.0, beat), value, keepTension };
        std::stable_sort(points.begin(), points.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });

        for (int i = 0; i < (int) points.size(); ++i)
            if (points[(size_t) i].beat == std::max(0.0, beat) && points[(size_t) i].value == value)
                return i;
        return index;
    }

    void removePoint(int index)
    {
        if ((int) points.size() <= 1)
            return;
        if (index >= 0 && index < (int) points.size())
            points.erase(points.begin() + index);
    }

    void setTension(int index, float tension)
    {
        if (index >= 0 && index < (int) points.size())
            points[(size_t) index].tension = juce::jlimit(-1.0f, 1.0f, tension);
    }

    // Legacy helpers still used when building demo content / loading old state.
    void setPoint(double beat, float value)
    {
        beat = std::max(0.0, beat);
        for (auto& p : points)
            if (std::abs(p.beat - beat) < 1.0e-6) { p.value = value; return; }
        addPoint(beat, value);
    }

    // Used when restoring saved state.
    void setPoint(double beat, float value, float tension)
    {
        beat = std::max(0.0, beat);
        for (auto& p : points)
            if (std::abs(p.beat - beat) < 1.0e-6) { p.value = value; p.tension = tension; return; }
        const int idx = addPoint(beat, value);
        points[(size_t) idx].tension = tension;
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
                return a.value + applyTension(t, a.tension) * (b.value - a.value);
            }
        }
        return points.back().value;
    }

private:
    float defaultVal;
    std::vector<CurvePoint> points;
};

// One note in the piano roll. Its pitch over time is `pitch` (the base key) plus
// the `bend` curve (semitone offset), so a note can start on one key and sweep to
// another - drawn and heard as a single bending note.
struct MpeNote
{
    juce::Uuid id;

    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int pitch = 60;           // MIDI note number the note's first point sits on
    float velocity = 0.8f;    // note-on velocity, 0..1
    float releaseVelocity = 0.5f;

    ExpressionCurve bend { 0.0f };   // semitone offset from `pitch` over the note

    // Runtime-only playback state, not persisted.
    int assignedChannel = -1;
    bool isSounding = false;

    double endBeat() const { return startBeat + lengthBeats; }

    bool isActiveAt(double beat) const
    {
        return beat >= startBeat && beat < endBeat();
    }

    // Semitone offset (relative to `pitch`) at an absolute beat position.
    float bendAtAbsBeat(double absBeat) const
    {
        return bend.sample(absBeat - startBeat);
    }

    // Fractional MIDI pitch at an absolute beat position.
    float pitchAtAbsBeat(double absBeat) const
    {
        return (float) pitch + bendAtAbsBeat(absBeat);
    }
};
