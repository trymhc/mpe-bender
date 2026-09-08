#include "CurveLaneComponent.h"

CurveLaneComponent::CurveLaneComponent(MpePianoRollAudioProcessor& processorToUse, LaneType laneType, juce::String laneLabel)
    : processor(processorToUse), type(laneType), label(std::move(laneLabel))
{
    setInterceptsMouseClicks(true, false);
}

void CurveLaneComponent::setSelectedNote(juce::Uuid noteId)
{
    selectedId = noteId;
    repaint();
}

void CurveLaneComponent::setPixelsPerBeat(float pixels)
{
    pixelsPerBeat = pixels;
    repaint();
}

ExpressionCurve* CurveLaneComponent::getCurveForType(MpeNote& note) const
{
    switch (type)
    {
        case LaneType::pitchBend: return &note.pitchBend;
        case LaneType::pressure:  return &note.pressure;
        case LaneType::timbre:    return &note.timbre;
    }
    return nullptr;
}

float CurveLaneComponent::valueToY(float value) const
{
    auto h = (float) getHeight();
    constexpr float margin = 6.0f;

    if (type == LaneType::pitchBend)
    {
        auto range = (float) processor.getPitchBendRangeSemitones();
        return juce::jmap(value, range, -range, margin, h - margin);
    }

    return juce::jmap(value, 1.0f, 0.0f, margin, h - margin);
}

float CurveLaneComponent::yToValue(float y) const
{
    auto h = (float) getHeight();
    constexpr float margin = 6.0f;

    if (type == LaneType::pitchBend)
    {
        auto range = (float) processor.getPitchBendRangeSemitones();
        return juce::jlimit(-range, range, juce::jmap(y, margin, h - margin, range, -range));
    }

    return juce::jlimit(0.0f, 1.0f, juce::jmap(y, margin, h - margin, 1.0f, 0.0f));
}

void CurveLaneComponent::applyPointAt(const juce::MouseEvent& e, bool removing)
{
    if (selectedId == juce::Uuid::null())
        return;

    auto absBeat = (e.position.x - (float) keyboardWidth) / pixelsPerBeat;
    auto value = yToValue(e.position.y);

    processor.modifyNotes([&](std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id != selectedId)
                continue;

            auto relBeat = juce::jlimit(0.0, n.lengthBeats, (double) absBeat - n.startBeat);
            auto* curve = getCurveForType(n);

            if (removing)
                curve->removePointNear(relBeat);
            else
                curve->setPoint(relBeat, value);

            break;
        }
    });

    repaint();
}

void CurveLaneComponent::mouseDown(const juce::MouseEvent& e)
{
    applyPointAt(e, e.mods.isRightButtonDown());
}

void CurveLaneComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown())
        applyPointAt(e, false);
}

void CurveLaneComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e22));

    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(12.0f);
    g.drawText(label, 4, 2, keyboardWidth + 100, 14, juce::Justification::topLeft);

    MpeNote snapshot;
    bool found = false;
    processor.readNotes([&](const std::vector<MpeNote>& notes)
    {
        for (auto& n : notes)
        {
            if (n.id == selectedId)
            {
                snapshot = n;
                found = true;
                break;
            }
        }
    });

    if (!found)
    {
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.drawText("Select a note to edit its " + label.toLowerCase(), getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto noteX0 = (float) keyboardWidth + (float) (snapshot.startBeat * pixelsPerBeat);
    auto noteX1 = (float) keyboardWidth + (float) (snapshot.endBeat() * pixelsPerBeat);

    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.fillRect(noteX0, 0.0f, noteX1 - noteX0, (float) getHeight());

    if (type == LaneType::pitchBend)
    {
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        auto zeroY = valueToY(0.0f);
        const float dashLengths[] = { 4.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(noteX0, zeroY, noteX1, zeroY), dashLengths, 2, 1.0f, 0);
    }

    auto* curve = getCurveForType(snapshot);
    auto& pts = curve->getPoints();

    juce::Path path;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        auto x = noteX0 + (float) (pts[i].beat * pixelsPerBeat);
        auto y = valueToY(pts[i].value);
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    if (!pts.empty())
        path.lineTo(noteX1, valueToY(pts.back().value));

    juce::Colour lineColour = type == LaneType::pitchBend ? juce::Colour(0xff56c2ff)
                             : type == LaneType::pressure  ? juce::Colour(0xffff9d56)
                                                            : juce::Colour(0xff9dff56);

    g.setColour(lineColour);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    for (auto& p : pts)
    {
        auto x = noteX0 + (float) (p.beat * pixelsPerBeat);
        auto y = valueToY(p.value);
        g.setColour(lineColour);
        g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
    }

    if (processor.getUiIsPlaying())
    {
        auto playX = (float) keyboardWidth + (float) (processor.getUiPlayheadBeat() * pixelsPerBeat);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawLine(playX, 0.0f, playX, (float) getHeight(), 1.5f);
    }
}
