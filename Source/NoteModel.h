#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

// A node in a note's pitch curve. Plain points are joined by a straight line.
// Either side of a point can also carry its own Bezier tangent handle - drag it
// out to bow the curve approaching (`in`) or leaving (`out`) that point, same as
// a vertex handle in a vector-drawing tool. Handle positions are absolute (beat,
// value); the beat is always kept within the point's own neighbours so a
// segment's time axis never runs backwards.
//
// `shaper` is a legacy flag from the old "diamond" curve tool (a point that bowed
// a Catmull-Rom spline through it). It's no longer created by the UI, only kept
// so older projects' curves still play back the same; ExpressionCurve::sample()
// still honours it when no Bezier handle is present on either side.
struct CurvePoint
{
    double beat = 0.0;    // position relative to the note's start, in beats
    float value = 0.0f;   // semitone offset from the note's base pitch
    bool shaper = false;

    bool hasIn = false;
    double inBeat = 0.0;
    float inValue = 0.0f;

    bool hasOut = false;
    double outBeat = 0.0;
    float outValue = 0.0f;
};

// Pitch envelope: straight between plain points; a cubic Bezier wherever either
// endpoint of a segment has a handle; Catmull-Rom through any run of
// bendPoint -> shapers... -> bendPoint (legacy projects only).
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
    bool hasAnyHandles() const
    {
        for (auto& p : points) if (p.hasIn || p.hasOut) return true;
        return false;
    }

    void clearAndReset()
    {
        points.clear();
        points.push_back({ 0.0, defaultVal, false });
    }

    int addPoint(double beat, float value, bool shaper = false)
    {
        CurvePoint p; p.beat = std::max(0.0, beat); p.value = value; p.shaper = shaper;
        auto insertAt = std::upper_bound(points.begin(), points.end(), p,
            [](const CurvePoint& a, const CurvePoint& b) { return a.beat < b.beat; });
        auto it = points.insert(insertAt, p);
        const int idx = (int) std::distance(points.begin(), it);

        // A new point landing inside an existing Bezier segment splits it in two;
        // keep the old neighbours' handles from reaching past the point that just
        // landed between them, so each segment's time axis stays monotonic.
        if (idx > 0)
        {
            auto& prev = points[(size_t) (idx - 1)];
            if (prev.hasOut) prev.outBeat = std::min(prev.outBeat, points[(size_t) idx].beat);
        }
        if (idx + 1 < (int) points.size())
        {
            auto& next = points[(size_t) (idx + 1)];
            if (next.hasIn) next.inBeat = std::max(next.inBeat, points[(size_t) idx].beat);
        }
        return idx;
    }

    // Move point `index` in time + value, re-sorting. Point 0 is pinned to beat 0.
    // Any handles on the point move along with it, keeping their offset.
    int movePoint(int index, double beat, float value)
    {
        if (index < 0 || index >= (int) points.size())
            return index;
        if (index == 0)
            beat = 0.0;

        auto p = points[(size_t) index];
        const double nb = std::max(0.0, beat);
        const double dBeat = nb - p.beat;
        const float dValue = value - p.value;
        p.beat = nb;
        p.value = value;
        if (p.hasIn)  { p.inBeat  += dBeat; p.inValue  += dValue; }
        if (p.hasOut) { p.outBeat += dBeat; p.outValue += dValue; }
        points[(size_t) index] = p;

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

    // Turns a point's Bezier handle(s) on or off. Turning on seeds the handle(s)
    // 25% of the way toward each neighbour, flat (same value as the point), so
    // the curve starts out looking unchanged until you drag from there. A point
    // has no `in` handle if it's the note's first point, no `out` handle if it's
    // the last.
    void toggleHandlesAt(int index)
    {
        if (index < 0 || index >= (int) points.size())
            return;
        auto& p = points[(size_t) index];
        if (p.hasIn || p.hasOut)
        {
            p.hasIn = false;
            p.hasOut = false;
            return;
        }
        if (index > 0)
        {
            auto& prev = points[(size_t) (index - 1)];
            p.hasIn = true;
            p.inBeat = p.beat - (p.beat - prev.beat) * 0.25;
            p.inValue = p.value;
        }
        if (index < (int) points.size() - 1)
        {
            auto& next = points[(size_t) (index + 1)];
            p.hasOut = true;
            p.outBeat = p.beat + (next.beat - p.beat) * 0.25;
            p.outValue = p.value;
        }
    }

    void setInHandle(int index, double beat, float value)
    {
        if (index < 0 || index >= (int) points.size()) return;
        points[(size_t) index].hasIn = true;
        points[(size_t) index].inBeat = beat;
        points[(size_t) index].inValue = value;
    }
    void setOutHandle(int index, double beat, float value)
    {
        if (index < 0 || index >= (int) points.size()) return;
        points[(size_t) index].hasOut = true;
        points[(size_t) index].outBeat = beat;
        points[(size_t) index].outValue = value;
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
            CurvePoint p; p.beat = std::max(0.25, lengthBeats); p.value = points.back().value;
            points.push_back(p);
            return points.back().beat;
        }

        points.back().shaper = false;
        if (points.back().beat < lengthBeats - 1.0e-6)
        {
            CurvePoint p; p.beat = lengthBeats; p.value = points.back().value;
            points.push_back(p);
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

        if (L.hasOut || R.hasIn)
            return sampleBezier(L, R, beat);

        const float t = (float) ((beat - L.beat) / span);

        if (! L.shaper && ! R.shaper)
            return L.value + t * (R.value - L.value);   // straight segment

        // legacy Catmull-Rom fallback: a run of bendPoint -> shapers... -> bendPoint
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
    // Cubic Bezier between L and R using whichever of L's out-handle / R's
    // in-handle are set (an unset one degenerates that side to a straight
    // tangent, i.e. the control point sits on L or R itself). Beat isn't a
    // linear function of the Bezier parameter t once handles are involved, so
    // solve for t by bisection - X(t) is guaranteed monotonic because handle
    // beats are always clamped to stay within [L.beat, R.beat] by whoever sets
    // them (the editor).
    float sampleBezier(const CurvePoint& L, const CurvePoint& R, double beat) const
    {
        const double c1beat = L.hasOut ? L.outBeat : L.beat;
        const float  c1val  = L.hasOut ? L.outValue : L.value;
        const double c2beat = R.hasIn ? R.inBeat : R.beat;
        const float  c2val  = R.hasIn ? R.inValue : R.value;

        auto xAt = [&](double t)
        {
            const double mt = 1.0 - t;
            return mt * mt * mt * L.beat + 3.0 * mt * mt * t * c1beat
                 + 3.0 * mt * t * t * c2beat + t * t * t * R.beat;
        };

        double tLo = 0.0, tHi = 1.0;
        for (int iter = 0; iter < 24; ++iter)
        {
            const double tMid = 0.5 * (tLo + tHi);
            if (xAt(tMid) < beat) tLo = tMid; else tHi = tMid;
        }
        const double t = 0.5 * (tLo + tHi);
        const double mt = 1.0 - t;
        return (float) (mt * mt * mt * L.value + 3.0 * mt * mt * t * c1val
                       + 3.0 * mt * t * t * c2val + t * t * t * R.value);
    }

    float defaultVal;
    std::vector<CurvePoint> points;
};

enum class BendShape { straight = 0, sine = 1, triangle = 2 };

// A sine/triangle wave riding on top of the chord between two points on the
// note, picked in the editor (Shift-click a pair of bend points). A note can
// have any number of these, each independent, as long as they don't overlap.
struct WaveSection
{
    double fromBeat = 0.0;
    double toBeat   = 1.0;
    BendShape shape = BendShape::sine;
    float cycles    = 4.0f;    // cycle count; always a multiple of 0.5 so the wave
                                // returns to the chord at the end point
    float skew      = 1.0f;    // phase warp: >1 bunches cycles toward the end
    float ampStart  = 0.0f;    // wave amplitude (semitones) at the start point
    float ampEnd    = 0.25f;   // wave amplitude (semitones) at the end point

    float cycleCount() const { return juce::jmax(1.0f, cycles); }
    bool containsBeat(double beat) const { return beat >= fromBeat - 1.0e-9 && beat <= toBeat + 1.0e-9; }
};

// One note in the piano roll. Its pitch over time is `pitch` (the base key) plus
// the `bend` chord (piecewise-linear/Bezier through the bend points), plus
// whatever `waveSections` cover the sampled beat.
struct MpeNote
{
    juce::Uuid id;

    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int pitch = 60;
    float velocity = 0.8f;
    float releaseVelocity = 0.5f;

    ExpressionCurve bend { 0.0f };
    std::vector<WaveSection> waveSections;

    bool muted = false;   // skipped by the engine; drawn hollow in the roll

    int assignedChannel = -1;
    bool isSounding = false;

    double endBeat() const { return startBeat + lengthBeats; }
    bool isActiveAt(double beat) const { return beat >= startBeat && beat < endBeat(); }

    // Full semitone offset at a note-relative beat: chord + whichever wave
    // section (if any) covers this beat.
    float bendOffsetAt(double beatOffset) const
    {
        const float chord = bend.sample(beatOffset);
        for (auto& ws : waveSections)
        {
            if (! ws.containsBeat(beatOffset))
                continue;

            const double span = juce::jmax(1.0e-6, ws.toBeat - ws.fromBeat);
            const float t = (float) juce::jlimit(0.0, 1.0, (beatOffset - ws.fromBeat) / span);

            const float amp   = ws.ampStart + t * (ws.ampEnd - ws.ampStart);
            const float skew  = juce::jlimit(0.2f, 5.0f, ws.skew);
            const float phase = std::pow(t, skew) * ws.cycleCount();

            float w;
            if (ws.shape == BendShape::sine)
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
        return chord;
    }

    float bendAtAbsBeat(double absBeat) const { return bendOffsetAt(absBeat - startBeat); }
    float pitchAtAbsBeat(double absBeat) const { return (float) pitch + bendAtAbsBeat(absBeat); }
};
