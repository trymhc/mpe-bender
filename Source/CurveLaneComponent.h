#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Draws and edits one per-note expression curve (pitch bend, pressure, or timbre)
// for whichever note is currently selected in the piano roll.
class CurveLaneComponent final : public juce::Component
{
public:
    enum class LaneType { pitchBend, pressure, timbre };

    CurveLaneComponent(MpePianoRollAudioProcessor& processorToUse, LaneType laneType, juce::String laneLabel);

    void setSelectedNote(juce::Uuid noteId);
    void setPixelsPerBeat(float pixels);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    ExpressionCurve* getCurveForType(MpeNote& note) const;
    float valueToY(float value) const;
    float yToValue(float y) const;
    void applyPointAt(const juce::MouseEvent& e, bool removing);

    MpePianoRollAudioProcessor& processor;
    LaneType type;
    juce::String label;
    juce::Uuid selectedId;
    float pixelsPerBeat = 80.0f;
    static constexpr int keyboardWidth = 50;
};
