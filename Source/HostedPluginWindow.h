#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>

// A floating window that shows the hosted plugin's own editor (Serum 2's real UI).
// The hosted AudioPluginInstance creates and owns the editor lifecycle contract, so
// we must call editorBeingDeleted() before destroying it.
class HostedPluginWindow final : public juce::DocumentWindow
{
public:
    explicit HostedPluginWindow(juce::AudioPluginInstance& pluginToShow)
        : juce::DocumentWindow(pluginToShow.getName(),
                               juce::Colours::black,
                               juce::DocumentWindow::closeButton),
          plugin(pluginToShow)
    {
        setUsingNativeTitleBar(true);

        if (auto* realEditor = plugin.createEditorAndMakeActive())
        {
            editor.reset(realEditor);
            editorIsFromPlugin = true;
        }
        else
        {
            editor = std::make_unique<juce::GenericAudioProcessorEditor>(plugin);
            editorIsFromPlugin = false;
        }

        setContentNonOwned(editor.get(), true);
        setResizable(true, false);
        centreWithSize(juce::jmax(320, getWidth()), juce::jmax(240, getHeight()));
        setVisible(true);
    }

    ~HostedPluginWindow() override
    {
        clearContentComponent();

        if (editor != nullptr)
        {
            if (editorIsFromPlugin)
                plugin.editorBeingDeleted(editor.get());
            editor.reset();
        }
    }

    void closeButtonPressed() override
    {
        if (onRequestClose != nullptr)
            onRequestClose();
    }

    // The owner sets this to destroy the window (do not delete from inside the callback).
    std::function<void()> onRequestClose;

private:
    juce::AudioPluginInstance& plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool editorIsFromPlugin = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedPluginWindow)
};
