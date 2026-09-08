#include "PianoRollComponent.h"

PianoRollComponent::PianoRollComponent(MpePianoRollAudioProcessor& processorToUse)
    : processor(processorToUse)
{
    setWantsKeyboardFocus(true);
    setSize(keyboardWidth + (int) (processor.getLoopLengthBeats() * pixelsPerBeat),
             (highestPitch - lowestPitch + 1) * rowHeight);
    startTimerHz(30);
}

bool PianoRollComponent::isBlackKey(int pitch) const
{
    static const bool blacks[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    return blacks[((pitch % 12) + 12) % 12];
}

// ---------------------------------------------------------------------------
//  Geometry helpers
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
    float lo = (float) note.pitch;
    float hi = (float) note.pitch;
    for (auto& p : note.bend.getPoints())
    {
        lo = juce::jmin(lo, (float) note.pitch + p.value);
        hi = juce::jmax(hi, (float) note.pitch + p.value);
    }
    return { lo, hi };
}

juce::Point<float> PianoRollComponent::tensionHandlePos(const MpeNote& note, int i) const
{
    const auto& pts = note.bend.getPoints();
    if (i < 0 || i + 1 >= (int) pts.size())
        return {};

    const double b0 = juce::jlimit(0.0, note.lengthBeats, pts[(size_t) i].beat);
    const double b1 = juce::jlimit(0.0, note.lengthBeats, pts[(size_t) (i + 1)].beat);
    const double bm = 0.5 * (b0 + b1);

    return { xForBeat(note.startBeat + bm),
             yForPitch((float) note.pitch + note.bend.sample(bm)) };
}

int PianoRollComponent::tensionHandleAt(const MpeNote& note, juce::Point<float> pos) const
{
    const auto& pts = note.bend.getPoints();
    for (int i = 0; i + 1 < (int) pts.size(); ++i)
    {
        if (std::abs(pts[(size_t) (i + 1)].value - pts[(size_t) i].value) < 0.01f)
            continue;
        if (pos.getDistanceFrom(tensionHandlePos(note, i)) <= pointRadius + 3.0f)
            return i;
    }
    return -1;
}

void PianoRollComponent::applyTensionDrag(MpeNote& note, int leftIndex, juce::Point<float> pos) const
{
    const auto& pts = note.bend.getPoints();
    if (leftIndex < 0 || leftIndex + 1 >= (int) pts.size())
        return;

    const float v0 = pts[(size_t) leftIndex].value;
    const float v1 = pts[(size_t) (leftIndex + 1)].value;
    if (std::abs(v1 - v0) < 0.01f)
        return;

    const float mouseSemis = pitchForY(pos.y) - (float) note.pitch;
    const float w = juce::jlimit(0.0f, 1.0f, (mouseSemis - v0) / (v1 - v0));

    float tension = tensionForMidpoint(w);
    if (std::abs(tension) < 0.06f)
        tension = 0.0f;

    note.bend.setTension(leftIndex, tension);
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
    const float ey = yForPitch((float) note.pitch + note.bend.sample(note.lengthBeats));
    return std::abs(pos.x - ex) <= 6.0f && std::abs(pos.y - ey) <= rowHeight;
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
    if (it != selection.end())
        selection.erase(it);
    else
        selection.push_back(id);
}

void PianoRollComponent::clearSelection()
{
    selection.clear();
}

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

    const float x0 = juce::jmin(marqueeA.x, marqueeB.x);
    const float x1 = juce::jmax(marqueeA.x, marqueeB.x);
    const float y0 = juce::jmin(marqueeA.y, marqueeB.y);
    const float y1 = juce::jmax(marqueeA.y, marqueeB.y);

    const double bLo = beatForX(x0);
    const double bHi = beatForX(x1);
    const float  pLo = pitchForY(y1);
    const float  pHi = pitchForY(y0);

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

    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        auto yTop = (float) (highestPitch - pitch) * rowHeight;

        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff222226) : juce::Colour(0xff2e2e33));
        g.fillRect(0.0f, yTop, (float) keyboardWidth, (float) rowHeight);

        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff1c1c20) : juce::Colour(0xff202024));
        g.fillRect((float) keyboardWidth, yTop, (float) getWidth() - keyboardWidth, (float) rowHeight);

        if (pitch % 12 == 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.setFont(9.0f);
            g.drawText("C" + juce::String(pitch / 12 - 1), 2, (int) yTop, keyboardWidth - 4, rowHeight,
                       juce::Justification::centredLeft);
        }
    }

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (double beat = 0.0; beat <= loopLen + 0.001; beat += 1.0)
        g.drawVerticalLine((int) xForBeat(beat), 0.0f, (float) getHeight());

    g.setColour(juce::Colours::orange.withAlpha(0.5f));
    g.drawVerticalLine((int) xForBeat(loopLen), 0.0f, (float) getHeight());

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

    for (auto& n : snapshot)
        if (! isSelected(n.id))
            drawNote(n, false);
    for (auto& n : snapshot)
        if (isSelected(n.id))
            drawNote(n, true);

    // point + curve handles only when exactly one note is selected and we're drawing
    const auto sole = soleSelection();
    if (tool == Tool::draw && sole != juce::Uuid::null())
    {
        for (auto& n : snapshot)
        {
            if (n.id != sole)
                continue;

            const auto& pts = n.bend.getPoints();

            for (int i = 0; i + 1 < (int) pts.size(); ++i)
            {
                if (std::abs(pts[(size_t) (i + 1)].value - pts[(size_t) i].value) < 0.01f)
                    continue;

                auto c = tensionHandlePos(n, i);
                const float r = pointRadius - 0.5f;
                juce::Path diamond;
                diamond.addQuadrilateral(c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);

                g.setColour((pts[(size_t) i].tension != 0.0f ? juce::Colour(0xffffd23f)
                                                            : juce::Colours::white).withAlpha(0.9f));
                g.fillPath(diamond);
                g.setColour(juce::Colours::black.withAlpha(0.6f));
                g.strokePath(diamond, juce::PathStrokeType(1.0f));
            }

            for (int i = 0; i < (int) pts.size(); ++i)
            {
                auto x = xForBeat(n.startBeat + juce::jlimit(0.0, n.lengthBeats, pts[(size_t) i].beat));
                auto y = yForPitch((float) n.pitch + pts[(size_t) i].value);

                g.setColour(juce::Colours::white);
                g.fillEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
                g.setColour(juce::Colours::black.withAlpha(0.6f));
                g.drawEllipse(x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f, 1.0f);

                const float semis = pts[(size_t) i].value;
                if (std::abs(semis) >= 0.5f)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                    g.setFont(10.0f);
                    juce::String label = (semis > 0 ? "+" : "") + juce::String(semis, semis == std::round(semis) ? 0 : 1);
                    g.drawText(label, (int) (x + 6.0f), (int) (y - 14.0f), 40, 12, juce::Justification::left);
                }
            }
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
        g.drawVerticalLine((int) x, 0.0f, (float) getHeight());
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
    const bool fine = e.mods.isShiftDown();
    const bool addMod = e.mods.isCommandDown() && ! rightClick;   // Ctrl on Windows

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    // ---- Draw tool: bend-point / curve editing on the sole-selected note ----
    if (tool == Tool::draw)
    {
        const float range = (float) processor.getPitchBendRangeSemitones();

        // Ctrl+left-click a ribbon -> add a bend point and start dragging it
        if (addMod)
        {
            auto tryAdd = [&](const MpeNote& n) -> bool
            {
                if (! ribbonHit(n, pos))
                    return false;

                const double b = juce::jlimit(0.0, n.lengthBeats,
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

        // bend point under the mouse
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
            }
            repaint();
            return true;
        };

        for (auto& n : snap) if (isSelected(n.id) && tryPoint(n)) return;
        for (auto& n : snap) if (! isSelected(n.id) && tryPoint(n)) return;

        // tension handle on a segment of the sole-selected note
        if (soleSelection() != juce::Uuid::null())
        {
            for (auto& n : snap)
            {
                if (n.id != soleSelection())
                    continue;

                const int seg = tensionHandleAt(n, pos);
                if (seg < 0)
                    break;

                if (rightClick)
                    processor.modifyNotes([&](std::vector<MpeNote>& notes)
                    {
                        for (auto& m : notes)
                            if (m.id == n.id) { m.bend.setTension(seg, 0.0f); break; }
                    });
                else
                {
                    dragMode = DragMode::moveTension;
                    dragNoteId = n.id;
                    dragPointIndex = seg;
                }
                repaint();
                return;
            }
        }
    }

    // ---- shared: note ribbon under the mouse ----
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
            if (it != selection.end())
                selection.erase(it);
            repaint();
            return true;
        }

        if (fine)   // Shift: toggle this note in the selection, no drag
        {
            toggleSelected(n.id);
            repaint();
            return true;
        }

        if (! isSelected(n.id))
            selectOnly(n.id);

        if (tool == Tool::draw && selection.size() == 1 && nearRightEdge(n, pos))
        {
            dragMode = DragMode::resizeRight;
            dragNoteId = n.id;
        }
        else
        {
            beginMoveNotes(pos, snap);
        }
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

    if (tool == Tool::select)
    {
        marqueeA = marqueeB = pos;
        preMarqueeSelection = fine ? selection : std::vector<juce::Uuid>{};
        if (! fine)
            selection.clear();
        dragMode = DragMode::marquee;
        repaint();
        return;
    }

    // Draw tool on empty space: new note
    MpeNote n;
    n.startBeat = snapBeat(beatForX(pos.x), fine);
    n.lengthBeats = 1.0;
    n.pitch = juce::jlimit(lowestPitch, highestPitch, (int) std::round(pitchForY(pos.y)));
    n.velocity = 0.85f;
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
    const bool fine = e.mods.isShiftDown();

    if (dragMode == DragMode::marquee)
    {
        std::vector<MpeNote> snap;
        processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });
        updateMarquee(pos, snap, e.mods.isShiftDown());
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

        for (auto& n : notes)
        {
            if (n.id != dragNoteId)
                continue;

            if (dragMode == DragMode::resizeRight)
            {
                n.lengthBeats = juce::jmax(0.25, snapBeat(beatForX(pos.x) - n.startBeat, fine));
            }
            else if (dragMode == DragMode::movePoint)
            {
                double b = juce::jlimit(0.0, n.lengthBeats, snapBeat(beatForX(pos.x) - n.startBeat, fine));
                float semis = pitchForY(pos.y) - (float) n.pitch;
                if (! fine) semis = std::round(semis);
                semis = juce::jlimit(-range, range, semis);
                dragPointIndex = n.bend.movePoint(dragPointIndex, b, semis);
            }
            else if (dragMode == DragMode::moveTension)
            {
                applyTensionDrag(n, dragPointIndex, pos);
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
    preMarqueeSelection.clear();
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto pos = e.position;
    if (pos.x < (float) keyboardWidth || e.mods.isRightButtonDown() || tool != Tool::draw)
        return;

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    // double-click on/near a note ribbon -> add a bend (curve) point there
    auto tryAdd = [&](const MpeNote& n) -> bool
    {
        if (pointIndexAt(n, pos) >= 0)   // not right on an existing point
            return false;
        if (! ribbonHit(n, pos))
            return false;

        const double b = juce::jlimit(0.0, n.lengthBeats,
                                      snapBeat(beatForX(pos.x) - n.startBeat, e.mods.isShiftDown()));
        const float semis = n.bend.sample(b);

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
    if (key.getTextCharacter() == 'b' || key.getTextCharacter() == 'B')
    {
        setTool(Tool::draw);
        if (onToolChanged) onToolChanged(Tool::draw);
        return true;
    }
    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
    {
        setTool(Tool::select);
        if (onToolChanged) onToolChanged(Tool::select);
        return true;
    }
    return false;
}
