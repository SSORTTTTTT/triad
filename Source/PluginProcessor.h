#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioEngine.h"
#include "Modulation.h"
#include "TriadIdentity.h"

// Triad: a multiband effects host. The signal is split into 1-10 bands; each band runs a chain of
// hosted effects (VST3, and AU on macOS) in order, like an FL Studio mixer insert. A band starts
// with one slot and can grow to as many slots as needed. Every slot has its own dry/wet mix, each
// band has a mix and a volume, and LFOs drawn on a grid can move any of these (the matrix).
//
// Threading: bands, slots, LFO lists and routes are only restructured on the message thread, while
// holding `gate`. The audio thread only try-locks `gate` (it outputs silence for a block rather than
// wait), so the message thread may read the structure without locking. LFO shapes, amounts and
// mixes are exchanged through atomics, so drawing an LFO never interrupts the audio.
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
    static juce::String bandGainID (int band);             // "gain0".."gain9" (dB)
    static constexpr float minGainDb = -36.0f, maxGainDb = 12.0f;

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
    // Moves a band's whole chain (effects, LFOs, mix, volume) to another position; the frequency
    // ranges stay where they are and the bands in between shift over.
    bool moveBand (int from, int to);

    // ---- Slots (message thread) -------------------------------------------------------------
    struct SlotInfo
    {
        juce::String name, status, format, maker;
        bool loaded = false, hasContent = false, enabled = true, pending = false, modulated = false;
        int latency = 0;
        int channels = 2;                        // channels the effect processes (more than 2: an extra bus fed silence)
        int invalidAudio = 0;                    // blocks of NaN/Inf output Triad silenced
        float mix = 100.0f, liveMix = 100.0f;   // knob, and the value playing now (with LFOs)
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
    // Third-party effects kept loaded, idle, after their last copy was removed (one per effect, for the
    // session): deleting a plug-in's last copy makes it shut down, which crashes some plug-ins.
    static int getIdleEffectCount();
    // Hosts an effect instance that was created elsewhere (used by the tests).
    juce::String loadInstance (int band, int slot, std::unique_ptr<juce::AudioPluginInstance>);
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

    // ---- LFOs (message thread) ------------------------------------------------------------------
    // Every band has an LFO for its Band Mix and one for each slot's mix, already routed (and
    // silent until the shape is first edited); more LFOs can be added and routed in the matrix.
    struct LfoInfo
    {
        int id = 0;
        juce::String name, target;   // "LFO 2", "KHS Distortion"
        int length = 0;              // index into triad::mod::lengths()
        triad::mod::Shape shape;
        bool active = false;         // at least one route from it is on
        bool bandLfo = false;
        int slot = -1;               // the slot it was made for, or -1
        bool canUndo = false, canRedo = false;
    };
    int getLfoCount (int band) const;
    LfoInfo getLfoInfo (int band, int lfo) const;
    int getSlotLfo (int band, int slot) const;
    int getBandLfo (int band) const;
    void beginLfoEdit (int band, int lfo);                       // remembers the shape for undo
    void setLfoShape (int band, int lfo, const triad::mod::Shape&, bool undoable);
    bool undoLfo (int band, int lfo);
    bool redoLfo (int band, int lfo);
    void setLfoLength (int band, int lfo, int lengthIndex);
    int addLfo (int band);
    bool removeLfo (int band, int lfo);                          // extra LFOs only
    float getLfoPhase (int band, int lfo) const;                 // 0..1, or < 0 when not running

    // ---- Matrix (message thread) -----------------------------------------------------------------
    struct Destination
    {
        enum Kind { slotMix = 0, bandMix = 1, bandVolume = 2 };
        int kind = bandMix;
        int slotId = 0;
        bool operator== (const Destination& o) const { return kind == o.kind && (kind != slotMix || slotId == o.slotId); }
    };
    struct RouteInfo
    {
        int lfo = -1;
        Destination destination;
        float amount = 1.0f;
        bool invert = false, muted = true;
        juce::String sourceName, destinationName;
    };
    int getRouteCount (int band) const;
    RouteInfo getRouteInfo (int band, int route) const;
    std::vector<int> getRoutesFrom (int band, int lfo) const;
    int addRoute (int band, int lfo, Destination);
    void setRouteSource (int band, int route, int lfo);
    void setRouteDestination (int band, int route, Destination);
    void setRouteAmount (int band, int route, float amount);    // 0..1
    void setRouteInvert (int band, int route, bool);
    void setRouteMuted (int band, int route, bool);
    void removeRoute (int band, int route);
    std::vector<std::pair<Destination, juce::String>> getDestinations (int band) const;

    // ---- Meters ------------------------------------------------------------------------------------
    struct Meters { float input = 0.0f, output = 0.0f; std::array<float, triad::maxBands> bands {}; };
    Meters takeMeters();   // peak levels (linear) since the last call

    // ---- Presets -----------------------------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> createChainXml (int band);     // one band's chain, LFOs, mix, volume
    juce::String loadChainXml (int band, const juce::XmlElement&);
    std::unique_ptr<juce::XmlElement> createPresetXml();             // the whole Triad
    void loadPresetXml (const juce::XmlElement&);
    juce::String getPresetName() const;
    void setPresetName (const juce::String&);

    triad::SpectrumTap spectrum;

private:
    struct Lfo
    {
        Lfo() { table.render (shape); }
        int id = 0, slotId = 0;          // slotId: the slot this LFO was made for (0 = none)
        bool bandLfo = false, touched = false;
        triad::mod::Shape shape { triad::mod::flat() };
        triad::mod::Table table;
        std::atomic<int> length { triad::mod::defaultLength() };
        std::vector<triad::mod::Shape> undo, redo;
        // Audio thread.
        double runPhase = 0.0, blockPhase = 0.0, increment = 0.0;
        std::atomic<float> displayPhase { -1.0f };
        float value (int frame) const noexcept { return table.lookup (blockPhase + increment * frame); }
    };
    struct Route
    {
        int lfoId = 0;
        Destination destination;
        std::atomic<float> amount { 1.0f };
        std::atomic<bool> invert { false }, muted { true };
        bool automatic = false;          // made by Triad; switches on when its LFO is first drawn
        Lfo* lfo = nullptr;              // resolved under gate
        float value (int frame) const noexcept { const float v = lfo->value (frame); return invert.load (std::memory_order_relaxed) ? 1.0f - v : v; }
    };
    struct Slot
    {
        int id = 0;
        std::unique_ptr<juce::AudioPluginInstance> effect;
        std::unique_ptr<juce::DocumentWindow> window;
        juce::PluginDescription description;
        juce::MemoryBlock savedState;
        juce::String error;
        bool enabled = true;
        int latency = 0;            // latency compensated for this slot (0 when inactive)
        int channels = 2;           // channels the effect processes: stereo, plus any always-on extra bus (fed silence)
        // Blocks of invalid audio (NaN/Inf) the effect produced; Triad silences them, then resets or bypasses it.
        std::atomic<int> invalidBlocks { 0 };
        int invalidHandled = 0, invalidResets = 0;
        juce::int64 pendingRequest = 0;
        std::atomic<float> mixPercent { 100.0f };
        std::atomic<float> liveMix { 100.0f };
        std::vector<Route*> modulators;   // resolved under gate
        triad::StereoDelay dryDelay;
        juce::SmoothedValue<float> mix;
        bool isActive() const { return effect != nullptr && enabled && error.isEmpty(); }
        bool hasContent() const { return effect != nullptr || description.name.isNotEmpty(); }
    };
    struct Band
    {
        explicit Band (bool withSlot = true);
        std::vector<std::unique_ptr<Slot>> slots;
        std::vector<std::unique_ptr<Lfo>> lfos;
        std::vector<std::unique_ptr<Route>> routes;
        std::vector<Route*> mixModulators, volumeModulators;
        int nextSlotId = 1, nextLfoId = 1;
        int chainLatency = 0;
        triad::StereoDelay dryDelay, alignDelay;
        juce::SmoothedValue<float> mix, gain;
        juce::AudioBuffer<float> dry, slotDry;
        Slot& appendSlot();
        Lfo& appendLfo (int slotId, bool bandLfo);
        void ensureModulation();     // band LFO, and an LFO + route for every slot
        void relink();               // resolve routes; drop routes whose LFO or slot is gone
        Lfo* findLfo (int id) const;
        Slot* findSlot (int id) const;
        int lfoIndex (int id) const;
        int slotIndex (int id) const;
    };
    struct PendingLoad { int slot; const juce::XmlElement* node; };

    juce::AudioPluginFormatManager formats;
    std::array<std::unique_ptr<Band>, triad::maxBands> bands;
    // Removed effects and closed effect windows (also those of a closed Triad) are deleted a moment
    // later; see EffectGraveyard.
    static void retire (std::unique_ptr<juce::AudioPluginInstance>);
    static void retire (std::unique_ptr<juce::DocumentWindow>);   // hidden now, deleted with the same delay
    void handleInvalidAudio();   // message thread: reset (or bypass) effects that produced NaN/Inf
    std::array<juce::AudioBuffer<float>, triad::maxBands> split;
    juce::AudioBuffer<float> wide;   // for effects with an extra bus that cannot be switched off (e.g. a sidechain)
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
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::array<std::atomic<float>, triad::maxBands> bandPeaks {};
    juce::int64 nextRequest = 1;
    int numBands = 3;               // guarded by gate; bandCount mirrors it for other threads
    int capacity = 512;
    int fadeRemaining = 0, fadeLength = 1;
    bool prepared = false;
    juce::String status, presetName;
    std::array<std::atomic<float>*, triad::maxCrossovers> crossoverValues {};
    std::array<std::atomic<float>*, triad::maxBands> bandMixValues {}, bandGainValues {};
    // Song position for the LFOs, read at the start of each block (audio thread).
    double blockPpq = 0.0, blockBpm = 120.0, blockBeatsPerBar = 4.0;
    bool blockPlaying = false;

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void alignLatency();
    void publishInfo();   // call with gate held
    void prepareBand (int index, bool prepareEffects);
    void prepareSlot (Slot&);
    void readCrossovers (float* dest) const;
    void startFadeIn();
    void resetDelays();
    Slot* slotAt (int band, int slot) const;
    Lfo* lfoAt (int band, int lfo) const;
    Route* routeAt (int band, int route) const;
    juce::String finishInstall (int band, int slot, const juce::PluginDescription&, const juce::MemoryBlock& state,
                                std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error, bool openEditor);
    juce::String requestInstall (int band, int slot, const juce::PluginDescription&, const juce::MemoryBlock& state, bool openEditor, bool restoringSlot);
    void installFromXml (int band, int slot, const juce::XmlElement& slotNode, bool restoringSlot);
    bool findPending (juce::int64 request, int& band, int& slot) const;
    void readSongPosition();
    void advanceLfos (Band&, int offset, int frames);
    void process (juce::AudioBuffer<float>&);
    void setParameterValue (const juce::String& id, float plainValue);
    float getParameterValue (const juce::String& id) const;
    void refreshWindowTitles();
    void restoreFrom (const juce::XmlElement&);
    void writeBand (juce::XmlElement&, const Band&) const;
    std::unique_ptr<Band> readBand (const juce::XmlElement* node, int version, const std::function<float (int)>& legacyMix,
                                    std::vector<PendingLoad>& loads) const;
    juce::String destinationName (const Band&, const Destination&) const;
    void stateChanged();
    bool sendKeyToHost();

    JUCE_DECLARE_WEAK_REFERENCEABLE (TriadProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadProcessor)
};
