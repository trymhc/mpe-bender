#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Scale.h"
#include <vector>
#include <functional>

// The note grid. Each note is a ribbon: a piecewise-linear "chord" through its
// bend points, optionally with a sine / triangle wave riding on top.
//
// Draw tool: click empty = new note; drag body = move; drag right edge = length +
//   tail bend; Ctrl+click or double-click the ribbon = add a bend point; drag a
//   bend point = shape the chord; right-click = delete.
//   When one note is selected, the shape controls appear around it (a straight /
//   sine / triangle wheel, plus cycles / squeeze / start- and end-amplitude
//   sliders when the shape is a wave).
//   Hold Shift while dragging = temporarily the Select tool. Hold Alt = fine.
// Select tool: box-drag = marquee select (Shift adds); drag a selected note = move
//   the whole selection; drag a right edge = resize all selected; Delete removes.
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
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    static constexpr int keyboardWidth = 50;
    static constexpr int lowestPitch = 24;    // C1
    static constexpr int highestPitch = 96;   // C7

    float getRowHeight() const { return rowHeight; }
    bool isBlackKey(int pitch) const;

    // for the keyboard sidebar's scale highlighting
    int  scaleType() const { return processor.getScaleType(); }
    int  scaleRoot() const { return processor.getScaleRoot(); }
    bool scaleActive() const { return processor.getScaleType() != Scale::chromatic; }

    void zoomBoth(float factor, float anchorX, float anchorY);
    void resetZoom();
    void updateContentSize();

    bool hasSoleSelection() const { return selection.size() == 1; }

private:
    void timerCallback() override { repaint(); }

    enum class DragMode
    {
        none, marquee, moveNotes, movePoint, resizeEnds,
        shapeCycles, shapeSqueeze, shapeAmpStart, shapeAmpEnd
    };

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

    int maybeSnapPitch(int pitch) const
    {
        if (processor.getSnapToScale() && processor.getScaleType() != Scale::chromatic)
            return juce::jlimit(lowestPitch, highestPitch,
                                Scale::snap(processor.getScaleType(), processor.getScaleRoot(), pitch));
        return pitch;
    }

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
    void eraseNoteAt(juce::Point<float> contentPos);   // right-click / right-drag erase
    void toggleMuteSelected();
    void copySelection();
    void pasteClipboard();

    // Every note mutation goes through this so one gesture = one undo step.
    template <typename Fn>
    void editNotes(Fn&& fn)
    {
        if (! gestureStashValid)
        {
            preGestureNotes = processor.snapshotNotes();
            gestureStashValid = true;
        }
        processor.modifyNotes(std::forward<Fn>(fn));
        gestureDidEdit = true;
    }
    void beginGesture() { gestureStashValid = false; gestureDidEdit = false; }
    void endGesture()
    {
        if (gestureDidEdit && gestureStashValid)
            processor.commitUndo(std::move(preGestureNotes));
        gestureStashValid = false;
        gestureDidEdit = false;
    }
    void beginMoveNotes(juce::Point<float> pos, const std::vector<MpeNote>& snapshot);
    void updateMarquee(juce::Point<float> pos, const std::vector<MpeNote>& snapshot, bool additive);

    const MpeNote* findNote(const std::vector<MpeNote>& snap, const juce::Uuid& id) const;

    // The shape editor overlay drawn around the sole-selected note.
    struct ShapeUI
    {
        bool valid = false;
        juce::Uuid noteId;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // first / last bend point, screen space
        juce::Point<float> wheel;               // 3-slice shape selector centre
        float wheelR = 15.0f;
        bool wave = false;                      // shape != straight -> extra controls
        juce::Rectangle<float> cyclesTrack, squeezeTrack;
        juce::Point<float> cyclesH, squeezeH;
        juce::Point<float> ampStartKnob, ampEndKnob;
        float knobR = 9.0f;
    };
    ShapeUI shapeUIFor(const MpeNote& note) const;
    void setNoteShape(const juce::Uuid& id, BendShape s);
    int wheelSliceAt(const ShapeUI& s, juce::Point<float> pos) const;   // -1, 0=STR, 1=TRI, 2=SIN

    MpePianoRollAudioProcessor& processor;
    Tool tool = Tool::draw;

    std::vector<juce::Uuid> selection;
    std::vector<MpeNote> clipboard;                 // for Ctrl+C / Ctrl+V
    std::vector<MpeNote> preGestureNotes;           // undo snapshot for the current gesture
    bool gestureStashValid = false;
    bool gestureDidEdit = false;

    double hoverBeat = 0.0;
    float  hoverPitch = 60.0f;

    bool panning = false;                           // middle-drag scroll
    juce::Point<int> panStartView;
    juce::Point<float> panStartMouseScreen;

    bool rightErasing = false;                       // right-button held to erase notes

    DragMode dragMode = DragMode::none;
    juce::Uuid dragNoteId;
    int dragPointIndex = -1;
    float shapeGrabY = 0.0f, shapeGrabVal = 0.0f;   // for the amplitude knobs

    juce::Point<float> marqueeA, marqueeB;
    std::vector<juce::Uuid> preMarqueeSelection;
    bool marqueeAdditive = false;

    double dragAnchorBeat = 0.0;
    float dragAnchorPitch = 60.0f;
    struct NoteOrigin { juce::Uuid id; double startBeat = 0.0; int pitch = 60; };
    std::vector<NoteOrigin> dragOrigins;

    double lastNoteLength = 1.0;
    struct EndOrigin { juce::Uuid id; double lengthBeats = 1.0; float endValue = 0.0f; };
    std::vector<EndOrigin> endOrigins;

    static constexpr float pointRadius = 4.0f;

    static constexpr float defaultPixelsPerBeat = 80.0f;
    static constexpr float defaultRowHeight = 14.0f;

    float pixelsPerBeat = defaultPixelsPerBeat;
    float rowHeight = defaultRowHeight;
};
