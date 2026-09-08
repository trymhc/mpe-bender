#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

// Wraps a single third-party plugin instance (intended: Serum 2, a VST3) that we
// load off disk and run *inside* this plugin. The MpeEngine's generated MPE MIDI is
// fed to process(); the hosted plugin's audio becomes our output.
//
// Threading: load()/unload()/prepare()/state calls happen on the message thread and
// take `lock`. process() runs on the audio thread and only *tries* the lock - if a
// load is in progress it outputs silence for that block rather than blocking.
class HostedPlugin
{
public:
    HostedPlugin();
    ~HostedPlugin();

    // Create the instance from a .vst3 file and prepare it. Blocking; message thread
    // only. Returns an error message, or an empty string on success.
    juce::String load (const juce::File& vst3File, double sampleRate, int blockSize);

    // As load(), but also restores the hosted plugin's patch state afterwards.
    // Used when recalling a saved project.
    juce::String loadWithState (const juce::File& vst3File, double sampleRate, int blockSize,
                                const juce::MemoryBlock& hostedState);

    void unload();

    // Re-prepare the current instance for a new sample rate / block size.
    void prepare (double sampleRate, int blockSize);
    void releaseResources();

    // Audio thread. `buffer` is our output buffer (already sized to our bus layout).
    // On return it holds the hosted plugin's first two output channels, or silence
    // if nothing is loaded / a load is in progress. `playHead` is forwarded to the
    // hosted plugin under the same non-blocking lock.
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                  juce::AudioPlayHead* playHead);

    // Message thread only (blocking lock).
    void setPlayHead (juce::AudioPlayHead* playHead);

    bool isLoaded() const noexcept   { return loaded.load (std::memory_order_relaxed); }
    juce::String getDisplayName() const;
    juce::File getFile() const        { return currentFile; }

    // Message thread. The hosted instance owns the returned editor - do not delete it
    // directly; see HostedPluginWindow for the correct teardown.
    juce::AudioPluginInstance* getInstance() const { return instance.get(); }

    // Serialises / restores the hosted plugin's own state (patch, macros, etc.).
    void getHostedState (juce::MemoryBlock& dest);

private:
    juce::String loadInternal (const juce::File& vst3File, double sampleRate, int blockSize,
                               const juce::MemoryBlock* stateToRestore);

    juce::AudioPluginFormatManager formatManager;
    juce::CriticalSection lock;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::File currentFile;

    int preparedBlock = 0;

    juce::AudioBuffer<float> scratch;
    std::atomic<bool> loaded { false };
};
