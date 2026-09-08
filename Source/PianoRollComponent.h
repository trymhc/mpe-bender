#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The note grid. Each note is drawn as a ribbon that follows its own pitch curve
// (base key + bend points) across the keyboard - so a note can start on one key
// and bend to another, drawn and heard as one gesture. Bend points are edited
// directly on the note: double-click the ribbon to add a point, drag points in
// time and pitch, drag the small handle on the middle of a segment to curve it
// (right-click / double-click that handle resets it straight), right-click a
// point to remove it, right-click the ribbon body to delete the note.
class PianoRollComponent final : public juce::Component, private juce::Timer
{
public:
    explicit PianoRollComponent(MpePianoRollAudioProcessor& processorToUse);
    ~PianoRollComponent() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    static constexpr float pixelsPerBeat = 80.0f;
    static constexpr int keyboardWidth = 50;

private:
    void timerCallback() override { repaint(); }

    enum class DragMode { none, moveNote, resizeRight, movePoint, moveTension };

    // --- coordinate mapping (y is the CENTRE of a key row) ---
    float yForPitch(float pitch) const
    {
        return (float) (highestPitch - pitch) * rowHeight + rowHeight * 0.5f;
    }
    float pitchForY(float y) const
    {
        return (float) highestPitch + 0.5f - y / (float) rowHeight;
    }
    float xForBeat(double beat) const
    {
        return (float) keyboardWidth + (float) (beat * pixelsPerBeat);
    }
    double beatForX(float x) const
    {
        return juce::jmax(0.0, (double) ((x - (float) keyboardWidth) / pixelsPerBeat));
    }
    static double snapBeat(double beat, bool fine)
    {
        if (fine) return juce::jmax(0.0, beat);
        constexpr double grid = 0.25;
        return juce::jmax(0.0, std::round(beat / grid) * grid);
    }

    bool isBlackKey(int pitch) const;

    // Returns the index of a bend point of `note` under the mouse, or -1.
    int pointIndexAt(const MpeNote& note, juce::Point<float> pos) const;
    // Returns the index of the LEFT point of a segment whose tension handle is
    // under the mouse, or -1. Only meaningful for the selected note.
    int tensionHandleAt(const MpeNote& note, juce::Point<float> pos) const;
    // Centre of the tension handle for the segment starting at point `i`.
    juce::Point<float> tensionHandlePos(const MpeNote& note, int i) const;
    // Is the mouse on `note`'s ribbon (anywhere along its length)?
    bool ribbonHit(const MpeNote& note, juce::Point<float> pos) const;
    bool nearRightEdge(const MpeNote& note, juce::Point<float> pos) const;

    void applyTensionDrag(MpeNote& note, int leftIndex, juce::Point<float> pos) const;

    void buildNotePath(const MpeNote& note, juce::Path& path) const;

    MpePianoRollAudioProcessor& processor;

    juce::Uuid selectedId;

    DragMode dragMode = DragMode::none;
    juce::Uuid dragNoteId;
    int dragPointIndex = -1;
    double grabBeatOffset = 0.0;   // for moveNote: mouseBeat - note.startBeat at grab
    int grabPitch = 60;           // for moveNote: note.pitch at grab
    float grabPitchAtY = 60.0f;   // for moveNote: pitchForY(mouseY) at grab

    static constexpr int lowestPitch = 24;   // C1
    static constexpr int highestPitch = 96;  // C7
    static constexpr int rowHeight = 14;
    static constexpr float pointRadius = 4.0f;
};
