#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Shared grey palette.
namespace Theme
{
    inline const juce::Colour panel      { 0xff3d3d3d };   // window / roll background
    inline const juce::Colour rowLight   { 0xff4a4a4a };   // white-key row in the note area
    inline const juce::Colour rowDark    { 0xff444444 };   // black-key row
    inline const juce::Colour whiteKey   { 0xffcfcfcf };
    inline const juce::Colour blackKey   { 0xff2c2c2c };
    inline const juce::Colour button     { 0xff353535 };
    inline const juce::Colour buttonOn   { 0xff585858 };
    inline const juce::Colour field      { 0xff333333 };   // combo / textbox fill
    inline const juce::Colour note       { 0xff33e0d4 };   // idle note
    inline const juce::Colour noteSel    { 0xff86f2ec };
    inline const juce::Colour noteHot    { 0xffcafffb };
    inline const juce::Colour text       { 0xffe8e8e8 };
}

// Flat monochrome look: dark buttons, near-square corners, no outline.
class FlatLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FlatLookAndFeel()
    {
        setColour(juce::TextButton::buttonColourId,   Theme::button);
        setColour(juce::TextButton::buttonOnColourId, Theme::buttonOn);
        setColour(juce::TextButton::textColourOffId,  Theme::text.withAlpha(0.8f));
        setColour(juce::TextButton::textColourOnId,   Theme::text);
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
