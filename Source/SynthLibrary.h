#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>

// A small named list of VST3 instrument paths, so you can bookmark "Serum 2",
// "Vital", ... once and switch between them from a menu instead of hunting for
// the .vst3 every time. Global (shared by every MPE Bender instance), stored at
// %APPDATA%\MPE Bender\synths.xml.
class SynthLibrary
{
public:
    struct Entry { juce::String name; juce::File file; };

    SynthLibrary() { load(); }

    const std::vector<Entry>& entries() const { return items; }

    void add(juce::String name, const juce::File& file)
    {
        name = name.trim();
        if (name.isEmpty() || file == juce::File())
            return;
        // drop any existing entry with the same name or the same path, then append
        items.erase(std::remove_if(items.begin(), items.end(), [&](const Entry& e)
                    { return e.name.equalsIgnoreCase(name) || e.file == file; }),
                    items.end());
        items.push_back({ name, file });
        save();
    }

    void rename(const juce::String& oldName, juce::String newName)
    {
        newName = newName.trim();
        if (newName.isEmpty())
            return;
        for (auto& e : items)
            if (e.name.equalsIgnoreCase(oldName)) { e.name = newName; save(); return; }
    }

    void removeByName(const juce::String& name)
    {
        items.erase(std::remove_if(items.begin(), items.end(), [&](const Entry& e)
                    { return e.name.equalsIgnoreCase(name); }), items.end());
        save();
    }

    const Entry* findByName(const juce::String& name) const
    {
        for (auto& e : items)
            if (e.name.equalsIgnoreCase(name)) return &e;
        return nullptr;
    }
    const Entry* findByFile(const juce::File& f) const
    {
        for (auto& e : items)
            if (e.file == f) return &e;
        return nullptr;
    }

    juce::String suggestName(const juce::File& f) const
    {
        const auto base = f.getFileNameWithoutExtension();
        if (findByName(base) == nullptr)
            return base;
        for (int i = 2; ; ++i)
        {
            const auto n = base + " " + juce::String(i);
            if (findByName(n) == nullptr) return n;
        }
    }

private:
    static juce::File storageFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("MPE Bender").getChildFile("synths.xml");
    }

    void load()
    {
        items.clear();
        if (auto xml = juce::XmlDocument::parse(storageFile()))
            for (auto* c : xml->getChildWithTagNameIterator("Synth"))
                items.push_back({ c->getStringAttribute("name"),
                                  juce::File(c->getStringAttribute("path")) });
    }

    void save() const
    {
        juce::XmlElement root("Synths");
        for (auto& e : items)
        {
            auto* c = root.createNewChildElement("Synth");
            c->setAttribute("name", e.name);
            c->setAttribute("path", e.file.getFullPathName());
        }
        storageFile().getParentDirectory().createDirectory();
        root.writeTo(storageFile());
    }

    std::vector<Entry> items;
};
