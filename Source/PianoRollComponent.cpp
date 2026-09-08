#include "PianoRollComponent.h"

PianoRollComponent::PianoRollComponent(MpePianoRollAudioProcessor& processorToUse)
    : processor(processorToUse)
{
    setWantsKeyboardFocus(true);
    updateContentSize();
    startTimerHz(30);
}

void PianoRollComponent::updateContentSize()
{
    // Smallest horizontal zoom = the whole loop just fits the viewport (no zooming
    // out past 8 bars). Largest = 400 px/beat.
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

void PianoRollComponent::zoomHorizontal(float factor, float anchorX)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    const double beatAtAnchor = beatForX(anchorX);
    const float screenX = vp != nullptr ? anchorX - (float) vp->getViewPositionX() : anchorX;

    pixelsPerBeat *= factor;
    updateContentSize();   // clamps + resizes

    if (vp != nullptr)
        vp->setViewPosition(juce::roundToInt(xForBeat(beatAtAnchor) - screenX), vp->getViewPositionY());
    repaint();
}

void PianoRollComponent::zoomVertical(float factor, float anchorY)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    const float pitchAtAnchor = pitchForY(anchorY);
    const float screenY = vp != nullptr ? anchorY - (float) vp->getViewPositionY() : anchorY;

    rowHeight *= factor;
    updateContentSize();

    if (vp != nullptr)
        vp->setViewPosition(vp->getViewPositionX(), juce::roundToInt(yForPitch(pitchAtAnchor) - screenY));
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
    if (e.mods.isCommandDown())
    {
        float dy = wheel.deltaY;
        if (wheel.isReversed) dy = -dy;
        const float step = dy >= 0.0f ? 1.15f : (1.0f / 1.15f);

        if (e.mods.isShiftDown())
            zoomVertical(step, e.position.y);
        else
            zoomHorizontal(step, e.position.x);
        return;
    }

    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        vp->mouseWheelMove(e.getEventRelativeTo(vp), wheel);
}

bool PianoRollComponent::isBlackKey(int pitch) const
{
    static const bool blacks[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    return blacks[((pitch % 12) + 12) % 12];
}

// ---------------------------------------------------------------------------
//  Geometry
// ---------------------------------------------------------------------------

void PianoRollComponent::buildNotePath(const MpeNote& note, juce::Path& path) const
{
    auto yAt = [&](double beatOffset)
    {
        return yForPitch((float) note.pitch + note.bend.sample(beatOffset));
    };

    const float x0 = xForBeat(note.startBeat);
    const float x1 = xForBeat(note.endBeat());

    path.startNewSubPath(x0, yAt(0.0));
    for (float x = x0 + 2.0f; x < x1; x += 2.0f)
        path.lineTo(x, yAt(beatForX(x) - note.startBeat));
    path.lineTo(x1, yAt(note.lengthBeats));
}

juce::Range<float> PianoRollComponent::pitchExtent(const MpeNote& note) const
{
    float lo = (float) note.pitch, hi = (float) note.pitch;
    for (auto& p : note.bend.getPoints())
    {
        lo = juce::jmin(lo, (float) note.pitch + p.value);
        hi = juce::jmax(hi, (float) note.pitch + p.value);
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

    const float semis = note.bend.sample(b - note.startBeat);
    const float ry = yForPitch((float) note.pitch + semis);
    return std::abs(pos.y - ry) <= rowHeight * 0.65f;
}

bool PianoRollComponent::nearRightEdge(const MpeNote& note, juce::Point<float> pos) const
{
    const float ex = xForBeat(note.endBeat());
    if (pos.x < ex - 7.0f || pos.x > ex + 4.0f)
        return false;

    // vertical: anywhere near the note's height at its end (generous, so a bend
    // point sitting on the edge doesn't steal the resize)
    const float ey = yForPitch((float) note.pitch + note.bend.sample(note.lengthBeats));
    return std::abs(pos.y - ey) <= rowHeight * 1.5f;
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
    g.fillAll(juce::Colour(0xff17171a));

    const auto loopLen = processor.getLoopLengthBeats();
    const float h = (float) getHeight();

    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        auto yTop = (float) (highestPitch - pitch) * rowHeight;

        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff222226) : juce::Colour(0xff2e2e33));
        g.fillRect(0.0f, yTop, (float) keyboardWidth, (float) rowHeight);

        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff1c1c20) : juce::Colour(0xff202024));
        g.fillRect((float) keyboardWidth, yTop, (float) getWidth() - keyboardWidth, (float) rowHeight);

        if (pitch % 12 == 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.drawHorizontalLine((int) yTop, (float) keyboardWidth, (float) getWidth());
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.setFont(9.0f);
            g.drawText("C" + juce::String(pitch / 12 - 1), 2, (int) yTop, keyboardWidth - 4, (int) rowHeight,
                       juce::Justification::centredLeft);
        }
    }

    // vertical grid: 1/4-beat, beat, bar (skip finer lines when zoomed out)
    auto verticals = [&](double step, juce::Colour c)
    {
        g.setColour(c);
        for (double b = 0.0; b <= loopLen + 1.0e-6; b += step)
            g.drawVerticalLine((int) xForBeat(b), 0.0f, h);
    };
    if (pixelsPerBeat * 0.25f >= 5.0f) verticals(0.25, juce::Colours::white.withAlpha(0.035f));
    if (pixelsPerBeat        >= 5.0f) verticals(1.0,  juce::Colours::white.withAlpha(0.11f));
    verticals(4.0, juce::Colours::white.withAlpha(0.20f));

    g.setColour(juce::Colours::orange.withAlpha(0.5f));
    g.drawVerticalLine((int) xForBeat(loopLen), 0.0f, h);

    std::vector<MpeNote> snapshot;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snapshot = notes; });

    auto drawNote = [&](const MpeNote& n, bool selected)
    {
        juce::Path p;
        buildNotePath(n, p);

        const juce::Colour base = n.isSounding ? juce::Colour(0xffffd23f)
                                : selected       ? juce::Colour(0xff6cc4ff)
                                                 : juce::Colour(0xff3f7fbf);

        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.strokePath(p, juce::PathStrokeType(selected ? 12.0f : 10.0f,
                                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(base.withAlpha(selected ? 0.95f : 0.8f));
        g.strokePath(p, juce::PathStrokeType(selected ? 9.0f : 7.0f,
                                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    for (auto& n : snapshot) if (! isSelected(n.id)) drawNote(n, false);
    for (auto& n : snapshot) if (isSelected(n.id))   drawNote(n, true);

    const auto sole = soleSelection();
    if (tool == Tool::draw && sole != juce::Uuid::null())
    {
        for (auto& n : snapshot)
        {
            if (n.id != sole)
                continue;

            const auto& pts = n.bend.getPoints();
            for (int i = 0; i < (int) pts.size(); ++i)
            {
                auto x = xForBeat(n.startBeat + juce::jlimit(0.0, n.lengthBeats, pts[(size_t) i].beat));
                auto y = yForPitch((float) n.pitch + pts[(size_t) i].value);

                if (pts[(size_t) i].anchor)
                {
                    g.setColour(juce::Colours::white);
                    g.fillEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
                    g.setColour(juce::Colours::black.withAlpha(0.6f));
                    g.drawEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f, 1.0f);

                    const float semis = pts[(size_t) i].value;
                    if (std::abs(semis) >= 0.5f)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.85f));
                        g.setFont(10.0f);
                        juce::String label = (semis > 0 ? "+" : "")
                                           + juce::String(semis, semis == std::round(semis) ? 0 : 1);
                        g.drawText(label, (int) (x + 6.0f), (int) (y - 14.0f), 44, 12, juce::Justification::left);
                    }
                }
                else
                {
                    const float r = pointRadius - 0.3f;
                    juce::Path d;
                    d.addQuadrilateral(x, y - r, x + r, y, x, y + r, x - r, y);
                    g.setColour(juce::Colour(0xffffd23f).withAlpha(0.95f));
                    g.fillPath(d);
                    g.setColour(juce::Colours::black.withAlpha(0.6f));
                    g.strokePath(d, juce::PathStrokeType(1.0f));
                }
            }
        }
    }
    else if (selection.size() > 1)
    {
        // multi-selection: mark each note's end point so the group resize handle is visible
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
        g.setColour(juce::Colour(0xff6cc4ff).withAlpha(0.15f));
        g.fillRect(r);
        g.setColour(juce::Colour(0xff6cc4ff).withAlpha(0.8f));
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
    const bool addAnchor  = e.mods.isCommandDown() && ! rightClick;
    const bool springSel  = tool == Tool::draw && e.mods.isShiftDown() && ! rightClick && ! addAnchor;
    const bool selectMode = tool == Tool::select || springSel;

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    const float range = (float) processor.getPitchBendRangeSemitones();

    // ---- right-edge resize handle (both tools) ----
    // The end anchor IS the resize handle: grabbing near the note end grabs the
    // trailing bend point. Dragging it horizontally changes the note length, and
    // vertically bends the note's tail. Checked before other points so a point on
    // the edge doesn't block it.
    if (! rightClick && ! addAnchor && ! e.mods.isShiftDown())
    {
        auto tryResize = [&](const MpeNote& n) -> bool
        {
            const int last = n.bend.lastIndex();
            if (last <= 0 || ! nearRightEdge(n, pos))
                return false;

            if (isSelected(n.id) && selection.size() > 1)
            {
                // group resize: drag every selected note's end anchor together
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
                dragPointIsAnchor = n.bend.isAnchor(last);
            }
            repaint();
            return true;
        };
        for (auto& n : snap) if (isSelected(n.id) && tryResize(n)) return;
        for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryResize(*it)) return;
    }

    // ---- Draw-tool point / diamond editing (only when NOT acting as select) ----
    if (! selectMode)
    {
        if (addAnchor)
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
                        if (m.id == n.id) { newIndex = m.bend.addPoint(b, semis, true); break; }
                });
                selectOnly(n.id);
                dragMode = DragMode::movePoint;
                dragNoteId = n.id;
                dragPointIndex = newIndex;
                dragPointIsAnchor = true;
                repaint();
                return true;
            };
            for (auto& n : snap) if (isSelected(n.id) && tryAdd(n)) return;
            for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryAdd(*it)) return;
            return;
        }

        auto tryPoint = [&](const MpeNote& n) -> bool
        {
            const int idx = pointIndexAt(n, pos);
            if (idx < 0)
                return false;

            if (rightClick)
            {
                if (idx > 0)
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
                dragPointIsAnchor = n.bend.isAnchor(idx);
            }
            repaint();
            return true;
        };

        for (auto& n : snap) if (isSelected(n.id) && tryPoint(n)) return;
        for (auto& n : snap) if (! isSelected(n.id) && tryPoint(n)) return;
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

        const bool addToSel = tool == Tool::select && e.mods.isShiftDown();

        if (addToSel)
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
    n.lengthBeats = 1.0;
    n.pitch = juce::jlimit(lowestPitch, highestPitch, (int) std::round(pitchForY(pos.y)));
    n.velocity = 0.85f;
    n.lengthBeats = n.bend.conformEnd(n.lengthBeats);   // start + end anchors
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
                        const float  newEnd = juce::jlimit(-range, range, o.endValue + dSemis);
                        n.bend.movePoint(li, newLen, newEnd);
                        n.lengthBeats = newLen;
                        break;
                    }
            return;
        }

        for (auto& n : notes)
        {
            if (n.id != dragNoteId)
                continue;

            if (dragMode == DragMode::movePoint)
            {
                float semis = pitchForY(pos.y) - (float) n.pitch;
                if (! fine && dragPointIsAnchor)
                    semis = std::round(semis);
                semis = juce::jlimit(-range, range, semis);

                const double rawBeat = snapBeat(beatForX(pos.x) - n.startBeat, fine);

                if (dragPointIndex == n.bend.lastIndex() && dragPointIndex > 0)
                {
                    // the end anchor: horizontal = note length, vertical = tail bend
                    const double minLen = juce::jmax(0.25, n.bend.beatBefore(dragPointIndex) + 0.0625);
                    const double b = juce::jmax(minLen, rawBeat);
                    dragPointIndex = n.bend.movePoint(dragPointIndex, b, semis);
                    n.lengthBeats = b;
                }
                else
                {
                    // an interior point / diamond: stay inside the note
                    const double b = juce::jlimit(0.0, n.bend.lastBeat() - 0.03125, rawBeat);
                    dragPointIndex = n.bend.movePoint(dragPointIndex, b, semis);
                }
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

    auto tryAddDiamond = [&](const MpeNote& n) -> bool
    {
        if (pointIndexAt(n, pos) >= 0)
            return false;
        if (! ribbonHit(n, pos))
            return false;

        const double b = juce::jlimit(0.03125, n.bend.lastBeat() - 0.03125,
                                      snapBeat(beatForX(pos.x) - n.startBeat, e.mods.isAltDown()));
        const float semis = n.bend.sample(b);

        int newIndex = -1;
        processor.modifyNotes([&](std::vector<MpeNote>& notes)
        {
            for (auto& m : notes)
                if (m.id == n.id) { newIndex = m.bend.addPoint(b, semis, false); break; }
        });

        selectOnly(n.id);
        dragMode = DragMode::movePoint;
        dragNoteId = n.id;
        dragPointIndex = newIndex;
        dragPointIsAnchor = false;
        repaint();
        return true;
    };

    for (auto& n : snap) if (isSelected(n.id) && tryAddDiamond(n)) return;
    for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (! isSelected(it->id) && tryAddDiamond(*it)) return;
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
