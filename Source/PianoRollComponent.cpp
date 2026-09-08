#include "PianoRollComponent.h"

PianoRollComponent::PianoRollComponent(MpePianoRollAudioProcessor& processorToUse)
    : processor(processorToUse)
{
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
    const auto& pts = note.bend.getPoints();

    auto pointX = [&](double beatOffset) { return xForBeat(note.startBeat + juce::jlimit(0.0, note.lengthBeats, beatOffset)); };
    auto pointY = [&](float semis)        { return yForPitch((float) note.pitch + semis); };

    path.startNewSubPath(xForBeat(note.startBeat), pointY(pts.front().value));
    for (auto& p : pts)
        path.lineTo(pointX(p.beat), pointY(p.value));
    path.lineTo(xForBeat(note.endBeat()), pointY(note.bend.sample(note.lengthBeats)));
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
//  Painting
// ---------------------------------------------------------------------------

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff17171a));

    const auto loopLen = processor.getLoopLengthBeats();

    // row backgrounds + keyboard sidebar
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

    // beat grid
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (double beat = 0.0; beat <= loopLen + 0.001; beat += 1.0)
        g.drawVerticalLine((int) xForBeat(beat), 0.0f, (float) getHeight());

    g.setColour(juce::Colours::orange.withAlpha(0.5f));
    g.drawVerticalLine((int) xForBeat(loopLen), 0.0f, (float) getHeight());

    // notes
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
        if (n.id != selectedId)
            drawNote(n, false);

    for (auto& n : snapshot)
    {
        if (n.id != selectedId)
            continue;

        drawNote(n, true);

        // bend-point handles + offset labels
        const auto& pts = n.bend.getPoints();
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

    // playhead
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
    const auto pos = e.position;
    if (pos.x < (float) keyboardWidth)
        return;

    const bool rightClick = e.mods.isRightButtonDown();
    const bool fine = e.mods.isShiftDown();

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    // 1) bend point under the mouse? (selected note's points win ties)
    auto tryPoint = [&](const MpeNote& n) -> bool
    {
        const int idx = pointIndexAt(n, pos);
        if (idx < 0)
            return false;

        if (rightClick)
        {
            if (idx > 0)   // index 0 is the note's start anchor - not removable
                processor.modifyNotes([&](std::vector<MpeNote>& notes)
                {
                    for (auto& m : notes)
                        if (m.id == n.id) { m.bend.removePoint(idx); break; }
                });
        }
        else
        {
            selectedId = n.id;
            dragMode = DragMode::movePoint;
            dragNoteId = n.id;
            dragPointIndex = idx;
        }
        repaint();
        return true;
    };

    for (auto& n : snap) if (n.id == selectedId && tryPoint(n)) return;
    for (auto& n : snap) if (n.id != selectedId && tryPoint(n)) return;

    // 2) note ribbon under the mouse?
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
            if (selectedId == n.id)
                selectedId = juce::Uuid::null();
            repaint();
            return true;
        }

        selectedId = n.id;
        dragNoteId = n.id;

        if (nearRightEdge(n, pos))
        {
            dragMode = DragMode::resizeRight;
        }
        else
        {
            dragMode = DragMode::moveNote;
            grabBeatOffset = beatForX(pos.x) - n.startBeat;
            grabPitch = n.pitch;
            grabPitchAtY = pitchForY(pos.y);
        }
        repaint();
        return true;
    };

    for (auto& n : snap) if (n.id == selectedId && tryRibbon(n)) return;
    for (auto it = snap.rbegin(); it != snap.rend(); ++it) if (it->id != selectedId && tryRibbon(*it)) return;

    // 3) empty grid - create a note (left click only)
    if (rightClick)
    {
        selectedId = juce::Uuid::null();
        repaint();
        return;
    }

    MpeNote n;
    n.startBeat = snapBeat(beatForX(pos.x), fine);
    n.lengthBeats = 1.0;
    n.pitch = juce::jlimit(lowestPitch, highestPitch, (int) std::round(pitchForY(pos.y)));
    n.velocity = 0.85f;
    const auto id = n.id;

    processor.modifyNotes([&](std::vector<MpeNote>& notes) { notes.push_back(n); });

    selectedId = id;
    dragMode = DragMode::moveNote;
    dragNoteId = id;
    grabBeatOffset = beatForX(pos.x) - n.startBeat;
    grabPitch = n.pitch;
    grabPitchAtY = pitchForY(pos.y);
    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    const auto pos = e.position;
    const bool fine = e.mods.isShiftDown();
    const float range = (float) processor.getPitchBendRangeSemitones();

    processor.modifyNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id != dragNoteId)
                continue;

            if (dragMode == DragMode::moveNote)
            {
                n.startBeat = snapBeat(beatForX(pos.x) - grabBeatOffset, fine);
                const int dPitch = (int) std::round(pitchForY(pos.y) - grabPitchAtY);
                n.pitch = juce::jlimit(lowestPitch, highestPitch, grabPitch + dPitch);
            }
            else if (dragMode == DragMode::resizeRight)
            {
                n.lengthBeats = juce::jmax(0.25, snapBeat(beatForX(pos.x) - n.startBeat, fine));
            }
            else if (dragMode == DragMode::movePoint)
            {
                double b = juce::jlimit(0.0, n.lengthBeats, snapBeat(beatForX(pos.x) - n.startBeat, fine));
                float semis = pitchForY(pos.y) - (float) n.pitch;
                if (! fine)
                    semis = std::round(semis);
                semis = juce::jlimit(-range, range, semis);
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
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto pos = e.position;
    if (pos.x < (float) keyboardWidth || e.mods.isRightButtonDown())
        return;

    std::vector<MpeNote> snap;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snap = notes; });

    for (auto& n : snap)
    {
        if (! ribbonHit(n, pos))
            continue;

        const double b = juce::jlimit(0.0, n.lengthBeats,
                                      snapBeat(beatForX(pos.x) - n.startBeat, e.mods.isShiftDown()));
        const float semis = n.bend.sample(b);

        int newIndex = -1;
        processor.modifyNotes([&](std::vector<MpeNote>& notes)
        {
            for (auto& m : notes)
                if (m.id == n.id) { newIndex = m.bend.addPoint(b, semis); break; }
        });

        selectedId = n.id;
        dragMode = DragMode::movePoint;
        dragNoteId = n.id;
        dragPointIndex = newIndex;
        repaint();
        return;
    }
}
