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

    // The VST3 synth to auto-load on a fresh instance: the last one you loaded,
    // else Serum 2 if it can be found. loadHostedPlugin() records the choice.
    static juce::File rememberedSynthFile();
    static void rememberSynthFile(const juce::File&);

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

    // --- Undo / redo (snapshot-based; the editor drives it) ---
    std::vector<MpeNote> snapshotNotes() const
    {
        juce::ScopedLock sl(notesLock);
        return notes;
    }
    void commitUndo(std::vector<MpeNote> before);   // push a pre-edit snapshot, clears redo
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

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

    // Free run: when true the piano-roll loop always plays with the transport.
    // When false (default) it only plays while the host is sending it note(s) -
    // so a disabled / empty channel in the DAW stays silent.
    void setFreeRun(bool shouldFreeRun) { freeRun = shouldFreeRun; }
    bool getFreeRun() const { return freeRun; }
    bool getUiGateOpen() const { return uiGateOpen.load(std::memory_order_relaxed); }

    // UI theme index (see Theme::Id). Stored with the project; the editor applies it.
    void setThemeId(int id) { themeId = id; }
    int getThemeId() const { return themeId; }

    // --- Scale viewer (stored with the project) ---
    void setScaleRoot(int r)   { scaleRoot = ((r % 12) + 12) % 12; }
    void setScaleType(int t)   { scaleType = t; }
    void setSnapToScale(bool s){ snapToScale = s; }
    int  getScaleRoot() const  { return scaleRoot; }
    int  getScaleType() const  { return scaleType; }
    bool getSnapToScale() const { return snapToScale; }

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
    std::atomic<bool> freeRun { false };
    int heldHostNotes = 0;          // audio thread only
    bool gateWasOpen = false;       // audio thread only
    std::atomic<bool> uiGateOpen { false };
    int themeId = 0;   // Theme::Id::light
    bool pendingAutoLoad = true;   // message-thread only; see handleAsyncUpdate

    int scaleRoot = 0;         // 0 = C
    int scaleType = 0;         // Scale::chromatic (viewer off)
    bool snapToScale = false;

    // undo/redo snapshots (message-thread only)
    std::vector<std::vector<MpeNote>> undoStack, redoStack;
    static constexpr size_t maxUndo = 128;
    std::atomic<bool> pendingHardReset { false };   // set by undo/redo, consumed in processBlock

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
