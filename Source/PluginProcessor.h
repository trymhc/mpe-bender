#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "NoteModel.h"
#include "MpeEngine.h"
#include "HostedPlugin.h"
#include <vector>

class MpePianoRollAudioProcessor final : public juce::AudioProcessor,
                                         private juce::AsyncUpdater
{
public:
    MpePianoRollAudioProcessor();
    ~MpePianoRollAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MPE Bender"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Hosted synth (Serum 2) ---
    HostedPlugin& getHostedPlugin() { return hostedPlugin; }

    // Message thread. Loads a .vst3 as the hosted synth. Returns "" on success or an
    // error message. Also stores the path so the project recalls it.
    juce::String loadHostedPlugin(const juce::File& vst3File);
    void unloadHostedPlugin();

    // Best guess at where Serum 2 is installed on this machine (may not exist).
    static juce::File findLikelySerumFile();

    // --- Thread-safe access to the note list for the editor ---
    template <typename Fn>
    void modifyNotes(Fn&& fn)
    {
        juce::ScopedLock sl(notesLock);
        fn(notes);
    }

    template <typename Fn>
    void readNotes(Fn&& fn) const
    {
        juce::ScopedLock sl(notesLock);
        fn(notes);
    }

    void setLoopLengthBeats(double beats);
    double getLoopLengthBeats() const { return loopLengthBeats; }

    void setPitchBendRangeSemitones(int semitones);
    int getPitchBendRangeSemitones() const { return engine.getPitchBendRangeSemitones(); }

    void setNumMemberChannels(int numChannels);
    int getNumMemberChannels() const { return engine.getNumMemberChannels(); }

    // Pass MIDI arriving from the host straight through to the hosted synth as well,
    // so you can still play Serum from a keyboard / host clip.
    void setForwardHostMidi(bool shouldForward) { forwardHostMidi = shouldForward; }
    bool getForwardHostMidi() const { return forwardHostMidi; }

    double getUiPlayheadBeat() const { return uiPlayheadBeat.load(std::memory_order_relaxed); }
    bool getUiIsPlaying() const { return uiIsPlaying.load(std::memory_order_relaxed); }

    enum class TransportStatus
    {
        noPlayHeadObject,
        noPositionInfo,
        stopped,
        missingTempoOrPpq,
        playing
    };

    juce::uint64 getUiProcessBlockCount() const { return uiProcessBlockCount.load(std::memory_order_relaxed); }
    TransportStatus getUiTransportStatus() const { return uiTransportStatus.load(std::memory_order_relaxed); }
    double getUiBpm() const { return uiBpm.load(std::memory_order_relaxed); }
    juce::uint64 getUiNoteOnCount() const { return engine.getNoteOnCount(); }
    juce::uint64 getUiNoteOffCount() const { return engine.getNoteOffCount(); }

private:
    void handleAsyncUpdate() override;   // performs a deferred hosted-plugin load (message thread)
    void hardResetPlayback(juce::MidiBuffer& midiBuffer);

    mutable juce::CriticalSection notesLock;
    std::vector<MpeNote> notes;
    MpeEngine engine;
    HostedPlugin hostedPlugin;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    double loopLengthBeats = 32.0;   // 8 bars of 4/4
    bool wasPlaying = false;
    bool zoneConfigSent = false;
    int configResends = 0;
    int configResendCountdown = 0;
    std::atomic<bool> forwardHostMidi { true };

    // Deferred hosted-plugin load, set by setStateInformation and consumed on the
    // message thread in handleAsyncUpdate().
    juce::CriticalSection pendingLock;
    juce::File pendingHostedFile;
    juce::MemoryBlock pendingHostedState;
    bool hasPendingHostedLoad = false;

    std::atomic<double> uiPlayheadBeat { 0.0 };
    std::atomic<bool> uiIsPlaying { false };

    std::atomic<juce::uint64> uiProcessBlockCount { 0 };
    std::atomic<TransportStatus> uiTransportStatus { TransportStatus::noPlayHeadObject };
    std::atomic<double> uiBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MpePianoRollAudioProcessor)
};
