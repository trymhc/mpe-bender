#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Flat monochrome look: dark buttons, near-square corners, no outline.
class FlatLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FlatLookAndFeel()
    {
        setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1c1c1c));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3c3c3c));
        setColour(juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha(0.8f));
        setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto c = backgroundColour;

        if (down)             c = c.brighter(0.18f);
        else if (highlighted) c = c.brighter(0.08f);

        g.setColour(c);
        g.fillRoundedRectangle(bounds, 2.0f);
    }

    int getSliderThumbRadius(juce::Slider& s) override
    {
        return juce::jmax(4, juce::jmin(6, s.getHeight() / 3));
    }
};
