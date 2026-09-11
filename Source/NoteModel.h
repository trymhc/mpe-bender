#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

// A node in a note's pitch curve. `shaper` nodes are "diamonds" - they bow the
// otherwise-straight line between the surrounding bend points; non-shaper nodes
// are the hard bend points that the curve passes through with a corner.
struct CurvePoint
{
    double beat = 0.0;    // position relative to the note's start, in beats
    float value = 0.0f;   // semitone offset from the note's base pitch
    bool shaper = false;
};

// Pitch envelope: straight between hard bend points, Catmull-Rom through any run
// of bendPoint -> shapers... -> bendPoint.
class ExpressionCurve
{
public:
    explicit ExpressionCurve(float defaultValue = 0.0f) : defaultVal(defaultValue)
    {
        points.push_back({ 0.0, defaultValue, false });
    }

    float defaultValue() const { return defaultVal; }
    const std::vector<CurvePoint>& getPoints() const { return points; }
    int size() const { return (int) points.size(); }
    bool isShaper(int i) const { return i >= 0 && i < (int) points.size() && points[(size_t) i].shaper; }

    void clearAndReset()
    {
        points.clear();
        points.push_back({ 0.0, defaultVal, false });
    }

    int addPoint(double beat, float value, bool shaper = false)
    {
        CurvePoint p { std::max(0.0, beat), value, shaper };
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

        const bool keepShaper = points[(size_t) index].shaper;
        const double nb = std::max(0.0, beat);
        points[(size_t) index] = { nb, value, keepShaper };
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
            points.push_back({ 0.0, defaultVal, false });

        if ((int) points.size() < 2)
        {
            points.push_back({ std::max(0.25, lengthBeats), points.back().value, false });
            return points.back().beat;
        }

        points.back().shaper = false;
        if (points.back().beat < lengthBeats - 1.0e-6)
        {
            points.push_back({ lengthBeats, points.back().value, false });
            return lengthBeats;
        }
        return points.back().beat;
    }

    void setPoint(double beat, float value, bool shaper = false)
    {
        beat = std::max(0.0, beat);
        for (auto& p : points)
            if (std::abs(p.beat - beat) < 1.0e-6) { p.value = value; p.shaper = shaper; return; }
        addPoint(beat, value, shaper);
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
        if (span < 1.0e-9)
            return R.value;
        const float t = (float) ((beat - L.beat) / span);

        if (! L.shaper && ! R.shaper)
            return L.value + t * (R.value - L.value);   // straight segment

        // curved run: widen to the enclosing hard points, Catmull-Rom in value
        int a = (int) i;
        while (a > 0 && points[(size_t) a].shaper) --a;
        int b = (int) i + 1;
        while (b < (int) points.size() - 1 && points[(size_t) b].shaper) ++b;

        auto V = [&](int idx) { return points[(size_t) std::min(std::max(idx, a), b)].value; };
        const float p0 = V((int) i - 1), p1 = V((int) i), p2 = V((int) i + 1), p3 = V((int) i + 2);
        const float t2 = t * t, t3 = t2 * t;
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * t
                     + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                     + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
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

    // The wave rides on top of the chord between two bend points, picked in the
    // editor (Shift-click a pair of points). -1 means "not set" - the wave then
    // spans the whole note, first bend point to last, as it always used to.
    double shapeFromBeat = -1.0;
    double shapeToBeat   = -1.0;

    bool hasShapeRange() const { return shapeFromBeat >= 0.0 && shapeToBeat > shapeFromBeat; }
    // Clamped to the note's current bend span, so shrinking a note after picking a
    // section (e.g. dragging its end in) can't leave the wave reaching past the end.
    double shapeSpanFirstBeat() const
    {
        return hasShapeRange() ? juce::jlimit(0.0, bend.lastBeat(), shapeFromBeat) : bend.firstBeat();
    }
    double shapeSpanLastBeat() const
    {
        if (! hasShapeRange())
            return std::max(bend.lastBeat(), bend.firstBeat() + 1.0e-6);
        const double first = shapeSpanFirstBeat();
        return std::max(first + 1.0e-6, juce::jlimit(first, bend.lastBeat(), shapeToBeat));
    }

    float shapeCycleCount() const { return juce::jmax(1.0f, shapeCycles); }

    bool muted = false;   // skipped by the engine; drawn hollow in the roll

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

        const double first = shapeSpanFirstBeat();
        const double last  = shapeSpanLastBeat();
        if (beatOffset < first - 1.0e-9 || beatOffset > last + 1.0e-9)
            return chord;   // outside the picked section - straight chord, no wave

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
