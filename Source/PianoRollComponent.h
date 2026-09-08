#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <vector>

// The note grid. Each note is a ribbon that follows its own pitch curve (base key
// + bend points, with a per-segment curve handle) across the keyboard.
//
// Two tools, like FL's piano roll:
//   Draw   - click empty = new note; drag body = move; drag right edge = length;
//            double-click / Ctrl-click the ribbon = add a bend point; drag a point
//            = bend; drag a diamond = curve that segment; right-click = delete /
//            straighten.
//   Select - drag a box to marquee-select notes; drag any selected note to move the
//            whole selection; Delete removes them. Shift adds to the selection.
class PianoRollComponent final : public juce::Component, private juce::Timer
{
public:
    explicit PianoRollComponent(MpePianoRollAudioProcessor& processorToUse);
    ~PianoRollComponent() override { stopTimer(); }

    enum class Tool { draw, select };
    void setTool(Tool t) { tool = t; repaint(); }
    Tool getTool() const { return tool; }
    std::function<void(Tool)> onToolChanged;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    static constexpr float pixelsPerBeat = 80.0f;
    static constexpr int keyboardWidth = 50;

private:
    void timerCallback() override { repaint(); }

    enum class DragMode { none, marquee, moveNotes, resizeRight, movePoint, moveTension };

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
    static double snapDelta(double delta, bool fine)
    {
        if (fine) return delta;
        constexpr double grid = 0.25;
        return std::round(delta / grid) * grid;
    }

    bool isBlackKey(int pitch) const;

    int pointIndexAt(const MpeNote& note, juce::Point<float> pos) const;
    int tensionHandleAt(const MpeNote& note, juce::Point<float> pos) const;
    juce::Point<float> tensionHandlePos(const MpeNote& note, int i) const;
    bool ribbonHit(const MpeNote& note, juce::Point<float> pos) const;
    bool nearRightEdge(const MpeNote& note, juce::Point<float> pos) const;
    void applyTensionDrag(MpeNote& note, int leftIndex, juce::Point<float> pos) const;
    void buildNotePath(const MpeNote& note, juce::Path& path) const;
    juce::Range<float> pitchExtent(const MpeNote& note) const;

    // --- selection ---
    bool isSelected(const juce::Uuid& id) const;
    void selectOnly(const juce::Uuid& id);
    void toggleSelected(const juce::Uuid& id);
    void clearSelection();
    juce::Uuid soleSelection() const;      // the id iff exactly one note is selected
    void deleteSelected();
    void beginMoveNotes(juce::Point<float> pos, const std::vector<MpeNote>& snapshot);
    void updateMarquee(juce::Point<float> pos, const std::vector<MpeNote>& snapshot, bool additive);

    MpePianoRollAudioProcessor& processor;
    Tool tool = Tool::draw;

    std::vector<juce::Uuid> selection;

    DragMode dragMode = DragMode::none;
    juce::Uuid dragNoteId;
    int dragPointIndex = -1;

    juce::Point<float> marqueeA, marqueeB;
    std::vector<juce::Uuid> preMarqueeSelection;

    double dragAnchorBeat = 0.0;
    float dragAnchorPitch = 60.0f;
    struct NoteOrigin { juce::Uuid id; double startBeat = 0.0; int pitch = 60; };
    std::vector<NoteOrigin> dragOrigins;

    static constexpr int lowestPitch = 24;   // C1
    static constexpr int highestPitch = 96;  // C7
    static constexpr int rowHeight = 14;
    static constexpr float pointRadius = 4.0f;
};
