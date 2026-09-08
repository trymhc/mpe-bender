#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The main note grid: a piano-key sidebar plus a timeline where notes are drawn,
// moved, resized, and deleted. Reports the current selection so expression lanes
// can be shown for the right note.
class PianoRollComponent final : public juce::Component, private juce::Timer
{
public:
    explicit PianoRollComponent(MpePianoRollAudioProcessor& processorToUse);
    ~PianoRollComponent() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void(juce::Uuid)> onNoteSelected;

    static constexpr float pixelsPerBeat = 80.0f;
    static constexpr int keyboardWidth = 50;

private:
    void timerCallback() override { repaint(); }

    enum class DragMode { none, moveNote, resizeRight };

    int pitchToY(int pitch) const { return (highestPitch - pitch) * rowHeight; }
    int yToPitch(int y) const { return juce::jlimit(lowestPitch, highestPitch, highestPitch - y / rowHeight); }
    float beatToX(double beat) const { return (float) keyboardWidth + (float) (beat * pixelsPerBeat); }
    double xToBeat(float x) const { return juce::jmax(0.0, (double) ((x - (float) keyboardWidth) / pixelsPerBeat)); }
    static double snapBeat(double beat) { constexpr double grid = 0.25; return std::round(beat / grid) * grid; }
    bool isBlackKey(int pitch) const;

    MpePianoRollAudioProcessor& processor;

    juce::Uuid selectedId;
    DragMode dragMode = DragMode::none;
    juce::Uuid dragNoteId;
    double dragStartBeatOffset = 0.0;

    static constexpr int lowestPitch = 24;   // C1
    static constexpr int highestPitch = 96;  // C7
    static constexpr int rowHeight = 14;
};
