#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioEngine.h"
#include "TriadIdentity.h"

// Triad: a multiband effects host. The signal is split into 1-10 bands; each band runs a chain of
// hosted effects (VST3, and AU on macOS) in order, like an FL Studio mixer insert. A band starts
// with one slot and can grow to as many slots as needed. Every slot has its own dry/wet mix and
// each band has an overall mix.
//
// Threading: bands and slot lists are only ever restructured on the message thread, while holding
// `gate`. The audio thread only try-locks `gate` (it outputs silence for a block rather than wait),
// so the message thread may read the structure without locking.
class TriadProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater,
                             private juce::Timer
{
public:
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    TriadProcessor();
    ~TriadProcessor() override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return TRIAD_PRODUCT_NAME; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return tail.load(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    static juce::String crossoverID (int index);           // "lowCut", "highCut", "cross3".."cross9"
    static juce::String bandMixID (int band);              // "mix0".."mix9"

    // ---- Bands (positional, low to high) -------------------------------------------------
    int getBandCount() const noexcept { return bandCount.load(); }
    int getStructureVersion() const noexcept { return structureVersion.load(); }
    std::vector<float> effectiveCrossovers() const;          // numBands - 1 values, ascending
    std::pair<float, float> effectiveCutoffs() const;        // first two crossovers (0.1 API)
    float nyquistFrequency() const { return (float) rate.load() * 0.5f; }
    double getSampleRateForDisplay() const { return rate.load(); }
    juce::RangedAudioParameter* crossoverParameter (int index) const;
    static float defaultCrossover (int index, int bandCount);
    // Adds a band by splitting the band containing `hz` there (or the widest band if hz <= 0).
    bool addBand (float hz = -1.0f);
    bool removeBand (int band);

    // ---- Slots (message thread) -------------------------------------------------------------
    struct SlotInfo
    {
        juce::String name, status, format;
        bool loaded = false, hasContent = false, enabled = true, pending = false;
        int latency = 0;
        float mix = 100.0f;
    };
    int getSlotCount (int band) const;
    SlotInfo getSlotInfo (int band, int slot) const;
    SlotInfo getSlotInfo (int band) const { return getSlotInfo (band, 0); }   // 0.1 API: first slot
    int addSlot (int band);                     // appends an empty slot; returns its index
    void removeSlot (int band, int slot);       // removes the effect and its row (a band keeps one slot)
    // Loads synchronously when the format allows it, otherwise asynchronously (AUv3).
    // `slot` may equal getSlotCount (band): the slot is then appended.
    // Returns an error message, or an empty string on success / when an async load started.
    juce::String loadEffect (int band, int slot, const juce::PluginDescription&, bool openEditor);
    // Loads from a bundle/file; `preferredName` picks one plug-in from a multi-plug-in bundle.
    juce::String loadFile (int band, int slot, const juce::File&, bool openEditor = false, const juce::String& preferredName = {});
    juce::String loadFile (int band, const juce::File& file) { return loadFile (band, 0, file, false); }
    void removeEffect (int band, int slot);     // empties the slot but keeps its row
    void removeEffect (int band) { removeEffect (band, 0); }
    void setSlotEnabled (int band, int slot, bool);
    void setSlotMix (int band, int slot, float percent);
    float getSlotMix (int band, int slot) const;
    bool moveSlot (int band, int slot, int direction);   // -1 = up (earlier), +1 = down (later)
    void showEffect (int band, int slot);
    void showEffect (int band) { showEffect (band, 0); }
    void closeWindows();
    juce::String getStatus() const;
    void setStatus (const juce::String&);

    triad::SpectrumTap spectrum;

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> effect;
        std::unique_ptr<juce::DocumentWindow> window;
        juce::PluginDescription description;
        juce::MemoryBlock savedState;
        juce::String error;
        bool enabled = true;
        int latency = 0;            // latency compensated for this slot (0 when inactive)
        juce::int64 pendingRequest = 0;
        std::atomic<float> mixPercent { 100.0f };
        triad::StereoDelay dryDelay;
        juce::SmoothedValue<float> mix;
        bool isActive() const { return effect != nullptr && enabled && error.isEmpty(); }
        bool hasContent() const { return effect != nullptr || description.name.isNotEmpty(); }
    };
    struct Band
    {
        Band() { slots.push_back (std::make_unique<Slot>()); }
        std::vector<std::unique_ptr<Slot>> slots;
        int chainLatency = 0;
        triad::StereoDelay dryDelay, alignDelay;
        juce::SmoothedValue<float> mix;
        juce::AudioBuffer<float> dry, slotDry;
    };

    juce::AudioPluginFormatManager formats;
    std::array<std::unique_ptr<Band>, triad::maxBands> bands;
    std::array<juce::AudioBuffer<float>, triad::maxBands> split;
    triad::Splitter splitter;
    triad::StereoDelay bypassDelay;
    juce::MidiBuffer childMidi;
    // gate: band/slot structure, shared with the audio thread (which only ever try-locks it).
    // infoLock: the editor's snapshot and status text; never taken by the audio thread.
    mutable juce::CriticalSection gate, stateGate, infoLock;
    std::array<std::vector<SlotInfo>, triad::maxBands> info;
    std::unique_ptr<juce::XmlElement> pendingState;
    std::atomic<bool> restoring { false }, muted { false };
    std::atomic<int> pendingCreations { 0 }, bandCount { 3 }, structureVersion { 0 };
    std::atomic<double> tail { 0.0 };
    std::atomic<double> rate { 44100.0 };
    std::atomic<int> totalLatency { 0 };
    juce::int64 nextRequest = 1;
    int numBands = 3;               // guarded by gate; bandCount mirrors it for other threads
    int capacity = 512;
    int fadeRemaining = 0, fadeLength = 1;
    bool prepared = false;
    juce::String status;
    std::array<std::atomic<float>*, triad::maxCrossovers> crossoverValues {};
    std::array<std::atomic<float>*, triad::maxBands> bandMixValues {};

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void alignLatency();
    void publishInfo();   // call with gate held
    void prepareBand (int index, bool prepareEffects);
    void prepareSlot (Slot&);
    void readCrossovers (float* dest) const;
    void startFadeIn();
    Slot* slotAt (int band, int slot) const;
    juce::String finishInstall (int band, int slot, const juce::PluginDescription&, const juce::MemoryBlock& state,
                                std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error, bool openEditor);
    juce::String requestInstall (int band, int slot, const juce::PluginDescription&, const juce::MemoryBlock& state, bool openEditor, bool restoringSlot);
    bool findPending (juce::int64 request, int& band, int& slot) const;
    void process (juce::AudioBuffer<float>&);
    void setParameterValue (const juce::String& id, float plainValue);
    float getParameterValue (const juce::String& id) const;
    void refreshWindowTitles();
    void restoreFrom (const juce::XmlElement&);
    void stateChanged();

    JUCE_DECLARE_WEAK_REFERENCEABLE (TriadProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadProcessor)
};
