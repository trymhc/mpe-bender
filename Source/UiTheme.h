#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Runtime-swappable palette. Call sites just read Theme::panel, Theme::note, ...
// The editor calls Theme::apply() when the user picks a theme on the Settings tab,
// then refreshes the LookAndFeel and repaints. The values below are the Light
// defaults so there is always a sane palette before apply() runs.
namespace Theme
{
    enum class Id { light = 0, graphite = 1, dark = 2 };

    inline juce::Colour panel      { 0xfffbfbfd };   // window background
    inline juce::Colour rowLight   { 0xffffffff };   // white-key row in the note area
    inline juce::Colour rowDark    { 0xfff1f1f4 };   // black-key row
    inline juce::Colour whiteKey   { 0xffffffff };
    inline juce::Colour blackKey   { 0xff3a3a3c };
    inline juce::Colour separator  { 0xffd2d2d7 };
    inline juce::Colour button     { 0xffffffff };
    inline juce::Colour buttonOn   { 0xff0071e3 };   // system blue
    inline juce::Colour field      { 0xffffffff };
    inline juce::Colour accent     { 0xff0071e3 };
    inline juce::Colour note       { 0xff8e8e93 };   // neutral grey, reads on white
    inline juce::Colour noteSel    { 0xff48484a };
    inline juce::Colour noteHot    { 0xff6e6e73 };
    inline juce::Colour handle     { 0xff2b2b2e };   // bend-point / knob body
    inline juce::Colour handleEdge { 0xffffffff };
    inline juce::Colour text       { 0xff1d1d1f };
    inline juce::Colour textDim    { 0xff6e6e73 };
    inline juce::Colour gridLine   { 0xff000000 };   // grid / hairline outlines, used at low alpha
    inline juce::Colour wheelSlice { 0xffeeeef0 };   // inactive shape-wheel wedge
    inline juce::Colour sliderThumb{ 0xffffffff };   // little slider/knob dots in the roll
    inline juce::Colour scrollThumb{ 0xffc2c2c8 };
    inline juce::Colour scrollTrack{ 0xffededf0 };

    inline Id current = Id::light;

    inline void apply(Id id)
    {
        current = id;

        // 22 entries, in declaration order:
        // panel, rowLight, rowDark, whiteKey, blackKey, separator, button, buttonOn,
        // field, accent, note, noteSel, noteHot, handle, handleEdge, text, textDim,
        // gridLine, wheelSlice, sliderThumb, scrollThumb, scrollTrack
        static constexpr juce::uint32 light[] = {
            0xfffbfbfd, 0xffffffff, 0xfff1f1f4, 0xffffffff, 0xff3a3a3c, 0xffd2d2d7,
            0xffffffff, 0xff0071e3, 0xffffffff, 0xff0071e3, 0xff8e8e93, 0xff48484a,
            0xff6e6e73, 0xff2b2b2e, 0xffffffff, 0xff1d1d1f, 0xff6e6e73, 0xff000000,
            0xffeeeef0, 0xffffffff, 0xffc2c2c8, 0xffededf0 };
        static constexpr juce::uint32 graphite[] = {
            0xff3d3d40, 0xff4a4a4e, 0xff424246, 0xffcfcfd3, 0xff313135, 0xff56565b,
            0xff4a4a4e, 0xff3a86ff, 0xff4a4a4e, 0xff5aa0ff, 0xffb8b8bd, 0xfff0f0f2,
            0xffd6d6da, 0xffececee, 0xff3d3d40, 0xfff2f2f4, 0xffb8b8bd, 0xffffffff,
            0xff55555b, 0xfff2f2f4, 0xff6f6f75, 0xff444448 };
        static constexpr juce::uint32 dark[] = {
            0xff1d1d1f, 0xff2b2b2e, 0xff242427, 0xffd8d8dc, 0xff2a2a2d, 0xff3a3a3e,
            0xff2c2c30, 0xff0a84ff, 0xff2c2c30, 0xff0a84ff, 0xff8e8e93, 0xffe4e4e6,
            0xffb6b6bb, 0xffe6e6e8, 0xff1d1d1f, 0xfff5f5f7, 0xff98989d, 0xffffffff,
            0xff333338, 0xfff5f5f7, 0xff5a5a5f, 0xff2a2a2d };

        const juce::uint32* p = id == Id::dark ? dark : id == Id::graphite ? graphite : light;
        juce::Colour* dst[] = {
            &panel, &rowLight, &rowDark, &whiteKey, &blackKey, &separator, &button,
            &buttonOn, &field, &accent, &note, &noteSel, &noteHot, &handle,
            &handleEdge, &text, &textDim, &gridLine, &wheelSlice, &sliderThumb,
            &scrollThumb, &scrollTrack };
        for (int i = 0; i < (int) (sizeof(dst) / sizeof(dst[0])); ++i)
            *dst[i] = juce::Colour(p[i]);
    }

    inline juce::String name(Id id)
    {
        return id == Id::dark ? "Dark" : id == Id::graphite ? "Graphite" : "Light";
    }
}

// Flat look: buttons/sliders/scrollbars pull straight from the Theme palette.
// Call syncColours() again whenever Theme::apply() changes the palette.
class FlatLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FlatLookAndFeel() { syncColours(); }

    void syncColours()
    {
        setColour(juce::TextButton::buttonColourId,   Theme::button);
        setColour(juce::TextButton::buttonOnColourId, Theme::buttonOn);
        setColour(juce::TextButton::textColourOffId,  Theme::text);
        setColour(juce::TextButton::textColourOnId,   juce::Colours::white);

        setColour(juce::Slider::trackColourId,           Theme::accent);
        setColour(juce::Slider::backgroundColourId,      Theme::separator);
        setColour(juce::Slider::thumbColourId,           Theme::sliderThumb);
        setColour(juce::Slider::textBoxTextColourId,     Theme::text);

        setColour(juce::ScrollBar::thumbColourId,      Theme::scrollThumb);
        setColour(juce::ScrollBar::trackColourId,      Theme::scrollTrack);
        setColour(juce::ScrollBar::backgroundColourId, Theme::panel);

        setColour(juce::ComboBox::backgroundColourId, Theme::button);
        setColour(juce::ComboBox::textColourId,       Theme::text);
        setColour(juce::ComboBox::outlineColourId,    Theme::separator);
        setColour(juce::ComboBox::arrowColourId,      Theme::textDim);
        setColour(juce::PopupMenu::backgroundColourId,        Theme::button);
        setColour(juce::PopupMenu::textColourId,              Theme::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour(juce::PopupMenu::highlightedTextColourId,   juce::Colours::white);

        setColour(juce::Label::textColourId, Theme::text);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        auto c = backgroundColour;
        if (down)             c = c.darker(0.06f);
        else if (highlighted) c = on ? c.brighter(0.10f) : c.contrasting(0.04f);

        g.setColour(c);
        g.fillRoundedRectangle(bounds, 4.0f);
        if (! on)
        {
            g.setColour(Theme::separator);
            g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
        }
    }

    int getSliderThumbRadius(juce::Slider& s) override
    {
        return juce::jmax(4, juce::jmin(6, s.getHeight() / 3));
    }
};
