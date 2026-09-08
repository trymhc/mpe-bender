#include "HostedPlugin.h"

HostedPlugin::HostedPlugin()
{
    // With JUCE_PLUGINHOST_VST3=1 this registers the VST3 format (and AU on macOS).
    // (JUCE 9 replaced AudioPluginFormatManager::addDefaultFormats() with this.)
    juce::addDefaultFormatsToManager (formatManager);
}

HostedPlugin::~HostedPlugin()
{
    unload();
}

juce::String HostedPlugin::load (const juce::File& vst3File, double sampleRate, int blockSize)
{
    return loadInternal (vst3File, sampleRate, blockSize, nullptr);
}

juce::String HostedPlugin::loadWithState (const juce::File& vst3File, double sampleRate, int blockSize,
                                          const juce::MemoryBlock& hostedState)
{
    return loadInternal (vst3File, sampleRate, blockSize, &hostedState);
}

juce::String HostedPlugin::loadInternal (const juce::File& vst3File, double sampleRate, int blockSize,
                                         const juce::MemoryBlock* stateToRestore)
{
    if (! vst3File.exists())
        return "File not found: " + vst3File.getFullPathName();

    if (sampleRate <= 0.0)  sampleRate = 44100.0;
    if (blockSize  <= 0)    blockSize  = 512;

    // Ask the VST3 format what plugin(s) live in this file.
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, vst3File.getFullPathName());

    if (descriptions.isEmpty())
        return "No VST3 plugin found in " + vst3File.getFileName();

    juce::String errorMessage;
    std::unique_ptr<juce::AudioPluginInstance> newInstance (
        formatManager.createPluginInstance (*descriptions.getFirst(), sampleRate, blockSize, errorMessage));

    if (newInstance == nullptr)
        return errorMessage.isNotEmpty() ? errorMessage
                                         : juce::String ("Could not create plugin instance");

    // We drive it as a synth: no audio input, stereo output.
    newInstance->enableAllBuses();
    newInstance->setPlayConfigDetails (newInstance->getTotalNumInputChannels(),
                                       juce::jmax (2, newInstance->getTotalNumOutputChannels()),
                                       sampleRate, blockSize);
    newInstance->prepareToPlay (sampleRate, blockSize);
    newInstance->setProcessingPrecision (juce::AudioProcessor::singlePrecision);

    if (stateToRestore != nullptr && stateToRestore->getSize() > 0)
        newInstance->setStateInformation (stateToRestore->getData(),
                                          (int) stateToRestore->getSize());

    const int scratchChannels = juce::jmax (2,
                                            newInstance->getTotalNumInputChannels(),
                                            newInstance->getTotalNumOutputChannels());

    {
        const juce::ScopedLock sl (lock);
        if (instance != nullptr)
        {
            instance->releaseResources();
            instance.reset();
        }
        instance = std::move (newInstance);
        currentFile   = vst3File;
        preparedBlock = blockSize;
        scratch.setSize (scratchChannels, blockSize, false, false, true);
        loaded.store (true, std::memory_order_relaxed);
    }

    return {};
}

void HostedPlugin::unload()
{
    const juce::ScopedLock sl (lock);
    loaded.store (false, std::memory_order_relaxed);
    if (instance != nullptr)
    {
        instance->releaseResources();
        instance.reset();
    }
    currentFile = juce::File();
}

void HostedPlugin::prepare (double sampleRate, int blockSize)
{
    const juce::ScopedLock sl (lock);
    preparedBlock = blockSize;

    if (instance != nullptr)
    {
        instance->releaseResources();
        instance->setPlayConfigDetails (instance->getTotalNumInputChannels(),
                                        juce::jmax (2, instance->getTotalNumOutputChannels()),
                                        sampleRate, blockSize);
        instance->prepareToPlay (sampleRate, blockSize);

        const int scratchChannels = juce::jmax (2,
                                                instance->getTotalNumInputChannels(),
                                                instance->getTotalNumOutputChannels());
        scratch.setSize (scratchChannels, blockSize, false, false, true);
    }
}

void HostedPlugin::releaseResources()
{
    const juce::ScopedLock sl (lock);
    if (instance != nullptr)
        instance->releaseResources();
}

void HostedPlugin::setPlayHead (juce::AudioPlayHead* playHead)
{
    const juce::ScopedLock sl (lock);
    if (instance != nullptr)
        instance->setPlayHead (playHead);
}

void HostedPlugin::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                            juce::AudioPlayHead* playHead)
{
    const juce::ScopedTryLock stl (lock);

    if (! stl.isLocked() || instance == nullptr)
    {
        buffer.clear();
        return;
    }

    instance->setPlayHead (playHead);

    const int numSamples = buffer.getNumSamples();

    if (scratch.getNumSamples() < numSamples
        || scratch.getNumChannels() < juce::jmax (2, instance->getTotalNumOutputChannels()))
    {
        scratch.setSize (juce::jmax (2, instance->getTotalNumInputChannels(),
                                        instance->getTotalNumOutputChannels()),
                         juce::jmax (numSamples, preparedBlock), false, false, true);
    }

    scratch.clear();

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                   scratch.getNumChannels(), numSamples);

    instance->processBlock (view, midi);

    buffer.clear();
    const int outChannels = juce::jmin (buffer.getNumChannels(), view.getNumChannels());
    for (int ch = 0; ch < outChannels; ++ch)
        buffer.copyFrom (ch, 0, view, ch, 0, numSamples);

    // Mono hosted output -> duplicate to both sides.
    if (view.getNumChannels() == 1 && buffer.getNumChannels() >= 2)
        buffer.copyFrom (1, 0, view, 0, 0, numSamples);
}

juce::String HostedPlugin::getDisplayName() const
{
    const juce::ScopedLock sl (lock);
    if (instance != nullptr)
        return instance->getName();
    return "No plugin loaded";
}

void HostedPlugin::getHostedState (juce::MemoryBlock& dest)
{
    const juce::ScopedLock sl (lock);
    dest.reset();
    if (instance != nullptr)
        instance->getStateInformation (dest);
}
