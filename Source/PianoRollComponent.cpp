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
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const double loop = juce::jmax(1.0, processor.getLoopLengthBeats());
        const float avail = (float) vp->getWidth() - (float) keyboardWidth - 16.0f;
        if (avail > 20.0f)
            minPixelsPerBeat = juce::jmin(400.0f, (float) (avail / loop));
    }

    pixelsPerBeat = juce::jlimit(minPixelsPerBeat, 400.0f, pixelsPerBeat);
    rowHeight = juce::jlimit(6.0f, 40.0f, rowHeight);

    const int w = keyboardWidth + juce::roundToInt(processor.getLoopLengthBeats() * pixelsPerBeat);
    const int h = juce::roundToInt((float) (highestPitch - lowestPitch + 1) * rowHeight);
    setSize(juce::jmax(1, w), juce::jmax(1, h));
}

void PianoRollComponent::zoomBoth(float factor, float anchorX, float anchorY)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    const double beatAtX  = beatForX(anchorX);
    const float  pitchAtY = pitchForY(anchorY);
    const float  sx = vp != nullptr ? anchorX - (float) vp->getViewPositionX() : anchorX;
    const float  sy = vp != nullptr ? anchorY - (float) vp->getViewPositionY() : anchorY;

    pixelsPerBeat *= factor;
    rowHeight     *= factor;
    updateContentSize();

    if (vp != nullptr)
        vp->setViewPosition(juce::roundToInt(xForBeat(beatAtX) - sx),
                            juce::roundToInt(yForPitch(pitchAtY) - sy));
    repaint();
}

void PianoRollComponent::resetZoom()
{
    pixelsPerBeat = defaultPixelsPerBeat;
    rowHeight = defaultRowHeight;
    updateContentSize();
    repaint();
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())   // Ctrl / trackpad pinch -> zoom both axes
    {
        float dy = wheel.deltaY;
        if (wheel.isReversed) dy = -dy;
        if (std::abs(dy) < 1.0e-4f)
            return;
        const float step = dy > 0.0f ? 1.12f : (1.0f / 1.12f);
        zoomBoth(step, e.position.x, e.position.y);
        return;
    }

    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        vp->mouseWheelMove(e.getEventRelativeTo(vp), wheel);
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
    const float step = note.shape == BendShape::straight ? 3.0f : 1.5f;

    path.startNewSubPath(x0, yAt(0.0));
    for (float x = x0 + step; x < x1; x += step)
        path.lineTo(x, yAt(beatForX(x) - note.startBeat));
    path.lineTo(x1, yAt(note.lengthBeats));
}

juce::Range<float> PianoRollComponent::pitchExtent(const MpeNote& note) const
{
    float lo = 1.0e9f, hi = -1.0e9f;
    const int steps = note.shape == BendShape::straight ? 8 : 48;
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
    return std::abs(pos.y - ry) <= rowHeight * 0.7f;
}

bool PianoRollComponent::nearRightEdge(const MpeNote& note, juce::Point<float> pos) const
{
    const float ex = xForBeat(note.endBeat());
    if (pos.x < ex - 7.0f || pos.x > ex + 4.0f)
        return false;
    const float ey = yForPitch((float) note.pitch + note.bend.sample(note.lengthBeats));
    return std::abs(pos.y - ey) <= rowHeight * 1.5f;
}

const MpeNote* PianoRollComponent::findNote(const std::vector<MpeNote>& snap, const juce::Uuid& id) const
{
    for (auto& n : snap)
        if (n.id == id)
            return &n;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Shape editor overlay
// ---------------------------------------------------------------------------

namespace
{
    constexpr float shapeAmpMax = 12.0f;   // +/- one octave

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
}

PianoRollComponent::ShapeUI PianoRollComponent::shapeUIFor(const MpeNote& note) const
{
    ShapeUI s;
    const auto& pts = note.bend.getPoints();
    if (pts.size() < 2)
        return s;

    s.valid = true;
    s.noteId = note.id;
    s.x0 = xForBeat(note.startBeat + pts.front().beat);
    s.y0 = yForPitch((float) note.pitch + pts.front().value);
    s.x1 = xForBeat(note.endBeat());
    s.y1 = yForPitch((float) note.pitch + pts.back().value);
    s.wave = note.shape != BendShape::straight;

    const float midX = 0.5f * (s.x0 + s.x1);

    // Place the controls off the wave's *envelope* - chord extent +/- the larger
    // amplitude - not the live sampled curve, so dragging cycles/skew (which only
    // change the wave inside a fixed envelope) doesn't make the sliders jump.
    float chordLo = 1.0e9f, chordHi = -1.0e9f;
    for (auto& p : pts) { chordLo = juce::jmin(chordLo, p.value); chordHi = juce::jmax(chordHi, p.value); }
    const float maxAmp = juce::jmax(std::abs(note.shapeAmpStart), std::abs(note.shapeAmpEnd)) + 0.5f;
    const float drawnBot = s.wave ? yForPitch((float) note.pitch + chordLo - maxAmp) : juce::jmax(s.y0, s.y1);
    const float drawnTop = s.wave ? yForPitch((float) note.pitch + chordHi + maxAmp) : juce::jmin(s.y0, s.y1);

    s.wheel  = { midX, drawnBot + (s.wave ? 40.0f : 24.0f) };
    s.wheelR = 15.0f;

    if (s.wave)
    {
        s.cyclesTrack  = { s.x0, drawnBot + 8.0f,  s.x1 - s.x0, 6.0f };
        s.squeezeTrack = { s.x0, drawnTop - 16.0f, s.x1 - s.x0, 6.0f };

        const double spanB = note.bend.lastBeat() - note.bend.firstBeat();
        const float cycMax = cycMaxFor(spanB);
        const float cyN = juce::jlimit(0.0f, 1.0f, (note.shapeCycleCount() - 1.0f) / juce::jmax(1.0f, cycMax - 1.0f));
        s.cyclesH  = { s.x0 + cyN * (s.x1 - s.x0), s.cyclesTrack.getCentreY() };
        s.squeezeH = { s.x0 + squeezeToNorm(note.shapeSkew) * (s.x1 - s.x0), s.squeezeTrack.getCentreY() };

        s.ampStartKnob = { s.x0 - 16.0f, s.y0 };
        s.ampEndKnob   = { s.x1 + 16.0f, s.y1 };
    }
    return s;
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

void PianoRollComponent::setNoteShape(const juce::Uuid& id, BendShape shp)
{
    processor.modifyNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
            if (n.id == id) { n.shape = shp; break; }
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

void PianoRollComponent::clearSelection() { selection.clear(); }

juce::Uuid PianoRollComponent::soleSelection() const
{
    return selection.size() == 1 ? selection.front() : juce::Uuid::null();
}

void PianoRollComponent::deleteSelected()
{
    if (selection.empty())
        return;
    processor.modifyNotes([&](std::vector<MpeNote>& notes)
    {
        notes.erase(std::remove_if(notes.begin(), notes.end(),
            [&](const MpeNote& n) { return isSelected(n.id); }), notes.end());
    });
    selection.clear();
    repaint();
}

void PianoRollComponent::beginMoveNotes(juce::Point<float> pos, const std::vector<MpeNote>& snapshot)
{
    dragMode = DragMode::moveNotes;
    dragAnchorBeat = beatForX(pos.x);
    dragAnchorPitch = pitchForY(pos.y);
    dragOrigins.clear();
    for (auto& n : snapshot)
        if (isSelected(n.id))
            dragOrigins.push_back({ n.id, n.startBeat, n.pitch });
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
    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        auto yTop = (float) (highestPitch - pitch) * rowHeight;
        g.setColour(isBlackKey(pitch) ? Theme::rowDark : Theme::rowLight);
        g.fillRect((float) keyboardWidth, yTop, (float) getWidth() - keyboardWidth, (float) rowHeight);
        if (pitch % 12 == 0)
        {
            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.drawHorizontalLine((int) yTop, (float) keyboardWidth, (float) getWidth());
        }
    }

    auto verticals = [&](double step, juce::Colour c)
    {
        g.setColour(c);
        for (double b = step; b <= loopLen + 1.0e-6; b += step)
            g.drawVerticalLine((int) xForBeat(b), 0.0f, h);
    };
    if (pixelsPerBeat * 0.25f >= 5.0f) verticals(0.25, juce::Colours::black.withAlpha(0.10f));
    if (pixelsPerBeat        >= 5.0f) verticals(1.0,  juce::Colours::black.withAlpha(0.22f));
    verticals(4.0, juce::Colours::black.withAlpha(0.38f));

    g.setColour(juce::Colours::white.withAlpha(0.4f));
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
        // grid when zoomed right out
        const float core = juce::jlimit(2.0f, 11.0f, rowHeight * 0.52f) + (selected ? 1.5f : 0.0f);

        if (n.isSounding)
        {
            g.setColour(Theme::noteSel.withAlpha(0.22f));
            g.strokePath(p, juce::PathStrokeType(core + 8.0f, joint, cap));
        }
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.strokePath(p, juce::PathStrokeType(core + 2.5f, joint, cap));
        g.setColour(base.withAlpha(selected || n.isSounding ? 1.0f : 0.85f));
        g.strokePath(p, juce::PathStrokeType(core, joint, cap));
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
            for (auto& pt : pts)
            {
                auto x = xForBeat(n->startBeat + juce::jlimit(0.0, n->lengthBeats, pt.beat));
                auto y = yForPitch((float) n->pitch + pt.value);
                g.setColour(juce::Colours::white);
                g.fillEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
                g.setColour(juce::Colours::black.withAlpha(0.6f));
                g.drawEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f, 1.0f);
            }

            auto s = shapeUIFor(*n);
            if (s.valid)
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
                    g.setColour(juce::Colours::white.withAlpha(0.9f));
                    g.strokePath(gp, juce::PathStrokeType(1.3f));
                };

                const float ranges[3][2] = { { -pi / 3.0f, pi / 3.0f }, { pi / 3.0f, pi }, { -pi, -pi / 3.0f } };
                const BendShape slices[3] = { BendShape::straight, BendShape::triangle, BendShape::sine };
                for (int i = 0; i < 3; ++i)
                {
                    juce::Path wedge;
                    wedge.addPieSegment(s.wheel.x - s.wheelR, s.wheel.y - s.wheelR, s.wheelR * 2.0f, s.wheelR * 2.0f,
                                        ranges[i][0], ranges[i][1], 0.0f);
                    g.setColour(n->shape == slices[i] ? Theme::noteSel.withAlpha(0.85f) : Theme::field);
                    g.fillPath(wedge);
                    g.setColour(juce::Colours::black.withAlpha(0.4f));
                    g.strokePath(wedge, juce::PathStrokeType(1.0f));
                    sliceGlyph(0.5f * (ranges[i][0] + ranges[i][1]), slices[i]);
                }
                g.setColour(juce::Colours::black.withAlpha(0.5f));
                g.drawEllipse(s.wheel.x - s.wheelR, s.wheel.y - s.wheelR, s.wheelR * 2.0f, s.wheelR * 2.0f, 1.0f);

                if (s.wave)
                {
                    auto slider = [&](juce::Rectangle<float> r, juce::Point<float> handle)
                    {
                        g.setColour(juce::Colours::black.withAlpha(0.3f));
                        g.fillRect(r.withSizeKeepingCentre(r.getWidth(), 2.0f));
                        g.setColour(juce::Colour(0xffb0b0b0));
                        g.fillEllipse(handle.x - 4.0f, handle.y - 4.0f, 8.0f, 8.0f);
                        g.setColour(juce::Colours::black.withAlpha(0.55f));
                        g.drawEllipse(handle.x - 4.0f, handle.y - 4.0f, 8.0f, 8.0f, 1.0f);
                    };
                    slider(s.cyclesTrack,  s.cyclesH);
                    slider(s.squeezeTrack, s.squeezeH);

                    auto knob = [&](juce::Point<float> c, float value)   // value -12..+12
                    {
                        g.setColour(Theme::field);
                        g.fillEllipse(c.x - s.knobR, c.y - s.knobR, s.knobR * 2.0f, s.knobR * 2.0f);
                        g.setColour(juce::Colours::black.withAlpha(0.55f));
                        g.drawEllipse(c.x - s.knobR, c.y - s.knobR, s.knobR * 2.0f, s.knobR * 2.0f, 1.0f);
                        const float ang = juce::jmap(juce::jlimit(-shapeAmpMax, shapeAmpMax, value),
                                                    -shapeAmpMax, shapeAmpMax, -2.4f, 2.4f);
                        g.setColour(Theme::noteSel);
                        g.drawLine(c.x, c.y, c.x + std::sin(ang) * (s.knobR - 2.0f),
                                             c.y - std::cos(ang) * (s.knobR - 2.0f), 1.6f);
                    };
                    knob(s.ampStartKnob, n->shapeAmpStart);
                    knob(s.ampEndKnob,   n->shapeAmpEnd);

                    juce::String rd;
                    if (dragMode == DragMode::shapeCycles)
                        rd = juce::String(n->shapeCycleCount(), 1) + " cyc";
                    else if (dragMode == DragMode::shapeSqueeze)   rd = "skew " + juce::String(n->shapeSkew, 2);
                    else if (dragMode == DragMode::shapeAmpStart)  rd = "amp0 " + juce::String(n->shapeAmpStart, 1);
                    else if (dragMode == DragMode::shapeAmpEnd)    rd = "amp1 " + juce::String(n->shapeAmpEnd, 1);
                    if (rd.isNotEmpty())
                    {
                        juce::Rectangle<float> box(0.5f * (s.x0 + s.x1) - 42.0f, s.squeezeTrack.getY() - 20.0f, 84.0f, 16.0f);
                        g.setColour(juce::Colour(0xff2a2a2a));
                        g.fillRoundedRectangle(box, 3.0f);
                        g.setColour(Theme::text);
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
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.fillEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f, 1.0f);
        }
    }

    if (dragMode == DragMode::marquee)
    {
        juce::Rectangle<float> r(juce::Point<float>(juce::jmin(marqueeA.x, marqueeB.x), juce::jmin(marqueeA.y, marqueeB.y)),
                                 juce::Point<float>(juce::jmax(marqueeA.x, marqueeB.x), juce::jmax(marqueeA.y, marqueeB.y)));
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(r);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawRect(r, 1.0f);
    }

    if (processor.getUiIsPlaying())
    {
        auto x = xForBeat(processor.getUiPlayheadBeat());
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawVerticalLine((int) x, 0.0f, h);
    }
}

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto pos = e.position;
    if (pos.x < (float) keyboardWidth)
        return;

    const bool rightClick = e.mods.isRightButtonDown();
    const bool fine       = e.mods.isAltDown();
    const bool addPoint   = e.mods.isCommandDown() && ! rightClick;
    const bool springSel  = tool == Tool::draw && e.mods.isShiftDown() && ! rightClick && ! addPoint;
    const bool selectMode = tool == Tool::select || springSel;

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    const float range = (float) processor.getPitchBendRangeSemitones();

    // ---- shape overlay (sole selected note, draw tool) ----
    if (! rightClick && tool == Tool::draw && soleSelection() != juce::Uuid::null())
    {
        if (auto* n = findNote(snap, soleSelection()))
        {
            auto s = shapeUIFor(*n);
            if (s.valid)
            {
                const int slice = wheelSliceAt(s, pos);
                if (slice == 0) { setNoteShape(n->id, BendShape::straight); return; }
                if (slice == 1) { setNoteShape(n->id, BendShape::triangle); return; }
                if (slice == 2) { setNoteShape(n->id, BendShape::sine);     return; }

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
                    if (grabKnob(s.ampStartKnob, DragMode::shapeAmpStart, n->shapeAmpStart)) return;
                    if (grabKnob(s.ampEndKnob,   DragMode::shapeAmpEnd,   n->shapeAmpEnd))   return;
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

    // ---- bend points (draw tool) ----
    if (tool == Tool::draw && ! addPoint)
    {
        auto tryPoint = [&](const MpeNote& n) -> bool
        {
            const int idx = pointIndexAt(n, pos);
            if (idx < 0)
                return false;

            if (rightClick)
            {
                if (idx > 0 && idx < n.bend.lastIndex())
                    processor.modifyNotes([&](std::vector<MpeNote>& notes)
                    {
                        for (auto& m : notes)
                            if (m.id == n.id) { m.bend.removePoint(idx); break; }
                    });
            }
            else
            {
                selectOnly(n.id);
                dragMode = DragMode::movePoint;
                dragNoteId = n.id;
                dragPointIndex = idx;
            }
            repaint();
            return true;
        };
        for (auto& n : snap) if (isSelected(n.id) && tryPoint(n)) return;
        for (auto& n : snap) if (! isSelected(n.id) && tryPoint(n)) return;
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
            processor.modifyNotes([&](std::vector<MpeNote>& notes)
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

        if (rightClick)
        {
            processor.modifyNotes([&](std::vector<MpeNote>& notes)
            {
                notes.erase(std::remove_if(notes.begin(), notes.end(),
                    [&](const MpeNote& m) { return m.id == n.id; }), notes.end());
            });
            auto it = std::find(selection.begin(), selection.end(), n.id);
            if (it != selection.end()) selection.erase(it);
            repaint();
            return true;
        }

        if (tool == Tool::select && e.mods.isShiftDown())
        {
            toggleSelected(n.id);
            repaint();
            return true;
        }

        if (! isSelected(n.id) || selection.size() > 1)
            if (! (selectMode && isSelected(n.id)))
                selectOnly(n.id);

        beginMoveNotes(pos, snap);
        repaint();
        return true;
    };
    for (auto& n : snap) if (isSelected(n.id) && tryRibbon(n)) return;
    for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryRibbon(*it)) return;

    // ---- empty space ----
    if (rightClick)
    {
        clearSelection();
        repaint();
        return;
    }

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
    n.pitch = juce::jlimit(lowestPitch, highestPitch, (int) std::round(pitchForY(pos.y)));
    n.velocity = 0.85f;
    n.lengthBeats = n.bend.conformEnd(n.lengthBeats);
    const auto id = n.id;

    processor.modifyNotes([&](std::vector<MpeNote>& notes) { notes.push_back(n); });
    selectOnly(id);
    snap.push_back(n);
    beginMoveNotes(pos, snap);
    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
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
        processor.modifyNotes([&](std::vector<MpeNote>& notes)
        {
            for (auto& n : notes)
            {
                if (n.id != dragNoteId)
                    continue;
                auto s = shapeUIFor(n);
                if (! s.valid)
                    break;
                const float span = juce::jmax(1.0f, s.x1 - s.x0);

                if (dragMode == DragMode::shapeCycles)
                {
                    const double spanB = n.bend.lastBeat() - n.bend.firstBeat();
                    const float cycMax = cycMaxFor(spanB);
                    float cyc = 1.0f + juce::jlimit(0.0f, 1.0f, (pos.x - s.x0) / span) * (cycMax - 1.0f);
                    cyc = std::round(cyc * 2.0f) / 2.0f;   // always 0.5-cycle steps: the wave ends on the chord
                    n.shapeCycles = juce::jmax(1.0f, cyc);
                }
                else if (dragMode == DragMode::shapeSqueeze)
                {
                    n.shapeSkew = normToSqueeze(juce::jlimit(0.0f, 1.0f, (pos.x - s.x0) / span));
                }
                else   // amplitude knobs: drag up = louder, +/- one octave
                {
                    float a = shapeGrabVal + (shapeGrabY - pos.y) / 8.0f;
                    if (! fine) a = std::round(a * 2.0f) / 2.0f;
                    a = juce::jlimit(-juce::jmin(shapeAmpMax, range), juce::jmin(shapeAmpMax, range), a);
                    if (dragMode == DragMode::shapeAmpStart) n.shapeAmpStart = a;
                    else                                     n.shapeAmpEnd = a;
                }
                break;
            }
        });
        repaint();
        return;
    }

    processor.modifyNotes([&](std::vector<MpeNote>& notes)
    {
        if (dragMode == DragMode::moveNotes)
        {
            const double dBeat = snapDelta(beatForX(pos.x) - dragAnchorBeat, fine);
            const int dPitch = (int) std::round(pitchForY(pos.y) - dragAnchorPitch);
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
    dragMode = DragMode::none;
    dragPointIndex = -1;
    dragOrigins.clear();
    endOrigins.clear();
    preMarqueeSelection.clear();
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto pos = e.position;
    if (pos.x < (float) keyboardWidth || e.mods.isRightButtonDown() || tool != Tool::draw
        || e.mods.isShiftDown())
        return;

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    const float range = (float) processor.getPitchBendRangeSemitones();

    auto tryAdd = [&](const MpeNote& n) -> bool
    {
        if (pointIndexAt(n, pos) >= 0 || ! ribbonHit(n, pos))
            return false;

        const bool fine = e.mods.isAltDown();
        const double b = juce::jlimit(0.03125, n.bend.lastBeat() - 0.03125,
                                      snapBeat(beatForX(pos.x) - n.startBeat, fine));
        float semis = pitchForY(pos.y) - (float) n.pitch;
        if (! fine) semis = std::round(semis);
        semis = juce::jlimit(-range, range, semis);

        int newIndex = -1;
        processor.modifyNotes([&](std::vector<MpeNote>& notes)
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
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
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
    return false;
}
