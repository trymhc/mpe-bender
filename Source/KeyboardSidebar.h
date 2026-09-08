#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PianoRollComponent.h"
#include "UiTheme.h"

// The piano-key column. Lives OUTSIDE the roll's viewport (overlaid on its left
// edge) so it stays put while the roll scrolls horizontally; it follows the
// roll's vertical scroll and zoom.
class KeyboardSidebar final : public juce::Component, private juce::Timer
{
public:
    KeyboardSidebar(PianoRollComponent& rollToUse, juce::Viewport& viewportToUse)
        : roll(rollToUse), viewport(viewportToUse)
    {
        setInterceptsMouseClicks(true, false);   // swallow clicks so they don't hit the roll
        startTimerHz(30);
    }
    ~KeyboardSidebar() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        const float rh = roll.getRowHeight();
        const float scrollY = (float) viewport.getViewPositionY();

        g.fillAll(Theme::panel);

        for (int pitch = PianoRollComponent::lowestPitch; pitch <= PianoRollComponent::highestPitch; ++pitch)
        {
            const float yTop = (float) (PianoRollComponent::highestPitch - pitch) * rh - scrollY;
            if (yTop > (float) getHeight() || yTop + rh < 0.0f)
                continue;

            const bool black = roll.isBlackKey(pitch);
            g.setColour(black ? Theme::blackKey : Theme::whiteKey);
            g.fillRect(0.0f, yTop, (float) getWidth(), rh);
            g.setColour(juce::Colours::black.withAlpha(pitch % 12 == 0 ? 0.28f : 0.14f));
            g.drawHorizontalLine((int) yTop, 0.0f, (float) getWidth());

            if (pitch % 12 == 0)
            {
                g.setColour(juce::Colour(0xff202020));
                g.setFont(9.0f);
                g.drawText("C" + juce::String(pitch / 12 - 1), 2, (int) yTop, getWidth() - 4, (int) rh,
                           juce::Justification::centredLeft);
            }
        }

        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawVerticalLine(getWidth() - 1, 0.0f, (float) getHeight());
    }

private:
    void timerCallback() override { repaint(); }

    PianoRollComponent& roll;
    juce::Viewport& viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardSidebar)
};
