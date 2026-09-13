#pragma once

#include <juce_core/juce_core.h>

// UI preferences remembered globally, independent of any DAW project - so a
// brand-new instance (or the standalone app) opens with your last theme, scale,
// loop length etc. instead of the hard-coded defaults. Same idea as the
// remembered-synth path in PluginProcessor; one file at
// %APPDATA%\MPE Bender\prefs.xml. A saved DAW project's own state (if any)
// always wins over this once it's loaded.
struct GlobalPrefs
{
    int themeId = 0;
    int pitchBendRange = 48;
    int numMemberChannels = 15;
    bool forwardHostMidi = true;
    int scaleRoot = 0;
    int scaleType = 0;
    bool snapToScale = false;
    int gridDivision = 4;
    double loopLengthBeats = 32.0;
    juce::uint32 topBarColourArgb = 0;       // 0 = "not customised, use the theme default"
    juce::uint32 settingsBgColourArgb = 0;

    static juce::File storageFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("MPE Bender").getChildFile("prefs.xml");
    }

    static GlobalPrefs load()
    {
        GlobalPrefs p;
        if (auto xml = juce::XmlDocument::parse(storageFile()))
        {
            p.themeId = xml->getIntAttribute("themeId", p.themeId);
            p.pitchBendRange = xml->getIntAttribute("pitchBendRange", p.pitchBendRange);
            p.numMemberChannels = xml->getIntAttribute("numMemberChannels", p.numMemberChannels);
            p.forwardHostMidi = xml->getBoolAttribute("forwardHostMidi", p.forwardHostMidi);
            p.scaleRoot = xml->getIntAttribute("scaleRoot", p.scaleRoot);
            p.scaleType = xml->getIntAttribute("scaleType", p.scaleType);
            p.snapToScale = xml->getBoolAttribute("snapToScale", p.snapToScale);
            p.gridDivision = xml->getIntAttribute("gridDivision", p.gridDivision);
            p.loopLengthBeats = xml->getDoubleAttribute("loopLengthBeats", p.loopLengthBeats);
            p.topBarColourArgb = (juce::uint32) xml->getStringAttribute("topBarColour", "0").getLargeIntValue();
            p.settingsBgColourArgb = (juce::uint32) xml->getStringAttribute("settingsBgColour", "0").getLargeIntValue();
        }
        return p;
    }

    void save() const
    {
        juce::XmlElement xml("Prefs");
        xml.setAttribute("themeId", themeId);
        xml.setAttribute("pitchBendRange", pitchBendRange);
        xml.setAttribute("numMemberChannels", numMemberChannels);
        xml.setAttribute("forwardHostMidi", forwardHostMidi);
        xml.setAttribute("scaleRoot", scaleRoot);
        xml.setAttribute("scaleType", scaleType);
        xml.setAttribute("snapToScale", snapToScale);
        xml.setAttribute("gridDivision", gridDivision);
        xml.setAttribute("loopLengthBeats", loopLengthBeats);
        xml.setAttribute("topBarColour", juce::String((juce::int64) topBarColourArgb));
        xml.setAttribute("settingsBgColour", juce::String((juce::int64) settingsBgColourArgb));
        storageFile().getParentDirectory().createDirectory();
        xml.writeTo(storageFile());
    }
};
