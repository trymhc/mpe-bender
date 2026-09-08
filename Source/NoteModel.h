#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

// A node in a note's pitch curve. `anchor` nodes are the structural bend points
// (drawn as circles, land on semitones); non-anchor nodes are curve shapers
// (drawn as diamonds) that bow the curve between two anchors. Any number of
// shapers may sit between two anchors.
struct CurvePoint
{
    double beat = 0.0;    // position relative to the note's start, in beats
    float value = 0.0f;   // semitone offset from the note's base pitch
    bool anchor = true;
};

// Kept only for migrating pre-0.5 saved state (per-segment "tension" scalar).
inline float applyTension(float t, float tension)
{
    t = std::min(1.0f, std::max(0.0f, t));
    if (tension > 1.0e-4f)  return std::pow(t, 1.0f + tension * 3.0f);
    if (tension < -1.0e-4f) return 1.0f - std::pow(1.0f - t, 1.0f - tension * 3.0f);
    return t;
}

// A pitch envelope over the life of a note: straight between adjacent anchors,
// a Catmull-Rom spline through any run of anchor -> shapers... -> anchor.
class ExpressionCurve
{
public:
    explicit ExpressionCurve(float defaultValue = 0.0f) : defaultVal(defaultValue)
    {
        points.push_back({ 0.0, defaultValue, true });
    }

    float defaultValue() const { return defaultVal; }
    const std::vector<CurvePoint>& getPoints() const { return points; }
    int size() const { return (int) points.size(); }

    void clearAndReset()
    {
        points.clear();
        points.push_back({ 0.0, defaultVal, true });
    }

    // Insert a node, keeping the list sorted by beat. Returns its index.
    int addPoint(double beat, float value, bool anchor = true)
    {
        CurvePoint p { std::max(0.0, beat), value, anchor };
        auto insertAt = std::upper_bound(points.begin(), points.end(), p,
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });
        auto it = points.insert(insertAt, p);
        return (int) std::distance(points.begin(), it);
    }

    // Move node `index` in time + value, re-sorting. Node 0 is the note's start
    // anchor: its beat is pinned to 0. Keeps the node's anchor/shaper kind.
    int movePoint(int index, double beat, float value)
    {
        if (index < 0 || index >= (int) points.size())
            return index;
        if (index == 0)
            beat = 0.0;

        const bool keepAnchor = points[(size_t) index].anchor;
        const double nb = std::max(0.0, beat);
        points[(size_t) index] = { nb, value, keepAnchor };
        std::stable_sort(points.begin(), points.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });

        for (int i = 0; i < (int) points.size(); ++i)
            if (points[(size_t) i].beat == nb && points[(size_t) i].value == value)
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

    bool isAnchor(int index) const
    {
        return index >= 0 && index < (int) points.size() && points[(size_t) index].anchor;
    }

    // legacy loaders
    void setPoint(double beat, float value) { setPoint(beat, value, true); }
    void setPoint(double beat, float value, bool anchor)
    {
        beat = std::max(0.0, beat);
        for (auto& p : points)
            if (std::abs(p.beat - beat) < 1.0e-6) { p.value = value; p.anchor = anchor; return; }
        addPoint(beat, value, anchor);
    }

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
        const float t = span < 1.0e-9 ? 0.0f : (float) ((beat - L.beat) / span);

        if (L.anchor && R.anchor)
            return L.value + t * (R.value - L.value);

        // curved run: widen [a..b] so both ends are anchors (or the list ends)
        int a = (int) i;
        while (a > 0 && ! points[(size_t) a].anchor) --a;
        int b = (int) i + 1;
        while (b < (int) points.size() - 1 && ! points[(size_t) b].anchor) ++b;

        auto V = [&](int idx) { return points[(size_t) juce::jlimit(a, b, idx)].value; };
        const float p0 = V((int) i - 1);
        const float p1 = V((int) i);
        const float p2 = V((int) i + 1);
        const float p3 = V((int) i + 2);
        const float t2 = t * t;
        const float t3 = t2 * t;

        return 0.5f * ((2.0f * p1)
                     + (-p0 + p2) * t
                     + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                     + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
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
    int pitch = 60;
    float velocity = 0.8f;
    float releaseVelocity = 0.5f;

    ExpressionCurve bend { 0.0f };

    int assignedChannel = -1;
    bool isSounding = false;

    double endBeat() const { return startBeat + lengthBeats; }

    bool isActiveAt(double beat) const
    {
        return beat >= startBeat && beat < endBeat();
    }

    float bendAtAbsBeat(double absBeat) const { return bend.sample(absBeat - startBeat); }
    float pitchAtAbsBeat(double absBeat) const { return (float) pitch + bendAtAbsBeat(absBeat); }
};
