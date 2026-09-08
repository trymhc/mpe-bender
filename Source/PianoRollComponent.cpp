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
    return blacks[pitch % 12];
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff17171a));

    // --- piano key sidebar ---
    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        auto y = pitchToY(pitch);
        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff222226) : juce::Colour(0xff2e2e33));
        g.fillRect(0, y, keyboardWidth, rowHeight);

        if (pitch % 12 == 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.setFont(9.0f);
            g.drawText("C" + juce::String(pitch / 12 - 1), 2, y, keyboardWidth - 4, rowHeight, juce::Justification::centredLeft);
        }
    }

    // --- grid ---
    auto loopLen = processor.getLoopLengthBeats();
    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        auto y = pitchToY(pitch);
        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff1c1c20) : juce::Colour(0xff202024));
        g.fillRect(keyboardWidth, y, getWidth() - keyboardWidth, rowHeight);
    }

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (double beat = 0.0; beat <= loopLen + 0.001; beat += 1.0)
    {
        auto x = beatToX(beat);
        g.drawVerticalLine((int) x, 0.0f, (float) getHeight());
    }

    g.setColour(juce::Colours::orange.withAlpha(0.5f));
    g.drawVerticalLine((int) beatToX(loopLen), 0.0f, (float) getHeight());

    // --- notes ---
    std::vector<MpeNote> snapshot;
    processor.readNotes([&](const std::vector<MpeNote>& notes) { snapshot = notes; });

    for (auto& n : snapshot)
    {
        auto x0 = beatToX(n.startBeat);
        auto x1 = beatToX(n.endBeat());
        auto y = (float) pitchToY(n.pitch);

        auto colour = n.isSounding ? juce::Colour(0xffffd23f)
                    : (n.id == selectedId ? juce::Colour(0xff56c2ff) : juce::Colour(0xff3f7fbf));

        juce::Rectangle<float> r(x0, y + 1.0f, juce::jmax(4.0f, x1 - x0), (float) rowHeight - 2.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(r, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(r, 2.0f, 1.0f);
    }

    // --- playhead ---
    if (processor.getUiIsPlaying())
    {
        auto x = beatToX(processor.getUiPlayheadBeat());
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawVerticalLine((int) x, 0.0f, (float) getHeight());
    }
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.position.x < (float) keyboardWidth)
        return;

    auto beat = xToBeat(e.position.x);
    auto pitch = yToPitch((int) e.position.y);

    juce::Uuid hitId;
    DragMode mode = DragMode::none;
    double beatOffset = 0.0;

    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.pitch != pitch || beat < n.startBeat || beat >= n.endBeat())
                continue;

            hitId = n.id;
            auto edgeX = beatToX(n.endBeat());
            if (std::abs(e.position.x - edgeX) < 6.0f)
                mode = DragMode::resizeRight;
            else
            {
                mode = DragMode::moveNote;
                beatOffset = beat - n.startBeat;
            }
            break;
        }
    });

    if (mode != DragMode::none)
    {
        if (e.mods.isRightButtonDown())
        {
            processor.modifyNotes([&](std::vector<MpeNote>& notes)
            {
                notes.erase(std::remove_if(notes.begin(), notes.end(),
                    [&](const MpeNote& n) { return n.id == hitId; }), notes.end());
            });
            selectedId = juce::Uuid::null();
            if (onNoteSelected)
                onNoteSelected(selectedId);
            repaint();
            return;
        }

        selectedId = hitId;
        dragMode = mode;
        dragNoteId = hitId;
        dragStartBeatOffset = beatOffset;
    }
    else if (!e.mods.isRightButtonDown())
    {
        MpeNote n;
        n.startBeat = snapBeat(beat);
        n.lengthBeats = 1.0;
        n.pitch = pitch;
        auto id = n.id;

        processor.modifyNotes([&](std::vector<MpeNote>& notes) { notes.push_back(n); });

        selectedId = id;
        dragMode = DragMode::moveNote;
        dragNoteId = id;
        dragStartBeatOffset = 0.0;
    }

    if (onNoteSelected)
        onNoteSelected(selectedId);
    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    auto beat = xToBeat(e.position.x);
    auto pitch = yToPitch((int) e.position.y);

    processor.modifyNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id != dragNoteId)
                continue;

            if (dragMode == DragMode::moveNote)
            {
                n.startBeat = juce::jmax(0.0, snapBeat(beat - dragStartBeatOffset));
                n.pitch = pitch;
            }
            else if (dragMode == DragMode::resizeRight)
            {
                n.lengthBeats = juce::jmax(0.25, snapBeat(beat) - n.startBeat);
            }
            break;
        }
    });

    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::none;
}
