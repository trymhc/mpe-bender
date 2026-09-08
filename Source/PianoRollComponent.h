#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <vector>
#include <functional>

// The note grid. Each note is a ribbon following its own pitch curve (base key +
// bend points; extra "shaper" diamonds bow the curve between bend points).
//
// Draw tool: click empty = new note; drag body = move; drag right edge = length;
//   double-click ribbon = add a curve diamond; Ctrl+click ribbon = add a bend
//   point; drag any point/diamond = shape; right-click = delete.
//   Hold Shift while dragging = temporarily the Select tool (springs back on release).
//   Hold Alt while dragging = fine / no snap.
// Select tool: box-drag = marquee select (Shift adds); drag a selected note = move
//   the whole selection; Delete removes; Esc clears.
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

    enum class DragMode { none, marquee, moveNotes, movePoint, resizeEnds };

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
    bool ribbonHit(const MpeNote& note, juce::Point<float> pos) const;
    bool nearRightEdge(const MpeNote& note, juce::Point<float> pos) const;
    void buildNotePath(const MpeNote& note, juce::Path& path) const;
    juce::Range<float> pitchExtent(const MpeNote& note) const;

    bool isSelected(const juce::Uuid& id) const;
    void selectOnly(const juce::Uuid& id);
    void toggleSelected(const juce::Uuid& id);
    void clearSelection();
    juce::Uuid soleSelection() const;
    void deleteSelected();
    void beginMoveNotes(juce::Point<float> pos, const std::vector<MpeNote>& snapshot);
    void updateMarquee(juce::Point<float> pos, const std::vector<MpeNote>& snapshot, bool additive);

    MpePianoRollAudioProcessor& processor;
    Tool tool = Tool::draw;

    std::vector<juce::Uuid> selection;

    DragMode dragMode = DragMode::none;
    juce::Uuid dragNoteId;
    int dragPointIndex = -1;
    bool dragPointIsAnchor = true;

    juce::Point<float> marqueeA, marqueeB;
    std::vector<juce::Uuid> preMarqueeSelection;
    bool marqueeAdditive = false;

    double dragAnchorBeat = 0.0;
    float dragAnchorPitch = 60.0f;
    struct NoteOrigin { juce::Uuid id; double startBeat = 0.0; int pitch = 60; };
    std::vector<NoteOrigin> dragOrigins;
    struct EndOrigin { juce::Uuid id; double lengthBeats = 1.0; float endValue = 0.0f; };
    std::vector<EndOrigin> endOrigins;

    static constexpr int lowestPitch = 24;   // C1
    static constexpr int highestPitch = 96;  // C7
    static constexpr int rowHeight = 14;
    static constexpr float pointRadius = 4.0f;
};
