#include "PianoRollComponent.h"
#include "UiTheme.h"

PianoRollComponent::PianoRollComponent(MpePianoRollAudioProcessor& processorToUse)
    : processor(processorToUse)
{
    setWantsKeyboardFocus(true);
    updateContentSize();
    startTimerHz(30);
}

// ---------------------------------------------------------------------------
//  Zoom
// ---------------------------------------------------------------------------

void PianoRollComponent::updateContentSize()
{
    float minPixelsPerBeat = defaultPixelsPerBeat;
    float minRowHeight = 6.0f;
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const double loop = juce::jmax(1.0, processor.getLoopLengthBeats());
        const float availW = (float) vp->getWidth() - (float) keyboardWidth - 16.0f;
        if (availW > 20.0f)
            minPixelsPerBeat = juce::jmin(400.0f, (float) (availW / loop));

        // Never let the pitch range shrink to less than the viewport's height -
        // zooming out (wheel, pinch, or Ctrl+right-click) should never leave empty
        // space below the keyboard.
        const int totalRows = highestPitch - lowestPitch + 1;
        const float availH = (float) vp->getHeight();
        if (availH > 20.0f && totalRows > 0)
            minRowHeight = juce::jlimit(6.0f, 40.0f, availH / (float) totalRows);
    }

    pixelsPerBeat = juce::jlimit(minPixelsPerBeat, 400.0f, pixelsPerBeat);
    rowHeight = juce::jlimit(minRowHeight, 40.0f, rowHeight);

    const int w = keyboardWidth + juce::roundToInt(processor.getLoopLengthBeats() * pixelsPerBeat);
    const int h = juce::roundToInt((float) (highestPitch - lowestPitch + 1) * rowHeight);
    setSize(juce::jmax(1, w), juce::jmax(1, h));
}

void PianoRollComponent::zoomAxes(float fx, float fy, float anchorX, float anchorY)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    const double beatAtX  = beatForX(anchorX);
    const float  pitchAtY = pitchForY(anchorY);
    const float  sx = vp != nullptr ? anchorX - (float) vp->getViewPositionX() : anchorX;
    const float  sy = vp != nullptr ? anchorY - (float) vp->getViewPositionY() : anchorY;

    pixelsPerBeat *= fx;
    rowHeight     *= fy;
    updateContentSize();

    if (vp != nullptr)
        vp->setViewPosition(juce::roundToInt(xForBeat(beatAtX) - sx),
                            juce::roundToInt(yForPitch(pitchAtY) - sy));
    repaint();
}

void PianoRollComponent::zoomBoth(float factor, float anchorX, float anchorY)
{
    zoomAxes(factor, factor, anchorX, anchorY);
}

void PianoRollComponent::resetZoom()
{
    pixelsPerBeat = defaultPixelsPerBeat;
    rowHeight = defaultRowHeight;
    updateContentSize();
    repaint();
}

void PianoRollComponent::zoomAllTheWayOut()
{
    // updateContentSize() clamps both up to their floors (rowHeight's is fixed;
    // pixelsPerBeat's is whatever fits the whole loop in the current viewport
    // width) - starting from 0 always lands on those floors.
    pixelsPerBeat = 0.0f;
    rowHeight = 0.0f;
    updateContentSize();
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        vp->setViewPosition(0, 0);
    repaint();
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();

    float d = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
    if (wheel.isReversed) d = -d;
    if (std::abs(d) < 1.0e-4f)
        return;
    const float step = d > 0.0f ? 1.12f : (1.0f / 1.12f);

    if (e.mods.isCommandDown())
    {
        // Ctrl+wheel = zoom horizontally; Ctrl+Shift+wheel = zoom vertically
        if (e.mods.isShiftDown()) zoomAxes(1.0f, step, e.position.x, e.position.y);
        else                      zoomAxes(step, 1.0f, e.position.x, e.position.y);
        return;
    }

    if (vp == nullptr)
        return;

    // plain wheel = scroll vertically (always); Shift+wheel = scroll horizontally
    if (e.mods.isShiftDown())
    {
        const int amount = juce::roundToInt(d * juce::jmax(60.0f, pixelsPerBeat));
        vp->setViewPosition(vp->getViewPositionX() - amount, vp->getViewPositionY());
    }
    else
    {
        const int amount = juce::roundToInt(d * juce::jmax(48.0f, rowHeight * 4.0f));
        vp->setViewPosition(vp->getViewPositionX(), vp->getViewPositionY() - amount);
    }
}

void PianoRollComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        zoomAxes(scaleFactor, scaleFactor, e.position.x, e.position.y);
}

// ---------------------------------------------------------------------------
//  Geometry
// ---------------------------------------------------------------------------

bool PianoRollComponent::isBlackKey(int pitch) const
{
    static const bool blacks[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    return blacks[((pitch % 12) + 12) % 12];
}

void PianoRollComponent::buildNotePath(const MpeNote& note, juce::Path& path) const
{
    auto yAt = [&](double beatOffset)
    {
        return yForPitch((float) note.pitch + note.bendOffsetAt(beatOffset));
    };

    const float x0 = xForBeat(note.startBeat);
    const float x1 = xForBeat(note.endBeat());
    const bool curvy = ! note.waveSections.empty() || note.bend.hasAnyHandles();
    const float step = curvy ? 1.5f : 3.0f;

    path.startNewSubPath(x0, yAt(0.0));
    for (float x = x0 + step; x < x1; x += step)
        path.lineTo(x, yAt(beatForX(x) - note.startBeat));
    path.lineTo(x1, yAt(note.lengthBeats));
}

juce::Range<float> PianoRollComponent::pitchExtent(const MpeNote& note) const
{
    float lo = 1.0e9f, hi = -1.0e9f;
    const bool curvy = ! note.waveSections.empty() || note.bend.hasAnyHandles();
    const int steps = curvy ? 48 : 8;
    for (int i = 0; i <= steps; ++i)
    {
        const float p = (float) note.pitch + note.bendOffsetAt(note.lengthBeats * i / steps);
        lo = juce::jmin(lo, p);
        hi = juce::jmax(hi, p);
    }
    return { lo, hi };
}

int PianoRollComponent::pointIndexAt(const MpeNote& note, juce::Point<float> pos) const
{
    const auto& pts = note.bend.getPoints();
    const float hitR = pointRadius + 4.0f;
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        auto x = xForBeat(note.startBeat + juce::jlimit(0.0, note.lengthBeats, pts[(size_t) i].beat));
        auto y = yForPitch((float) note.pitch + pts[(size_t) i].value);
        if (pos.getDistanceFrom({ x, y }) <= hitR)
            return i;
    }
    return -1;
}

bool PianoRollComponent::ribbonHit(const MpeNote& note, juce::Point<float> pos) const
{
    const double b = beatForX(pos.x);
    if (b < note.startBeat - 0.02 || b > note.endBeat() + 0.02)
        return false;
    const float ry = yForPitch((float) note.pitch + note.bendOffsetAt(b - note.startBeat));
    return std::abs(pos.y - ry) <= rowHeight * 0.5f;
}

bool PianoRollComponent::nearRightEdge(const MpeNote& note, juce::Point<float> pos) const
{
    const float ex = xForBeat(note.endBeat());
    if (pos.x < ex - 7.0f || pos.x > ex + 4.0f)
        return false;
    const float ey = yForPitch((float) note.pitch + note.bend.sample(note.lengthBeats));
    return std::abs(pos.y - ey) <= rowHeight * 1.5f;
}

bool PianoRollComponent::nearLeftEdge(const MpeNote& note, juce::Point<float> pos) const
{
    const float ex = xForBeat(note.startBeat);
    if (pos.x < ex - 4.0f || pos.x > ex + 7.0f)
        return false;
    const float ey = yForPitch((float) note.pitch + note.bend.getPoints().front().value);
    return std::abs(pos.y - ey) <= rowHeight * 1.5f;
}

PianoRollComponent::HandleHit PianoRollComponent::handleIndexAt(const MpeNote& note, juce::Point<float> pos) const
{
    const auto& pts = note.bend.getPoints();
    const float hitR = pointRadius + 3.0f;
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        auto& p = pts[(size_t) i];
        if (p.hasIn)
        {
            auto x = xForBeat(note.startBeat + juce::jlimit(0.0, note.lengthBeats, p.inBeat));
            auto y = yForPitch((float) note.pitch + p.inValue);
            if (pos.getDistanceFrom({ x, y }) <= hitR)
                return { i, false };
        }
        if (p.hasOut)
        {
            auto x = xForBeat(note.startBeat + juce::jlimit(0.0, note.lengthBeats, p.outBeat));
            auto y = yForPitch((float) note.pitch + p.outValue);
            if (pos.getDistanceFrom({ x, y }) <= hitR)
                return { i, true };
        }
    }
    return {};
}

const MpeNote* PianoRollComponent::findNote(const std::vector<MpeNote>& snap, const juce::Uuid& id) const
{
    for (auto& n : snap)
        if (n.id == id)
            return &n;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Curve splitting - shared by the cut tool and left-edge resize/trim
// ---------------------------------------------------------------------------

namespace
{
    // Keeps everything before `cutBeat`, ending the curve there at the original's
    // sampled (interpolated) value - the left half of a cut.
    ExpressionCurve curveKeepBefore(const ExpressionCurve& orig, double cutBeat)
    {
        ExpressionCurve nc(orig.defaultValue());
        nc.clearAndReset();
        for (auto& p : orig.getPoints())
            if (p.beat < cutBeat - 1.0e-6)
                nc.setPoint(p.beat, p.value, p.shaper);
        nc.setPoint(cutBeat, orig.sample(cutBeat), false);
        return nc;
    }

    // Keeps everything from `cutBeat` onward, re-based so cutBeat becomes beat 0 -
    // the right half of a cut, and what dragging a note's left edge inward (trim)
    // reduces to.
    ExpressionCurve curveRebaseFrom(const ExpressionCurve& orig, double cutBeat)
    {
        ExpressionCurve nc(orig.defaultValue());
        nc.clearAndReset();
        nc.setPoint(0.0, orig.sample(cutBeat), false);
        for (auto& p : orig.getPoints())
            if (p.beat > cutBeat + 1.0e-6)
                nc.setPoint(p.beat - cutBeat, p.value, p.shaper);
        return nc;
    }

    // Extends the curve on the left by `amount` beats, holding the original's
    // starting value flat before it now begins - what dragging a note's left edge
    // outward (extend) reduces to.
    ExpressionCurve curveExtendLeft(const ExpressionCurve& orig, double amount)
    {
        ExpressionCurve nc(orig.defaultValue());
        nc.clearAndReset();
        nc.setPoint(0.0, orig.getPoints().front().value, false);
        for (auto& p : orig.getPoints())
            nc.setPoint(p.beat + amount, p.value, p.shaper);
        return nc;
    }
}

// ---------------------------------------------------------------------------
//  Shape editor overlay
// ---------------------------------------------------------------------------

namespace
{
    constexpr float shapeAmpMax = 7.0f;   // +/- 7 semitones per knob

    // cycle-count range for a note whose bend points are `spanBeats` apart:
    // 1 cycle over the whole span up to a cycle every 1/16 note.
    float cycMaxFor(double spanBeats) { return (float) juce::jmax(2.0, spanBeats * 4.0); }

    float squeezeToNorm(float skew)   // skew 0.25..4  ->  0..1
    {
        return juce::jlimit(0.0f, 1.0f, 0.5f + 0.5f * std::log(juce::jlimit(0.25f, 4.0f, skew)) / std::log(4.0f));
    }
    float normToSqueeze(float n)
    {
        return std::pow(4.0f, (juce::jlimit(0.0f, 1.0f, n) - 0.5f) * 2.0f);
    }

    // Bulk multi-select shape shortcuts: replace with (or retarget every
    // existing section to) one shape. Individual sections are edited one at a
    // time via shapeUIFor/setActiveSectionShape instead - see below.
    void applyShape(MpeNote& n, BendShape shp)
    {
        if (shp == BendShape::straight)
        {
            n.waveSections.clear();
            return;
        }
        if (n.waveSections.empty())
        {
            WaveSection ws;
            ws.fromBeat = 0.0;
            ws.toBeat = n.lengthBeats;
            ws.shape = shp;
            n.waveSections.push_back(ws);
        }
        else
        {
            for (auto& ws : n.waveSections)
                ws.shape = shp;
        }
    }
}

int PianoRollComponent::effectiveShapeSectionIndex(const MpeNote& note) const
{
    if (shapeEditSectionIndex >= 0 && shapeEditSectionIndex < (int) note.waveSections.size())
        return shapeEditSectionIndex;
    return note.waveSections.empty() ? -1 : 0;
}

PianoRollComponent::ShapeUI PianoRollComponent::shapeUIFor(const MpeNote& note) const
{
    ShapeUI s;
    const auto& pts = note.bend.getPoints();
    if (pts.size() < 2)
        return s;

    const int effIdx = effectiveShapeSectionIndex(note);
    const bool hasSection = effIdx >= 0 && effIdx < (int) note.waveSections.size();
    const WaveSection* ws = hasSection ? &note.waveSections[(size_t) effIdx] : nullptr;

    s.valid = true;
    s.noteId = note.id;
    const double fromBeat = ws != nullptr ? ws->fromBeat : note.bend.firstBeat();
    const double toBeat   = ws != nullptr ? juce::jmax(fromBeat + 1.0e-6, ws->toBeat)
                                          : juce::jmax(note.bend.lastBeat(), note.bend.firstBeat() + 1.0e-6);
    s.x0 = xForBeat(note.startBeat + fromBeat);
    s.y0 = yForPitch((float) note.pitch + note.bend.sample(fromBeat));
    s.x1 = xForBeat(note.startBeat + toBeat);
    s.y1 = yForPitch((float) note.pitch + note.bend.sample(toBeat));
    s.wave = ws != nullptr;
    s.currentShape = ws != nullptr ? ws->shape : BendShape::straight;

    const float midX = 0.5f * (s.x0 + s.x1);

    // Place the controls off the wave's *envelope* - chord extent +/- the larger
    // amplitude - not the live sampled curve, so dragging cycles/skew (which only
    // change the wave inside a fixed envelope) doesn't make the sliders jump.
    // Only bend points inside the section's own span count toward that extent.
    float chordLo = 1.0e9f, chordHi = -1.0e9f;
    for (auto& p : pts)
        if (p.beat >= fromBeat - 1.0e-6 && p.beat <= toBeat + 1.0e-6)
            { chordLo = juce::jmin(chordLo, p.value); chordHi = juce::jmax(chordHi, p.value); }
    if (chordLo > chordHi)
    {
        chordLo = juce::jmin(note.bend.sample(fromBeat), note.bend.sample(toBeat));
        chordHi = juce::jmax(note.bend.sample(fromBeat), note.bend.sample(toBeat));
    }
    const float maxAmp = ws != nullptr ? juce::jmax(std::abs(ws->ampStart), std::abs(ws->ampEnd)) + 0.5f : 0.5f;
    const float drawnBot = s.wave ? yForPitch((float) note.pitch + chordLo - maxAmp) : juce::jmax(s.y0, s.y1);
    const float drawnTop = s.wave ? yForPitch((float) note.pitch + chordHi + maxAmp) : juce::jmin(s.y0, s.y1);

    s.wheel  = { midX, drawnBot + (s.wave ? 40.0f : 24.0f) };
    s.wheelR = 15.0f;

    if (s.wave)
    {
        s.cyclesTrack  = { s.x0, drawnBot + 8.0f,  s.x1 - s.x0, 6.0f };
        s.squeezeTrack = { s.x0, drawnTop - 16.0f, s.x1 - s.x0, 6.0f };

        const double spanB = toBeat - fromBeat;
        const float cycMax = cycMaxFor(spanB);
        const float cyN = juce::jlimit(0.0f, 1.0f, (ws->cycleCount() - 1.0f) / juce::jmax(1.0f, cycMax - 1.0f));
        s.cyclesH  = { s.x0 + cyN * (s.x1 - s.x0), s.cyclesTrack.getCentreY() };
        s.squeezeH = { s.x0 + squeezeToNorm(ws->skew) * (s.x1 - s.x0), s.squeezeTrack.getCentreY() };

        // keep the left knob clear of the frozen keyboard column (which overlays the
        // roll's left edge) so it's never hidden behind the keys, whatever the scroll
        float leftEdge = (float) keyboardWidth;
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
            leftEdge += (float) vp->getViewPositionX();
        s.ampStartKnob = { juce::jmax(s.x0 - 16.0f, leftEdge + s.knobR + 3.0f), s.y0 };
        s.ampEndKnob   = { s.x1 + 16.0f, s.y1 };

        s.ampStart = ws->ampStart;
        s.ampEnd   = ws->ampEnd;
        s.cycles   = ws->cycleCount();
        s.skew     = ws->skew;
    }
    return s;
}

// A plain straight/triangle/sine wheel (no cycles/squeeze/amp controls - those
// don't make sense across a mix of notes) centred under the whole selection.
PianoRollComponent::ShapeUI PianoRollComponent::multiShapeWheelFor(const std::vector<MpeNote>& snapshot) const
{
    ShapeUI s;
    float xLo = 1.0e9f, xHi = -1.0e9f, yLo = 1.0e9f, yHi = -1.0e9f;
    bool any = false;

    for (auto& n : snapshot)
    {
        if (! isSelected(n.id))
            continue;
        any = true;
        xLo = juce::jmin(xLo, xForBeat(n.startBeat));
        xHi = juce::jmax(xHi, xForBeat(n.endBeat()));
        auto ext = pitchExtent(n);
        yLo = juce::jmin(yLo, yForPitch(ext.getEnd()));     // highest pitch -> smallest y
        yHi = juce::jmax(yHi, yForPitch(ext.getStart()));   // lowest pitch -> largest y
    }
    if (! any)
        return s;

    s.valid = true;
    s.wheel = { 0.5f * (xLo + xHi), yHi + 28.0f };
    s.wheelR = 15.0f;
    return s;
}

void PianoRollComponent::drawShapeWheel(juce::Graphics& g, const ShapeUI& s, const BendShape* highlight) const
{
    const float twoPi = juce::MathConstants<float>::twoPi;
    const float pi    = juce::MathConstants<float>::pi;

    // --- 3-slice shape wheel (STR top, TRI lower-right, SIN lower-left) ---
    auto sliceGlyph = [&](float midAng, BendShape which)
    {
        const float gx = s.wheel.x + std::sin(midAng) * s.wheelR * 0.55f;
        const float gy = s.wheel.y - std::cos(midAng) * s.wheelR * 0.55f;
        juce::Path gp;
        const float r = 5.0f;
        if (which == BendShape::straight)
        {
            gp.startNewSubPath(gx - r, gy); gp.lineTo(gx + r, gy);
        }
        else if (which == BendShape::triangle)
        {
            gp.startNewSubPath(gx - r, gy + r * 0.6f);
            gp.lineTo(gx, gy - r * 0.8f);
            gp.lineTo(gx + r, gy + r * 0.6f);
        }
        else
        {
            gp.startNewSubPath(gx - r, gy);
            for (int i = 1; i <= 12; ++i)
            {
                const float t = (float) i / 12.0f;
                gp.lineTo(gx - r + 2.0f * r * t, gy - std::sin(t * twoPi) * r * 0.8f);
            }
        }
        g.setColour(Theme::text.withAlpha(0.85f));
        g.strokePath(gp, juce::PathStrokeType(1.3f));
    };

    const float ranges[3][2] = { { -pi / 3.0f, pi / 3.0f }, { pi / 3.0f, pi }, { -pi, -pi / 3.0f } };
    const BendShape slices[3] = { BendShape::straight, BendShape::triangle, BendShape::sine };
    for (int i = 0; i < 3; ++i)
    {
        juce::Path wedge;
        wedge.addPieSegment(s.wheel.x - s.wheelR, s.wheel.y - s.wheelR, s.wheelR * 2.0f, s.wheelR * 2.0f,
                            ranges[i][0], ranges[i][1], 0.0f);
        g.setColour(highlight != nullptr && *highlight == slices[i] ? Theme::accent.withAlpha(0.22f) : Theme::wheelSlice);
        g.fillPath(wedge);
        g.setColour(Theme::separator);
        g.strokePath(wedge, juce::PathStrokeType(1.0f));
        sliceGlyph(0.5f * (ranges[i][0] + ranges[i][1]), slices[i]);
    }
    g.setColour(Theme::separator);
    g.drawEllipse(s.wheel.x - s.wheelR, s.wheel.y - s.wheelR, s.wheelR * 2.0f, s.wheelR * 2.0f, 1.0f);
}

int PianoRollComponent::wheelSliceAt(const ShapeUI& s, juce::Point<float> pos) const
{
    const auto d = pos - s.wheel;
    if (d.x * d.x + d.y * d.y > s.wheelR * s.wheelR)
        return -1;
    // angle measured from 12 o'clock, clockwise
    float a = std::atan2(d.x, -d.y);
    if (a >= -juce::MathConstants<float>::pi / 3.0f && a < juce::MathConstants<float>::pi / 3.0f) return 0; // STR (top)
    if (a >= juce::MathConstants<float>::pi / 3.0f) return 1;                                               // TRI (lower right)
    return 2;                                                                                              // SIN (lower left)
}

void PianoRollComponent::setActiveSectionShape(const juce::Uuid& id, BendShape shp)
{
    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id != id)
                continue;

            const int effIdx = effectiveShapeSectionIndex(n);
            if (shp == BendShape::straight)
            {
                if (effIdx >= 0)
                {
                    n.waveSections.erase(n.waveSections.begin() + effIdx);
                    shapeEditSectionIndex = -1;
                }
            }
            else if (effIdx >= 0)
            {
                n.waveSections[(size_t) effIdx].shape = shp;
            }
            else
            {
                WaveSection ws;
                ws.fromBeat = 0.0;
                ws.toBeat = n.lengthBeats;
                ws.shape = shp;
                n.waveSections.push_back(ws);
                shapeEditSectionIndex = (int) n.waveSections.size() - 1;
            }
            break;
        }
    });
    repaint();
}

void PianoRollComponent::setShapeForSelection(BendShape shp)
{
    if (selection.empty())
        return;
    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (isSelected(n.id)) applyShape(n, shp);
    });
    repaint();
}

void PianoRollComponent::pickShapeRangePoint(const juce::Uuid& noteId, int pointIndex)
{
    if (rangeAnchorIndex < 0 || rangeAnchorNoteId != noteId)
    {
        rangeAnchorNoteId = noteId;
        rangeAnchorIndex = pointIndex;
        return;
    }

    if (rangeAnchorIndex == pointIndex)   // clicked the same point again - cancel
    {
        rangeAnchorIndex = -1;
        return;
    }

    const int anchorIdx = rangeAnchorIndex;
    rangeAnchorIndex = -1;

    double lo = 0.0, hi = 0.0;
    bool haveRange = false;
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id != noteId)
                continue;
            const auto& pts = n.bend.getPoints();
            if (anchorIdx < (int) pts.size() && pointIndex < (int) pts.size())
            {
                const double a = pts[(size_t) anchorIdx].beat;
                const double b = pts[(size_t) pointIndex].beat;
                lo = juce::jmin(a, b);
                hi = juce::jmax(a, b);
                haveRange = hi - lo >= 1.0e-6;
            }
            break;
        }
    });
    if (! haveRange)
        return;

    // if this overlaps an existing section, just select that one for editing
    // instead of creating a duplicate/overlapping one
    bool foundExisting = false;
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id != noteId)
                continue;
            for (int i = 0; i < (int) n.waveSections.size(); ++i)
            {
                auto& ws = n.waveSections[(size_t) i];
                if (lo < ws.toBeat - 1.0e-6 && hi > ws.fromBeat + 1.0e-6)
                {
                    shapeEditSectionIndex = i;
                    foundExisting = true;
                    return;
                }
            }
            break;
        }
    });
    if (foundExisting)
    {
        repaint();
        return;
    }

    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id != noteId)
                continue;
            WaveSection ws;
            ws.fromBeat = lo;
            ws.toBeat = hi;
            ws.shape = BendShape::sine;
            n.waveSections.push_back(ws);
            shapeEditSectionIndex = (int) n.waveSections.size() - 1;
            break;
        }
    });
    repaint();
}

// ---------------------------------------------------------------------------
//  Selection
// ---------------------------------------------------------------------------

bool PianoRollComponent::isSelected(const juce::Uuid& id) const
{
    return std::find(selection.begin(), selection.end(), id) != selection.end();
}

void PianoRollComponent::selectOnly(const juce::Uuid& id)
{
    selection.clear();
    selection.push_back(id);
}

void PianoRollComponent::toggleSelected(const juce::Uuid& id)
{
    auto it = std::find(selection.begin(), selection.end(), id);
    if (it != selection.end()) selection.erase(it);
    else                       selection.push_back(id);
}

void PianoRollComponent::clearSelection() { selection.clear(); rangeAnchorIndex = -1; }

juce::Uuid PianoRollComponent::soleSelection() const
{
    return selection.size() == 1 ? selection.front() : juce::Uuid::null();
}

void PianoRollComponent::deleteSelected()
{
    if (selection.empty())
        return;
    editNotes([&](std::vector<MpeNote>& notes)
    {
        notes.erase(std::remove_if(notes.begin(), notes.end(),
            [&](const MpeNote& n) { return isSelected(n.id); }), notes.end());
    });
    selection.clear();
    repaint();
}

void PianoRollComponent::eraseNoteAt(juce::Point<float> pos)
{
    juce::Uuid hit;
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto it = notes.rbegin(); it != notes.rend(); ++it)
            if (ribbonHit(*it, pos)) { hit = it->id; break; }
    });
    if (hit.isNull())
        return;

    editNotes([&](std::vector<MpeNote>& notes)
    {
        notes.erase(std::remove_if(notes.begin(), notes.end(),
            [&](const MpeNote& m) { return m.id == hit; }), notes.end());
    });
    auto sit = std::find(selection.begin(), selection.end(), hit);
    if (sit != selection.end()) selection.erase(sit);
}

void PianoRollComponent::cutNoteAt(juce::Point<float> pos)
{
    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    for (auto it = snap.rbegin(); it != snap.rend(); ++it)
    {
        const auto& n = *it;
        if (! ribbonHit(n, pos) || n.lengthBeats < 0.125)
            continue;

        const double splitBeat = juce::jlimit(0.0625, n.lengthBeats - 0.0625, beatForX(pos.x) - n.startBeat);

        MpeNote left = n;
        left.bend = curveKeepBefore(n.bend, splitBeat);
        left.lengthBeats = left.bend.conformEnd(splitBeat);
        left.waveSections.clear();   // wouldn't necessarily still make sense split across two notes

        MpeNote right = n;
        right.id = juce::Uuid();
        right.startBeat = n.startBeat + splitBeat;
        right.isSounding = false;
        right.assignedChannel = -1;
        right.bend = curveRebaseFrom(n.bend, splitBeat);
        right.lengthBeats = right.bend.conformEnd(n.lengthBeats - splitBeat);
        right.waveSections.clear();

        editNotes([&](std::vector<MpeNote>& notes)
        {
            for (auto& m : notes)
                if (m.id == n.id) { m = left; break; }
            notes.push_back(right);
        });
        selection = { left.id, right.id };
        repaint();
        return;
    }
}

void PianoRollComponent::reverseSelection()
{
    if (selection.empty())
        return;

    double lo = 1.0e18, hi = -1.0e18;
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (isSelected(n.id)) { lo = juce::jmin(lo, n.startBeat); hi = juce::jmax(hi, n.endBeat()); }
    });
    if (hi <= lo)
        return;

    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (! isSelected(n.id))
                continue;

            // mirror this note's position within the selection's overall time span
            const double newStart = lo + hi - n.endBeat();

            // and reverse its own bend curve in time, so the pitch shape plays backwards
            ExpressionCurve nc(n.bend.defaultValue());
            nc.clearAndReset();
            for (auto& p : n.bend.getPoints())
                nc.setPoint(n.lengthBeats - p.beat, p.value, p.shaper);
            n.bend = nc;
            n.lengthBeats = n.bend.conformEnd(n.lengthBeats);
            n.startBeat = newStart;

            // mirror each wave section's position too, swapping start/end amplitude
            // since time direction flipped
            for (auto& ws : n.waveSections)
            {
                const double newFrom = n.lengthBeats - ws.toBeat;
                const double newTo   = n.lengthBeats - ws.fromBeat;
                ws.fromBeat = newFrom;
                ws.toBeat   = newTo;
                std::swap(ws.ampStart, ws.ampEnd);
            }
        }
    });
    repaint();
}

void PianoRollComponent::toggleMuteSelected()
{
    if (selection.empty())
        return;
    bool anyUnmuted = false;
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (isSelected(n.id) && ! n.muted) anyUnmuted = true;
    });
    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (isSelected(n.id)) n.muted = anyUnmuted;   // if any is live, mute all; else unmute all
    });
    repaint();
}

void PianoRollComponent::copySelection()
{
    clipboard.clear();
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (isSelected(n.id)) clipboard.push_back(n);
    });
    if (clipboard.empty())
        return;

    // normalise so the earliest note sits at beat 0
    double minBeat = 1.0e9;
    for (auto& n : clipboard) minBeat = juce::jmin(minBeat, n.startBeat);
    for (auto& n : clipboard)
    {
        n.startBeat -= minBeat;
        n.isSounding = false;
        n.assignedChannel = -1;
    }
}

void PianoRollComponent::duplicateSelectionAfter()
{
    if (selection.empty())
        return;

    double lo = 1.0e18, hi = -1.0e18;
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (isSelected(n.id)) { lo = juce::jmin(lo, n.startBeat); hi = juce::jmax(hi, n.endBeat()); }
    });
    if (hi <= lo)
        return;
    const double shift = hi - lo;   // whole block continues right after itself

    std::vector<juce::Uuid> copies;
    editNotes([&](std::vector<MpeNote>& notes)
    {
        std::vector<MpeNote> add;
        for (auto& n : notes)
            if (isSelected(n.id))
            {
                MpeNote c = n;
                c.id = juce::Uuid();
                c.startBeat = n.startBeat + shift;
                c.isSounding = false;
                c.assignedChannel = -1;
                copies.push_back(c.id);
                add.push_back(std::move(c));
            }
        for (auto& c : add) notes.push_back(std::move(c));
    });
    selection = copies;
    repaint();
}

void PianoRollComponent::nudgeSelectionPitch(int semitones)
{
    if (selection.empty() || semitones == 0)
        return;

    // With scale-snap on, a single step moves to the next scale degree (FL-style);
    // whole octaves always move by 12 (which keeps the scale degree).
    const bool byDegree = processor.getSnapToScale()
                       && processor.getScaleType() != Scale::chromatic
                       && std::abs(semitones) < 12;
    const int  dir = semitones > 0 ? 1 : -1;
    const int  st  = processor.getScaleType();
    const int  rt  = processor.getScaleRoot();

    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (isSelected(n.id))
            {
                int p = n.pitch;
                if (byDegree)
                {
                    for (int i = 1; i <= 12; ++i)
                        if (Scale::contains(st, rt, p + dir * i)) { p += dir * i; break; }
                }
                else
                {
                    p += semitones;
                }
                n.pitch = juce::jlimit(lowestPitch, highestPitch, p);
            }
    });
    repaint();
}

void PianoRollComponent::nudgeAllPitch(int semitones)
{
    if (semitones == 0)
        return;
    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            n.pitch = juce::jlimit(lowestPitch, highestPitch, n.pitch + semitones);
    });
    repaint();
}

void PianoRollComponent::selectAll()
{
    selection.clear();
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            selection.push_back(n.id);
    });
    repaint();
}

void PianoRollComponent::pasteClipboard()
{
    if (clipboard.empty())
        return;

    // anchor the block's start beat to the mouse (snapped), keep relative pitches,
    // shift so the top clipboard note lands on the hovered row
    const double anchorBeat = snapBeat(juce::jmax(0.0, hoverBeat), false);
    int topPitch = -1000;
    for (auto& n : clipboard) topPitch = juce::jmax(topPitch, n.pitch);
    const int dPitch = juce::jlimit(lowestPitch, highestPitch, (int) std::round(hoverPitch)) - topPitch;

    std::vector<juce::Uuid> pasted;
    editNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto src : clipboard)
        {
            src.id = juce::Uuid();
            src.startBeat = anchorBeat + src.startBeat;
            src.pitch = maybeSnapPitch(juce::jlimit(lowestPitch, highestPitch, src.pitch + dPitch));
            src.isSounding = false;
            src.assignedChannel = -1;
            pasted.push_back(src.id);
            notes.push_back(std::move(src));
        }
    });
    selection = pasted;
    repaint();
}

void PianoRollComponent::beginMoveNotes(juce::Point<float> pos, const std::vector<MpeNote>& snapshot,
                                        const juce::Uuid& leadNoteId)
{
    dragMode = DragMode::moveNotes;
    dragAnchorBeat = beatForX(pos.x);
    dragAnchorPitch = pitchForY(pos.y);
    dragOrigins.clear();
    for (auto& n : snapshot)
        if (isSelected(n.id))
            dragOrigins.push_back({ n.id, n.startBeat, n.pitch });
    dragLeadNoteId = leadNoteId;
}

void PianoRollComponent::updateMarquee(juce::Point<float> pos, const std::vector<MpeNote>& snapshot, bool additive)
{
    marqueeB = pos;

    const float x0 = juce::jmin(marqueeA.x, marqueeB.x), x1 = juce::jmax(marqueeA.x, marqueeB.x);
    const float y0 = juce::jmin(marqueeA.y, marqueeB.y), y1 = juce::jmax(marqueeA.y, marqueeB.y);
    const double bLo = beatForX(x0), bHi = beatForX(x1);
    const float  pLo = pitchForY(y1), pHi = pitchForY(y0);

    selection = additive ? preMarqueeSelection : std::vector<juce::Uuid>{};

    for (auto& n : snapshot)
    {
        if (n.endBeat() < bLo || n.startBeat > bHi)
            continue;
        auto ext = pitchExtent(n);
        if (ext.getEnd() < pLo || ext.getStart() > pHi)
            continue;
        if (! isSelected(n.id))
            selection.push_back(n.id);
    }
}

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panel);

    const auto loopLen = processor.getLoopLengthBeats();
    const float h = (float) getHeight();

    // note-area rows only (the keyboard column is a separate, non-scrolling overlay)
    const int   scaleType = processor.getScaleType();
    const int   scaleRoot = processor.getScaleRoot();
    const bool  scaleOn   = scaleType != Scale::chromatic;
    const float rowW = (float) getWidth() - keyboardWidth;

    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        auto yTop = (float) (highestPitch - pitch) * rowHeight;

        // With a scale active, "in scale" rows are the bright ones and "out of
        // scale" rows are the dark ones (instead of the usual white/black-key look).
        const bool dark = scaleOn ? ! Scale::contains(scaleType, scaleRoot, pitch)
                                  : isBlackKey(pitch);
        g.setColour(dark ? Theme::rowDark : Theme::rowLight);
        g.fillRect((float) keyboardWidth, yTop, rowW, (float) rowHeight);

        if (pitch % 12 == 0)
        {
            g.setColour(Theme::gridLine.withAlpha(0.12f));
            g.drawHorizontalLine((int) yTop, (float) keyboardWidth, (float) getWidth());
        }
    }

    auto verticals = [&](double step, juce::Colour c)
    {
        g.setColour(c);
        for (double b = step; b <= loopLen + 1.0e-6; b += step)
            g.drawVerticalLine((int) xForBeat(b), 0.0f, h);
    };
    if (pixelsPerBeat * 0.25f >= 5.0f) verticals(0.25, Theme::gridLine.withAlpha(0.05f));
    if (pixelsPerBeat        >= 5.0f) verticals(1.0,  Theme::gridLine.withAlpha(0.10f));
    verticals(4.0, Theme::gridLine.withAlpha(0.18f));

    g.setColour(Theme::accent.withAlpha(0.4f));
    g.drawVerticalLine((int) xForBeat(loopLen), 0.0f, h);

    std::vector<MpeNote> snapshot;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snapshot = notes; });

    auto drawNote = [&](const MpeNote& n, bool selected)
    {
        juce::Path p;
        buildNotePath(n, p);

        const juce::Colour base = n.isSounding ? Theme::noteHot
                                : selected       ? Theme::noteSel
                                                 : Theme::note;

        constexpr auto joint = juce::PathStrokeType::curved;
        constexpr auto cap   = juce::PathStrokeType::butt;

        // ribbon thickness follows the zoom (row height), so it never swamps the
        // grid when zoomed right out - kept on the thin side so adjacent notes at
        // neighbouring pitches don't crowd each other's click area
        const float core = juce::jlimit(1.5f, 8.0f, rowHeight * 0.38f) + (selected ? 1.5f : 0.0f);

        if (n.isSounding)
        {
            g.setColour(Theme::noteHot.withAlpha(0.28f));
            g.strokePath(p, juce::PathStrokeType(core + 8.0f, joint, cap));
        }
        g.setColour(Theme::gridLine.withAlpha(0.10f));
        g.strokePath(p, juce::PathStrokeType(core + 2.5f, joint, cap));

        if (n.muted)
        {
            // hollow ribbon: outline only, dimmed
            g.setColour(base.withAlpha(0.45f));
            g.strokePath(p, juce::PathStrokeType(core, joint, cap));
            g.setColour(Theme::panel.withAlpha(0.65f));
            g.strokePath(p, juce::PathStrokeType(juce::jmax(1.0f, core - 3.0f), joint, cap));
        }
        else
        {
            g.setColour(base.withAlpha(selected || n.isSounding ? 1.0f : 0.9f));
            g.strokePath(p, juce::PathStrokeType(core, joint, cap));
        }

        // note name printed on top of the note at its start, FL-style; follows the key.
        // Named for the row the ribbon actually starts on (base pitch + the first
        // bend point's own value, which can be nonzero if its start was shaped) -
        // not the raw base pitch, or the label can name a different key than the
        // one the note visibly sits on.
        if (rowHeight >= 8.0f && n.lengthBeats * pixelsPerBeat >= 16.0f)
        {
            static const char* kPc[12] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                           "G", "G#", "A", "A#", "B" };
            const int startPitch = juce::roundToInt((float) n.pitch + n.bend.getPoints().front().value);
            const juce::String nm = juce::String(kPc[((startPitch % 12) + 12) % 12])
                                  + juce::String(startPitch / 12 - 1);
            const float sx = xForBeat(n.startBeat);
            const float sy = yForPitch((float) n.pitch + n.bend.getPoints().front().value);
            g.setFont(juce::jlimit(8.0f, 11.0f, rowHeight * 0.6f));
            juce::Rectangle<float> tb(sx + 4.0f, sy - 7.0f,
                                      juce::jmin(46.0f, (float) n.lengthBeats * pixelsPerBeat - 6.0f), 14.0f);
            g.setColour((n.muted ? base.withAlpha(0.5f) : base).contrasting(0.9f));
            g.drawText(nm, tb, juce::Justification::centredLeft);
        }
    };

    for (auto& n : snapshot) if (! isSelected(n.id)) drawNote(n, false);
    for (auto& n : snapshot) if (isSelected(n.id))   drawNote(n, true);

    const auto sole = soleSelection();

    // bend-point handles + shape overlay for the sole selected note (draw tool)
    if (tool == Tool::draw && sole != juce::Uuid::null())
    {
        if (auto* n = findNote(snapshot, sole))
        {
            const auto& pts = n->bend.getPoints();
            for (int pi = 0; pi < (int) pts.size(); ++pi)
            {
                auto& pt = pts[(size_t) pi];
                auto x = xForBeat(n->startBeat + juce::jlimit(0.0, n->lengthBeats, pt.beat));
                auto y = yForPitch((float) n->pitch + pt.value);

                if (rangeAnchorNoteId == n->id && rangeAnchorIndex == pi)
                {
                    g.setColour(Theme::accent);
                    g.drawEllipse(x - pointRadius - 3.0f, y - pointRadius - 3.0f,
                                  (pointRadius + 3.0f) * 2.0f, (pointRadius + 3.0f) * 2.0f, 2.0f);
                }

                if (pt.shaper)
                {
                    // legacy Catmull-Rom diamond, from an older project - no longer
                    // created by the UI, kept only so it still renders recognisably
                    const float r = pointRadius - 0.3f;
                    juce::Path d;
                    d.addQuadrilateral(x, y - r, x + r, y, x, y + r, x - r, y);
                    g.setColour(Theme::handle);
                    g.fillPath(d);
                    g.setColour(Theme::handleEdge);
                    g.strokePath(d, juce::PathStrokeType(1.0f));
                }
                else
                {
                    g.setColour(Theme::handle);
                    g.fillEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
                    g.setColour(Theme::handleEdge);
                    g.drawEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f, 1.0f);
                }

                // Bezier tangent handles: a thin guide line from the point out to a
                // small diamond nub, draggable independently of the point itself.
                auto drawHandle = [&](double hBeat, float hValue)
                {
                    const float hx = xForBeat(n->startBeat + juce::jlimit(0.0, n->lengthBeats, hBeat));
                    const float hy = yForPitch((float) n->pitch + hValue);
                    g.setColour(Theme::accent.withAlpha(0.55f));
                    g.drawLine(x, y, hx, hy, 1.0f);
                    const float hr = pointRadius - 1.0f;
                    juce::Path d;
                    d.addQuadrilateral(hx, hy - hr, hx + hr, hy, hx, hy + hr, hx - hr, hy);
                    g.setColour(Theme::accent);
                    g.fillPath(d);
                    g.setColour(Theme::handleEdge);
                    g.strokePath(d, juce::PathStrokeType(1.0f));
                };
                if (pt.hasIn)  drawHandle(pt.inBeat, pt.inValue);
                if (pt.hasOut) drawHandle(pt.outBeat, pt.outValue);
            }

            auto s = shapeUIFor(*n);
            if (s.valid)
            {
                drawShapeWheel(g, s, &s.currentShape);

                if (s.wave)
                {
                    auto slider = [&](juce::Rectangle<float> r, juce::Point<float> handle)
                    {
                        g.setColour(Theme::separator);
                        g.fillRect(r.withSizeKeepingCentre(r.getWidth(), 3.0f));
                        g.setColour(Theme::sliderThumb);
                        g.fillEllipse(handle.x - 4.5f, handle.y - 4.5f, 9.0f, 9.0f);
                        g.setColour(Theme::accent);
                        g.drawEllipse(handle.x - 4.5f, handle.y - 4.5f, 9.0f, 9.0f, 1.4f);
                    };
                    slider(s.cyclesTrack,  s.cyclesH);
                    slider(s.squeezeTrack, s.squeezeH);

                    auto knob = [&](juce::Point<float> c, float value)   // value -12..+12
                    {
                        g.setColour(Theme::sliderThumb);
                        g.fillEllipse(c.x - s.knobR, c.y - s.knobR, s.knobR * 2.0f, s.knobR * 2.0f);
                        g.setColour(Theme::separator);
                        g.drawEllipse(c.x - s.knobR, c.y - s.knobR, s.knobR * 2.0f, s.knobR * 2.0f, 1.0f);
                        const float ang = juce::jmap(juce::jlimit(-shapeAmpMax, shapeAmpMax, value),
                                                    -shapeAmpMax, shapeAmpMax, -2.4f, 2.4f);
                        g.setColour(Theme::accent);
                        g.drawLine(c.x, c.y, c.x + std::sin(ang) * (s.knobR - 2.0f),
                                             c.y - std::cos(ang) * (s.knobR - 2.0f), 2.0f);
                    };
                    knob(s.ampStartKnob, s.ampStart);
                    knob(s.ampEndKnob,   s.ampEnd);

                    juce::String rd;
                    if (dragMode == DragMode::shapeCycles)
                        rd = juce::String(s.cycles, 1) + " cyc";
                    else if (dragMode == DragMode::shapeSqueeze)   rd = "skew " + juce::String(s.skew, 2);
                    else if (dragMode == DragMode::shapeAmpStart)  rd = "amp0 " + juce::String(s.ampStart, 2);
                    else if (dragMode == DragMode::shapeAmpEnd)    rd = "amp1 " + juce::String(s.ampEnd, 2);
                    if (rd.isNotEmpty())
                    {
                        juce::Rectangle<float> box(0.5f * (s.x0 + s.x1) - 42.0f, s.squeezeTrack.getY() - 20.0f, 84.0f, 16.0f);
                        g.setColour(Theme::text);
                        g.fillRoundedRectangle(box, 3.0f);
                        g.setColour(Theme::panel);
                        g.setFont(11.0f);
                        g.drawText(rd, box, juce::Justification::centred);
                    }
                }
            }
        }
    }
    else if (selection.size() > 1)
    {
        for (auto& n : snapshot)
        {
            if (! isSelected(n.id))
                continue;
            const int li = n.bend.lastIndex();
            if (li <= 0)
                continue;
            auto x = xForBeat(n.endBeat());
            auto y = yForPitch((float) n.pitch + n.bend.getPoints()[(size_t) li].value);
            g.setColour(Theme::handle);
            g.fillEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
            g.setColour(Theme::handleEdge);
            g.drawEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f, 1.0f);
        }

        // one wheel under the whole selection - sets every selected note's shape
        // at once, so you don't have to dial in straight/triangle/sine per note
        auto mw = multiShapeWheelFor(snapshot);
        if (mw.valid)
            drawShapeWheel(g, mw, nullptr);
    }

    if (dragMode == DragMode::marquee)
    {
        juce::Rectangle<float> r(juce::Point<float>(juce::jmin(marqueeA.x, marqueeB.x), juce::jmin(marqueeA.y, marqueeB.y)),
                                 juce::Point<float>(juce::jmax(marqueeA.x, marqueeB.x), juce::jmax(marqueeA.y, marqueeB.y)));
        g.setColour(Theme::accent.withAlpha(0.12f));
        g.fillRect(r);
        g.setColour(Theme::accent.withAlpha(0.9f));
        g.drawRect(r, 1.0f);
    }

    if (processor.getUiIsPlaying())
    {
        auto x = xForBeat(processor.getUiPlayheadBeat());
        g.setColour(Theme::accent.withAlpha(0.85f));
        g.drawVerticalLine((int) x, 0.0f, h);
    }

    // faint "prodcoldie" watermark, pinned to the visible area (doesn't scroll)
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const float vx = (float) vp->getViewPositionX();
        const float vy = (float) vp->getViewPositionY();
        const float vw = (float) vp->getWidth();
        const float vh = (float) vp->getHeight();
        const float sb = (float) vp->getScrollBarThickness();
        g.setColour(Theme::text.withAlpha(0.06f));
        g.setFont(juce::jlimit(15.0f, 34.0f, vw / 14.0f));
        g.drawText("prodcoldie",
                   juce::Rectangle<float>(vx + keyboardWidth + 12.0f, vy + vh - sb - 40.0f,
                                          vw - keyboardWidth - 24.0f - sb, 32.0f),
                   juce::Justification::bottomRight);
    }
}

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // middle-button drag = pan the roll (like a scroll-wheel push-drag)
    if (e.mods.isMiddleButtonDown())
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            panning = true;
            panStartView = { vp->getViewPositionX(), vp->getViewPositionY() };
            panStartMouseScreen = e.getScreenPosition().toFloat();
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        return;
    }

    const auto pos = e.position;
    if (pos.x < (float) keyboardWidth)
        return;

    beginGesture();

    const bool rightClick = e.mods.isRightButtonDown();
    const bool fine       = e.mods.isAltDown();
    const bool addPoint   = e.mods.isCommandDown() && ! rightClick;
    const bool springSel  = tool == Tool::draw && e.mods.isShiftDown() && ! rightClick && ! addPoint;
    const bool selectMode = tool == Tool::select || springSel;

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    const float range = (float) processor.getPitchBendRangeSemitones();

    // ---- right button: remove a bend point, else deselect + start erase-drag ----
    if (rightClick)
    {
        if (e.mods.isCommandDown())
        {
            zoomAllTheWayOut();
            return;
        }

        if (tool == Tool::draw)
            for (auto& n : snap)
                if (isSelected(n.id))
                {
                    const int idx = pointIndexAt(n, pos);
                    if (idx > 0 && idx < n.bend.lastIndex())
                    {
                        editNotes([&](std::vector<MpeNote>& notes)
                        {
                            for (auto& m : notes)
                                if (m.id == n.id) { m.bend.removePoint(idx); break; }
                        });
                        repaint();
                        return;
                    }
                }

        clearSelection();          // right-click deselects everything
        rightErasing = true;       // hold + drag to keep erasing
        eraseNoteAt(pos);
        repaint();
        return;
    }

    // ---- cut tool: click a note to slice it into two at that point ----
    if (tool == Tool::cut)
    {
        cutNoteAt(pos);
        return;
    }

    // ---- shape overlay (multi-selection): one wheel sets every selected note ----
    if (! rightClick && selection.size() > 1)
    {
        auto mw = multiShapeWheelFor(snap);
        if (mw.valid)
        {
            const int slice = wheelSliceAt(mw, pos);
            if (slice == 0) { setShapeForSelection(BendShape::straight); return; }
            if (slice == 1) { setShapeForSelection(BendShape::triangle); return; }
            if (slice == 2) { setShapeForSelection(BendShape::sine);     return; }
        }
    }

    // ---- shape overlay (sole selected note, draw tool) ----
    if (! rightClick && tool == Tool::draw && soleSelection() != juce::Uuid::null())
    {
        if (auto* n = findNote(snap, soleSelection()))
        {
            auto s = shapeUIFor(*n);
            if (s.valid)
            {
                const int slice = wheelSliceAt(s, pos);
                if (slice == 0) { setActiveSectionShape(n->id, BendShape::straight); return; }
                if (slice == 1) { setActiveSectionShape(n->id, BendShape::triangle); return; }
                if (slice == 2) { setActiveSectionShape(n->id, BendShape::sine);     return; }

                if (s.wave)
                {
                    auto grabH = [&](juce::Rectangle<float> track, juce::Point<float> handle, DragMode m) -> bool
                    {
                        if (pos.getDistanceFrom(handle) <= 9.0f
                            || track.expanded(0.0f, 7.0f).contains(pos))
                        {
                            dragMode = m;
                            dragNoteId = n->id;
                            repaint();
                            return true;
                        }
                        return false;
                    };
                    if (grabH(s.cyclesTrack,  s.cyclesH,  DragMode::shapeCycles))  return;
                    if (grabH(s.squeezeTrack, s.squeezeH, DragMode::shapeSqueeze)) return;

                    auto grabKnob = [&](juce::Point<float> c, DragMode m, float value) -> bool
                    {
                        if (pos.getDistanceFrom(c) <= s.knobR + 3.0f)
                        {
                            dragMode = m;
                            dragNoteId = n->id;
                            shapeGrabY = pos.y;
                            shapeGrabVal = value;
                            repaint();
                            return true;
                        }
                        return false;
                    };
                    if (grabKnob(s.ampStartKnob, DragMode::shapeAmpStart, s.ampStart)) return;
                    if (grabKnob(s.ampEndKnob,   DragMode::shapeAmpEnd,   s.ampEnd))   return;
                }
            }
        }
    }

    // ---- right-edge resize handle ----
    if (! rightClick && ! addPoint && ! e.mods.isShiftDown())
    {
        auto tryResize = [&](const MpeNote& n) -> bool
        {
            const int last = n.bend.lastIndex();
            if (last <= 0 || ! nearRightEdge(n, pos))
                return false;

            if (isSelected(n.id) && selection.size() > 1)
            {
                dragMode = DragMode::resizeEnds;
                dragAnchorBeat = beatForX(pos.x);
                dragAnchorPitch = pitchForY(pos.y);
                endOrigins.clear();
                for (auto& m : snap)
                {
                    if (! isSelected(m.id))
                        continue;
                    const int li = m.bend.lastIndex();
                    endOrigins.push_back({ m.id, m.lengthBeats,
                                           li > 0 ? m.bend.getPoints()[(size_t) li].value : 0.0f });
                }
            }
            else
            {
                selectOnly(n.id);
                dragMode = DragMode::movePoint;
                dragNoteId = n.id;
                dragPointIndex = last;
            }
            repaint();
            return true;
        };
        for (auto& n : snap) if (isSelected(n.id) && tryResize(n)) return;
        for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryResize(*it)) return;
    }

    // ---- Bezier tangent handles + bend points (draw tool). Both are only ever
    // drawn for the sole-selected note (see paint()), so hit-testing is
    // restricted to that same note - otherwise an invisible point or handle
    // belonging to some OTHER note in the roll (selected as part of a group, or
    // not selected at all) could silently hijack a click meant to start moving a
    // whole multi-note selection, collapsing it down to just that one note. ----
    if (tool == Tool::draw && ! addPoint)
    {
        if (auto sole = soleSelection(); sole != juce::Uuid::null())
        {
            if (auto* n = findNote(snap, sole))
            {
                auto h = handleIndexAt(*n, pos);
                if (h.pointIndex >= 0)
                {
                    dragMode = DragMode::bendHandle;
                    dragNoteId = n->id;
                    dragPointIndex = h.pointIndex;
                    draggingOutHandle = h.isOut;
                    repaint();
                    return;
                }

                const int idx = pointIndexAt(*n, pos);
                if (idx >= 0)
                {
                    // Shift-click a point: anchor / apply a wave section instead of dragging.
                    if (e.mods.isShiftDown())
                    {
                        pickShapeRangePoint(n->id, idx);
                        repaint();
                        return;
                    }

                    dragMode = DragMode::movePoint;
                    dragNoteId = n->id;
                    dragPointIndex = idx;
                    repaint();
                    return;
                }
            }
        }
    }

    // ---- left-edge resize handle: trim/extend from the start, end stays put.
    // Comes after the bend-points check above, so a click ON point 0 itself still
    // shapes its value; only clicks outside any point's hit radius land here ----
    if (! rightClick && ! addPoint && ! e.mods.isShiftDown())
    {
        auto tryResizeStart = [&](const MpeNote& n) -> bool
        {
            if (! nearLeftEdge(n, pos))
                return false;

            dragMode = DragMode::resizeStart;
            dragAnchorBeat = beatForX(pos.x);
            resizeStartOrigins.clear();
            if (isSelected(n.id) && selection.size() > 1)
            {
                for (auto& m : snap)
                    if (isSelected(m.id))
                        resizeStartOrigins.push_back(m);
            }
            else
            {
                selectOnly(n.id);
                resizeStartOrigins.push_back(n);
            }
            repaint();
            return true;
        };
        for (auto& n : snap) if (isSelected(n.id) && tryResizeStart(n)) return;
        for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryResizeStart(*it)) return;
    }

    // ---- Ctrl+click a ribbon -> add a bend point ----
    if (! selectMode && addPoint)
    {
        auto tryAdd = [&](const MpeNote& n) -> bool
        {
            if (! ribbonHit(n, pos))
                return false;
            const double b = juce::jlimit(0.03125, n.bend.lastBeat() - 0.03125,
                                          snapBeat(beatForX(pos.x) - n.startBeat, fine));
            float semis = pitchForY(pos.y) - (float) n.pitch;
            if (! fine) semis = std::round(semis);
            semis = juce::jlimit(-range, range, semis);

            int newIndex = -1;
            editNotes([&](std::vector<MpeNote>& notes)
            {
                for (auto& m : notes)
                    if (m.id == n.id) { newIndex = m.bend.addPoint(b, semis); break; }
            });
            selectOnly(n.id);
            dragMode = DragMode::movePoint;
            dragNoteId = n.id;
            dragPointIndex = newIndex;
            repaint();
            return true;
        };
        for (auto& n : snap) if (isSelected(n.id) && tryAdd(n)) return;
        for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryAdd(*it)) return;
        return;
    }

    // ---- shared: note ribbon ----
    auto tryRibbon = [&](const MpeNote& n) -> bool
    {
        if (! ribbonHit(n, pos))
            return false;

        // Draw tool, Shift held: duplicate this note (or the whole selection if it's
        // part of one) and drag the copies.
        if (tool == Tool::draw && e.mods.isShiftDown() && ! addPoint)
        {
            std::vector<juce::Uuid> src = (isSelected(n.id) && selection.size() > 1)
                                              ? selection : std::vector<juce::Uuid>{ n.id };
            std::vector<juce::Uuid> copies;
            editNotes([&](std::vector<MpeNote>& notes)
            {
                std::vector<MpeNote> add;
                for (auto& m : notes)
                    if (std::find(src.begin(), src.end(), m.id) != src.end())
                    {
                        MpeNote c = m;
                        c.id = juce::Uuid();
                        c.assignedChannel = -1;
                        c.isSounding = false;
                        copies.push_back(c.id);
                        add.push_back(c);
                    }
                for (auto& c : add) notes.push_back(std::move(c));
            });
            selection = copies;
            processor.readNotes([&](const std::vector<MpeNote>& nn) { snap = nn; });
            beginMoveNotes(pos, snap);
            repaint();
            return true;
        }

        if (tool == Tool::select && e.mods.isShiftDown())
        {
            toggleSelected(n.id);
            repaint();
            return true;
        }

        // keep an existing selection (single or group) when the clicked note is
        // already in it; otherwise select just this note
        if (! isSelected(n.id))
            selectOnly(n.id);

        beginMoveNotes(pos, snap, n.id);   // n is the note actually grabbed
        repaint();
        return true;
    };
    for (auto& n : snap) if (isSelected(n.id) && tryRibbon(n)) return;
    for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryRibbon(*it)) return;

    // ---- empty space ----
    if (selectMode)
    {
        marqueeAdditive = tool == Tool::select && e.mods.isShiftDown();
        marqueeA = marqueeB = pos;
        preMarqueeSelection = marqueeAdditive ? selection : std::vector<juce::Uuid>{};
        if (! marqueeAdditive)
            selection.clear();
        dragMode = DragMode::marquee;
        repaint();
        return;
    }

    MpeNote n;
    n.startBeat = snapBeat(beatForX(pos.x), fine);
    n.lengthBeats = juce::jmax(0.25, lastNoteLength);
    n.pitch = maybeSnapPitch(juce::jlimit(lowestPitch, highestPitch, (int) std::round(pitchForY(pos.y))));
    n.velocity = 0.85f;
    n.lengthBeats = n.bend.conformEnd(n.lengthBeats);
    const auto id = n.id;

    editNotes([&](std::vector<MpeNote>& notes) { notes.push_back(n); });
    selectOnly(id);
    snap.push_back(n);
    beginMoveNotes(pos, snap);
    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (panning)
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            const auto d = e.getScreenPosition().toFloat() - panStartMouseScreen;
            vp->setViewPosition(panStartView.x - juce::roundToInt(d.x),
                                panStartView.y - juce::roundToInt(d.y));
        }
        return;
    }

    if (rightErasing)
    {
        eraseNoteAt(e.position);
        repaint();
        return;
    }

    if (dragMode == DragMode::none)
        return;

    const auto pos = e.position;
    const bool fine = e.mods.isAltDown();

    if (dragMode == DragMode::marquee)
    {
        std::vector<MpeNote> snap;
        processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });
        updateMarquee(pos, snap, marqueeAdditive);
        repaint();
        return;
    }

    const float range = (float) processor.getPitchBendRangeSemitones();

    // ---- shape sliders ----
    if (dragMode == DragMode::shapeCycles || dragMode == DragMode::shapeSqueeze
        || dragMode == DragMode::shapeAmpStart || dragMode == DragMode::shapeAmpEnd)
    {
        editNotes([&](std::vector<MpeNote>& notes)
        {
            for (auto& n : notes)
            {
                if (n.id != dragNoteId)
                    continue;
                auto s = shapeUIFor(n);
                if (! s.valid || ! s.wave)
                    break;
                const int effIdx = effectiveShapeSectionIndex(n);
                if (effIdx < 0 || effIdx >= (int) n.waveSections.size())
                    break;
                auto& ws = n.waveSections[(size_t) effIdx];
                const float span = juce::jmax(1.0f, s.x1 - s.x0);

                if (dragMode == DragMode::shapeCycles)
                {
                    const double spanB = ws.toBeat - ws.fromBeat;
                    const float cycMax = cycMaxFor(spanB);
                    float cyc = 1.0f + juce::jlimit(0.0f, 1.0f, (pos.x - s.x0) / span) * (cycMax - 1.0f);
                    cyc = std::round(cyc * 2.0f) / 2.0f;   // always 0.5-cycle steps: the wave ends on the chord
                    ws.cycles = juce::jmax(1.0f, cyc);
                }
                else if (dragMode == DragMode::shapeSqueeze)
                {
                    ws.skew = normToSqueeze(juce::jlimit(0.0f, 1.0f, (pos.x - s.x0) / span));
                }
                else   // amplitude knobs: drag up = louder, +/- one octave
                {
                    // Expo response: slow and fine near 0 (0.25-semitone steps),
                    // accelerating for the bigger swings further out.
                    const float dPix = shapeGrabY - pos.y;
                    const float sgn  = dPix >= 0.0f ? 1.0f : -1.0f;
                    const float delta = sgn * (std::pow(1.0f + std::abs(dPix) / 130.0f, 1.9f) - 1.0f);
                    float a = shapeGrabVal + delta;
                    if (! fine) a = std::round(a * 4.0f) / 4.0f;   // 0.00 / 0.25 / 0.50 / ...
                    a = juce::jlimit(-juce::jmin(shapeAmpMax, range), juce::jmin(shapeAmpMax, range), a);
                    if (dragMode == DragMode::shapeAmpStart) ws.ampStart = a;
                    else                                     ws.ampEnd = a;
                }
                break;
            }
        });
        repaint();
        return;
    }

    editNotes([&](std::vector<MpeNote>& notes)
    {
        if (dragMode == DragMode::moveNotes)
        {
            const double dBeat = snapDelta(beatForX(pos.x) - dragAnchorBeat, fine);
            int dPitch = (int) std::round(pitchForY(pos.y) - dragAnchorPitch);

            if (! dragOrigins.empty() && processor.getSnapToScale()
                && processor.getScaleType() != Scale::chromatic)
            {
                // Snap by ONE reference note (whichever was grabbed) so a multi-note
                // ("chord") drag moves in lock-step. Snapping each note's own landing
                // pitch independently left some notes stuck while others jumped, since
                // the nearest in-scale tone isn't a linear function of the offset.
                const NoteOrigin* lead = nullptr;
                for (auto& o : dragOrigins)
                    if (o.id == dragLeadNoteId) { lead = &o; break; }
                if (lead == nullptr)
                    lead = &dragOrigins.front();

                const int snapped = maybeSnapPitch(juce::jlimit(lowestPitch, highestPitch, lead->pitch + dPitch));
                dPitch = snapped - lead->pitch;
            }

            for (auto& o : dragOrigins)
                for (auto& n : notes)
                    if (n.id == o.id)
                    {
                        n.startBeat = juce::jmax(0.0, o.startBeat + dBeat);
                        n.pitch = juce::jlimit(lowestPitch, highestPitch, o.pitch + dPitch);
                        break;
                    }
            return;
        }

        if (dragMode == DragMode::resizeEnds)
        {
            const double dBeat  = snapDelta(beatForX(pos.x) - dragAnchorBeat, fine);
            const float  dSemis = fine ? (pitchForY(pos.y) - dragAnchorPitch)
                                       : std::round(pitchForY(pos.y) - dragAnchorPitch);
            for (auto& o : endOrigins)
                for (auto& n : notes)
                    if (n.id == o.id)
                    {
                        const int li = n.bend.lastIndex();
                        const double minLen = juce::jmax(0.25, n.bend.beatBefore(li) + 0.0625);
                        const double newLen = juce::jmax(minLen, o.lengthBeats + dBeat);
                        n.bend.movePoint(li, newLen, juce::jlimit(-range, range, o.endValue + dSemis));
                        n.lengthBeats = newLen;
                        break;
                    }
            return;
        }

        if (dragMode == DragMode::resizeStart)
        {
            const double dBeat = snapDelta(beatForX(pos.x) - dragAnchorBeat, fine);
            for (auto& orig : resizeStartOrigins)
                for (auto& n : notes)
                    if (n.id == orig.id)
                    {
                        const double oldEnd = orig.startBeat + orig.lengthBeats;
                        double newStart = juce::jmax(0.0, orig.startBeat + dBeat);
                        newStart = juce::jmin(newStart, oldEnd - 0.25);   // keep a minimum length
                        const double delta = newStart - orig.startBeat;   // >0 trims, <0 extends

                        if (delta > 1.0e-9)
                            n.bend = curveRebaseFrom(orig.bend, delta);
                        else if (delta < -1.0e-9)
                            n.bend = curveExtendLeft(orig.bend, -delta);
                        else
                            n.bend = orig.bend;

                        n.startBeat = newStart;
                        n.lengthBeats = n.bend.conformEnd(oldEnd - newStart);

                        // carry the wave sections along too, shifted the same way,
                        // dropping/clipping any that fall outside the new length
                        n.waveSections = orig.waveSections;
                        for (auto& ws : n.waveSections) { ws.fromBeat -= delta; ws.toBeat -= delta; }
                        n.waveSections.erase(std::remove_if(n.waveSections.begin(), n.waveSections.end(),
                            [&](const WaveSection& ws) { return ws.toBeat <= 0.0 || ws.fromBeat >= n.lengthBeats; }),
                            n.waveSections.end());
                        for (auto& ws : n.waveSections)
                        {
                            ws.fromBeat = juce::jmax(0.0, ws.fromBeat);
                            ws.toBeat   = juce::jmin(n.lengthBeats, ws.toBeat);
                        }
                        break;
                    }
            return;
        }

        if (dragMode == DragMode::bendHandle)
        {
            for (auto& n : notes)
            {
                if (n.id != dragNoteId)
                    continue;
                const auto& pts = n.bend.getPoints();
                if (dragPointIndex < 0 || dragPointIndex >= (int) pts.size())
                    break;

                // clamp the handle's beat to the point's own neighbours so the
                // curve's time axis can never run backwards
                double loBeat, hiBeat;
                if (draggingOutHandle)
                {
                    loBeat = pts[(size_t) dragPointIndex].beat;
                    hiBeat = dragPointIndex + 1 < (int) pts.size() ? pts[(size_t) (dragPointIndex + 1)].beat
                                                                    : n.lengthBeats;
                }
                else
                {
                    loBeat = dragPointIndex > 0 ? pts[(size_t) (dragPointIndex - 1)].beat : 0.0;
                    hiBeat = pts[(size_t) dragPointIndex].beat;
                }
                const double b = juce::jlimit(loBeat, juce::jmax(loBeat, hiBeat),
                                              beatForX(pos.x) - n.startBeat);

                float semis = pitchForY(pos.y) - (float) n.pitch;
                if (! fine) semis = std::round(semis);
                semis = juce::jlimit(-range, range, semis);

                if (draggingOutHandle) n.bend.setOutHandle(dragPointIndex, b, semis);
                else                   n.bend.setInHandle(dragPointIndex, b, semis);
                break;
            }
            return;
        }

        for (auto& n : notes)
        {
            if (n.id != dragNoteId || dragMode != DragMode::movePoint)
                continue;

            float semis = pitchForY(pos.y) - (float) n.pitch;
            if (! fine) semis = std::round(semis);
            semis = juce::jlimit(-range, range, semis);

            const double rawBeat = snapBeat(beatForX(pos.x) - n.startBeat, fine);

            if (dragPointIndex == n.bend.lastIndex() && dragPointIndex > 0)
            {
                const double minLen = juce::jmax(0.25, n.bend.beatBefore(dragPointIndex) + 0.0625);
                const double b = juce::jmax(minLen, rawBeat);
                dragPointIndex = n.bend.movePoint(dragPointIndex, b, semis);
                n.lengthBeats = b;
                lastNoteLength = b;
            }
            else
            {
                const double b = juce::jlimit(0.0, n.bend.lastBeat() - 0.03125, rawBeat);
                dragPointIndex = n.bend.movePoint(dragPointIndex, b, semis);
            }
            break;
        }
    });

    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&)
{
    if (panning)
    {
        panning = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    rightErasing = false;
    dragMode = DragMode::none;
    dragPointIndex = -1;
    dragOrigins.clear();
    endOrigins.clear();
    resizeStartOrigins.clear();
    preMarqueeSelection.clear();
    endGesture();
}

void PianoRollComponent::mouseMove(const juce::MouseEvent& e)
{
    hoverBeat  = beatForX(e.position.x);
    hoverPitch = pitchForY(e.position.y);
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto pos = e.position;
    if (pos.x < (float) keyboardWidth || e.mods.isRightButtonDown() || tool != Tool::draw
        || e.mods.isShiftDown())
        return;

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    // Double-click a bend point: give it Bezier tangent handles (or take them
    // away if it already has them) - see ExpressionCurve::toggleHandlesAt.
    auto tryToggle = [&](const MpeNote& n) -> bool
    {
        const int idx = pointIndexAt(n, pos);
        if (idx < 0)
            return false;

        selectOnly(n.id);
        editNotes([&](std::vector<MpeNote>& notes)
        {
            for (auto& m : notes)
                if (m.id == n.id) { m.bend.toggleHandlesAt(idx); break; }
        });
        repaint();
        return true;
    };
    for (auto& n : snap) if (isSelected(n.id) && tryToggle(n)) return;
    for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryToggle(*it)) return;
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key)
{
    const bool cmd = key.getModifiers().isCommandDown();
    const int  kc  = key.getKeyCode();   // letter keys == uppercase ASCII

    if (cmd && kc == 'Z')
    {
        const bool ok = key.getModifiers().isShiftDown() ? processor.redo() : processor.undo();
        if (ok) { selection.clear(); repaint(); }
        return true;
    }
    if (cmd && kc == 'Y')
    {
        if (processor.redo()) { selection.clear(); repaint(); }
        return true;
    }
    if (cmd && kc == 'C') { copySelection(); return true; }
    if (cmd && kc == 'X')
    {
        copySelection();
        beginGesture(); deleteSelected(); endGesture();
        return true;
    }
    if (cmd && kc == 'V')
    {
        beginGesture(); pasteClipboard(); endGesture();
        return true;
    }
    if (cmd && kc == 'B')
    {
        beginGesture(); duplicateSelectionAfter(); endGesture();
        return true;
    }
    if (cmd && kc == 'A')
    {
        selectAll();
        return true;
    }
    if (key.getModifiers().isAltDown() && kc == 'Y')   // FL-style reverse
    {
        beginGesture(); reverseSelection(); endGesture();
        return true;
    }

    // arrows: move the selection in pitch (Ctrl = a whole octave). Ctrl+Up/Down
    // with nothing selected transposes every note instead.
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey))
    {
        const int dir  = key.isKeyCode(juce::KeyPress::upKey) ? 1 : -1;
        const int step = cmd ? 12 : 1;
        beginGesture();
        if (cmd && selection.empty()) nudgeAllPitch(dir * step);
        else                          nudgeSelectionPitch(dir * step);
        endGesture();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        beginGesture(); deleteSelected(); endGesture();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        clearSelection();
        repaint();
        return true;
    }
    const auto c = key.getTextCharacter();
    if (c == 'b' || c == 'B') { setTool(Tool::draw);   if (onToolChanged) onToolChanged(Tool::draw);   return true; }
    if (c == 's' || c == 'S') { setTool(Tool::select); if (onToolChanged) onToolChanged(Tool::select); return true; }
    if (c == 'c' || c == 'C') { setTool(Tool::cut);    if (onToolChanged) onToolChanged(Tool::cut);    return true; }
    if (c == 'm' || c == 'M') { beginGesture(); toggleMuteSelected(); endGesture(); return true; }

    // 1/2/3: set every selected note's shape (straight/triangle/sine) at once -
    // same three choices as the wheel, but works across a multi-selection.
    if (c == '1') { beginGesture(); setShapeForSelection(BendShape::straight); endGesture(); return true; }
    if (c == '2') { beginGesture(); setShapeForSelection(BendShape::triangle); endGesture(); return true; }
    if (c == '3') { beginGesture(); setShapeForSelection(BendShape::sine);     endGesture(); return true; }
    return false;
}
