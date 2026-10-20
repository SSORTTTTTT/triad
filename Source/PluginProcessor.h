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
    static juce::String bandMuteID (int band);             // "mute0".."mute9" (0.8.0)
    bool isBandMuted (int band) const;
    void setBandMuted (int band, bool);                    // as a gesture: an undo step, automation follows
    static juce::String slopeID (int index);               // "slope0".."slope8": crossover slope order 1-16 (x 6 dB/oct)
    static constexpr float minGainDb = -36.0f, maxGainDb = 12.0f;

    // ---- Bands (positional, low to high) -------------------------------------------------
    int getBandCount() const noexcept { return bandCount.load(); }
    // The PRE and POST chains: slots that process the full-range sound before it is split into bands (pitch
    // correction, a de-esser, a gate...) and after the bands come back together (a limiter, glue compression...).
    // Every slot, LFO and link function takes them as bands `preBand` and `postBand`.
    static constexpr int preBand = triad::maxBands, postBand = triad::maxBands + 1;
    static constexpr bool isExtraChain (int band) noexcept { return band == preBand || band == postBand; }
    bool isValidBand (int band) const noexcept { return (band >= 0 && band < getBandCount()) || isExtraChain (band); }
    bool hasChainEffects (int band) const;                    // any slot of that band (PRE, POST) holds an effect
    bool hasPreEffects() const { return hasChainEffects (preBand); }
    bool hasPostEffects() const { return hasChainEffects (postBand); }
    int getPreLatencySamples() const noexcept { return preLatency.load(); }
    int getPostLatencySamples() const noexcept { return postLatency.load(); }
    static juce::String chainName (int band);                 // "PRE", "POST", "band 3"
    int getStructureVersion() const noexcept { return structureVersion.load(); }
    std::vector<float> effectiveCrossovers() const;          // numBands - 1 values, ascending
    std::pair<float, float> effectiveCutoffs() const;        // first two crossovers (0.1 API)
    float nyquistFrequency() const { return (float) rate.load() * 0.5f; }
    double getSampleRateForDisplay() const { return rate.load(); }
    juce::RangedAudioParameter* crossoverParameter (int index) const;
    // Crossover slopes: order 1 (6 dB/oct, soft) to 16 (96 dB/oct, near brickwall); 4 (24 dB/oct) by default.
    juce::RangedAudioParameter* slopeParameter (int index) const;
    int getCrossoverSlope (int index) const;                // order 1-16
    void setCrossoverSlope (int index, int order);          // as one undoable, automatable gesture
    void setAllCrossoverSlopes (int order);                 // every divider at once (e.g. all hard wall, 96 dB/oct)
    static juce::String slopeText (int order) { return juce::String (triad::CrossoverFilter::dbPerOctave (order)) + " dB/oct"; }
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
        int latency = 0;                         // compensated now (0 when bypassed, or switched off by LIVE)
        int reportedLatency = 0;                 // what the effect reports, whether it plays or not
        bool liveBypassed = false;               // switched off by LIVE because it adds latency
        int channels = 2;                        // channels the effect processes (more than 2: an extra bus fed silence)
        int invalidAudio = 0;                    // blocks of NaN/Inf output Triad silenced
        float mix = 100.0f, liveMix = 100.0f;   // knob, and the value playing now (with LFOs)
    };
    int getSlotCount (int band) const;
    SlotInfo getSlotInfo (int band, int slot) const;
    SlotInfo getSlotInfo (int band) const { return getSlotInfo (band, 0); }   // 0.1 API: first slot
    // The effect hosted in a slot (message thread; valid until the slot changes). For the parameter-linking
    // feasibility test: LFOs on the knobs inside third-party effects.
    juce::AudioPluginInstance* getHostedEffect (int band, int slot) const;
    // Message thread, every frame: where an LFO has a slot's mix right now (cheap; no strings copied).
    float getSlotLiveMix (int band, int slot, bool& modulated) const;
    int addSlot (int band);                     // appends an empty slot; returns its index
    void removeSlot (int band, int slot);       // removes the effect and its row (a band keeps one slot)
    // Loads synchronously when the format allows it, otherwise asynchronously (AUv3).
    // `slot` may equal getSlotCount (band): the slot is then appended.
    // Returns an error message, or an empty string on success / when an async load started.
    juce::String loadEffect (int band, int slot, const juce::PluginDescription&, bool openEditor);
    // Third-party effects kept loaded, idle, after their last copy was removed (one per effect, for the
    // session): deleting a plug-in's last copy makes it shut down, which crashes some plug-ins.
    static int getIdleEffectCount();
    // Removed effects waiting to be deleted (each goes 1.5 s after it was removed).
    static int getRetiredEffectCount();
    // Of those, the ones still there 3 s or more after their time (deletion falling behind).
    static int getLateRetiredEffectCount();
    // The last effects and windows deleted, newest first, written into `out` (no allocation: a crash handler can call it).
    static void describeRecentDeletions (char* out, size_t size);
    // Audio blocks Triad output as silence because the message thread was changing the bands at that
    // moment (Triad never waits on the audio thread); a dropout. For the live-monitoring test and meters.
    int getSilencedBlockCount() const noexcept { return silencedBlocks.load(); }
    // Deletes removed effects and the idle copies right away (tests; a host would just unload Triad).
    static void releaseIdleEffects();
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
    bool moveSlotTo (int band, int from, int to);         // drag and drop: the slots in between shift over
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
        // effectParameter: a knob inside the effect in slot `slotId` (its index in the effect's parameter
        // list, and the effect's own ID for it, which is what projects keep).
        enum Kind { slotMix = 0, bandMix = 1, bandVolume = 2, effectParameter = 3 };
        Destination() = default;
        Destination (int k, int slot, int index = -1, juce::String id = {})
            : kind (k), slotId (slot), parameter (index), parameterId (std::move (id)) {}
        int kind = bandMix;
        int slotId = 0;
        int parameter = -1;
        juce::String parameterId;
        bool operator== (const Destination& o) const
        {
            return kind == o.kind && (kind == bandMix || kind == bandVolume || slotId == o.slotId) && (kind != effectParameter || parameter == o.parameter);
        }
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
    // The knobs inside a slot's effect that an LFO can move (what the effect lets a host automate).
    std::vector<std::pair<Destination, juce::String>> getEffectParameters (int band, int slot) const;
    // LEARN: the next knob moved in the window of one of this band's effects is linked to the LFO.
    bool beginParameterLearn (int band, int lfo);
    void cancelParameterLearn();
    bool isLearningParameter() const noexcept { return learn != nullptr; }
    int pollParameterLearn();   // message thread: once a knob was moved, links it and returns the new route (else -1)

    // ---- Meters ------------------------------------------------------------------------------------
    // Peak levels (linear) since the last call, and the share of the audio's time Triad spent processing it
    // (1 = all of it: dropouts) since the last call.
    struct Meters { float input = 0.0f, output = 0.0f, cpu = 0.0f; std::array<float, triad::maxBands> bands {}; };
    Meters takeMeters();
    // LIVE (for recording through Triad): every effect that adds latency is passed by, so Triad adds none.
    void setLiveMode (bool);
    bool isLiveMode() const noexcept { return liveMode.load(); }

    // ---- Presets -----------------------------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> createChainXml (int band);     // one band's chain, LFOs, mix, volume
    juce::String loadChainXml (int band, const juce::XmlElement&);
    // Band clipboard, shared by every Triad in this process (message thread): "Copy band state" keeps a
    // copy of the band's whole chain (effects and their settings, slot mixes, LFOs, band mix and volume);
    // "Paste band state" replaces another band's chain with it (the crossovers stay as they are).
    bool copyBandState (int band);
    juce::String pasteBandState (int band);                          // error text; empty when pasted
    static bool hasCopiedBand();
    static juce::String copiedBandDescription();                     // e.g. "band 2: OTT, Valhalla Delay"
    static void clearCopiedBand();
    // Plug-in clipboard, also shared by every Triad in this process: "Copy plugin state" keeps the effect in a
    // slot with all its settings (and the slot's mix); "Paste plugin state" puts it into another slot (only the
    // settings change when that slot holds the same effect) or, with slot == the band's slot count, a new slot.
    bool copyEffectState (int band, int slot);
    juce::String pasteEffectState (int band, int slot);              // error text; empty when pasted
    static bool hasCopiedEffect();
    static juce::String copiedEffectName();                          // e.g. "Infiltrator"
    static void clearCopiedEffect();

    // ---- Undo / redo (message thread) -----------------------------------------------------------------
    // The main page's back / forward arrows. A moment after each change (an effect added, moved or removed,
    // a knob turned in Triad or inside an effect's window, an LFO drawn or linked, a crossover moved) Triad
    // keeps a snapshot of itself; undo goes back to the previous one. Effects that are still there are kept
    // loaded (only their settings change, if they differ), so undo is quick and open windows stay open.
    // A project or preset opened by the DAW starts a fresh history; LIVE is not part of it.
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    int getUndoSteps() const;                  // how many steps back (and forward) there are
    int getRedoSteps() const;
    void commitHistory();                      // take the pending snapshot now (tests; undo/redo do this themselves)
    void clearHistory();
    static constexpr double historyQuietMs = 350.0;   // a snapshot is taken this long after the last change
    static constexpr int maxHistorySteps = 100;
    std::unique_ptr<juce::XmlElement> createPresetXml (bool forHistory = false);   // the whole Triad
    // undoable: a preset the user picked in Triad (one undo step); the startup preset is not.
    void loadPresetXml (const juce::XmlElement&, bool undoable = true);
    // Init: an empty Triad (3 bands, empty slots, 24 dB/oct, mixes 100%, volumes 0 dB, LFOs off).
    static std::unique_ptr<juce::XmlElement> createInitXml();
    void initialize();
    // A new Triad opens with the startup preset chosen in the settings, unless the DAW restores a project
    // into it first (checked a moment after it is created, on the message thread).
    bool applyStartupPreset();
    static constexpr double startupDelayMs = 250.0;
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
        // A knob inside an effect: its value when it was linked (the LFO moves it from there, by `amount`),
        // the parameter (resolved under gate), and the last value sent (audio thread).
        std::atomic<float> base { 0.5f };
        juce::AudioProcessorParameter* parameter = nullptr;
        float lastSent = -1.0f;
        float value (int frame) const noexcept { const float v = lfo->value (frame); return invert.load (std::memory_order_relaxed) ? 1.0f - v : v; }
    };
    struct Slot
    {
        Slot();
        int id = 0;
        int serial = 0;                  // unique in this process: undo finds the same slot (and effect) again
        std::unique_ptr<juce::AudioPluginInstance> effect;
        std::unique_ptr<juce::DocumentWindow> window;
        juce::PluginDescription description;
        juce::MemoryBlock savedState;
        juce::String error;
        bool enabled = true;
        int latency = 0;            // latency compensated for this slot (0 when inactive)
        int reportedLatency = 0;    // what the effect reports
        bool liveBypassed = false;  // LIVE is on and this effect adds latency: it is passed by
        int channels = 2;           // channels the effect processes: stereo, plus any always-on extra bus (fed silence)
        // Blocks of invalid audio (NaN/Inf) the effect produced; Triad silences them, then resets or bypasses it.
        std::atomic<int> invalidBlocks { 0 };
        int invalidHandled = 0, invalidResets = 0;
        juce::int64 pendingRequest = 0;
        std::atomic<float> mixPercent { 100.0f };
        std::atomic<float> liveMix { 100.0f };
        std::vector<Route*> modulators;   // resolved under gate
        std::vector<Route*> parameterModulators;   // routes to knobs inside this effect, resolved under gate
        triad::StereoDelay dryDelay;
        juce::SmoothedValue<float> mix;
        bool isActive() const { return effect != nullptr && enabled && error.isEmpty() && ! liveBypassed; }
        bool hasContent() const { return effect != nullptr || description.name.isNotEmpty(); }
    };
    struct Band
    {
        explicit Band (bool withSlot = true, bool isExtra = false);
        bool extra = false;              // the PRE or POST chain: no band LFO, mix or volume
        int serial = 0;                  // unique in this process (undo keeps LFO undo lists with their band)
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
        static juce::AudioProcessorParameter* resolveParameter (const Slot&, Destination&);
    };
    struct PendingLoad { int slot; const juce::XmlElement* node; };

    juce::AudioPluginFormatManager formats;
    // The playhead Triad's effects see: the DAW's, moved on to the piece of the block they are playing.
    struct ChunkPlayHead final : juce::AudioPlayHead
    {
        juce::AudioPlayHead* host = nullptr;
        int offset = 0;
        double rate = 48000.0;
        juce::Optional<PositionInfo> getPosition() const override;
    };
    ChunkPlayHead chunkHead;
    int chunkBase = 0;
    std::array<std::unique_ptr<Band>, triad::maxBands + 2> bands;   // the bands, then PRE at [preBand] and POST at [postBand]
    int chainAt (int k) const noexcept { return k == 0 ? preBand : k <= numBands ? k - 1 : postBand; }   // k < numBands + 2: PRE, bands, POST
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
    std::array<std::vector<SlotInfo>, triad::maxBands + 2> info;
    std::unique_ptr<juce::XmlElement> pendingState;
    std::atomic<bool> restoring { false }, muted { false };
    std::atomic<int> silencedBlocks { 0 };
    std::atomic<bool> liveMode { false };
    struct Learn;
    std::unique_ptr<Learn> learn;
    void endParameterLearn();
    void applyParameterModulation (Slot&, int frame);
    std::atomic<juce::int64> busyTicks { 0 }, audioSamples { 0 };
    std::atomic<bool> stateReceived { false };   // setStateInformation was called (a project, a preset)
    std::atomic<bool> keepHistory { false }, historyReset { false };   // see setStateInformation
    bool startupDecided = false;
    double createdAt = 0.0;
    std::atomic<int> pendingCreations { 0 }, bandCount { 3 }, structureVersion { 0 };
    std::atomic<double> tail { 0.0 };
    std::atomic<double> rate { 44100.0 };
    std::atomic<int> totalLatency { 0 }, preLatency { 0 }, postLatency { 0 };
    int bandsLatency = 0;            // the slowest band's chain (guarded by gate); total = PRE + this
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::array<std::atomic<float>, triad::maxBands> bandPeaks {};
    juce::int64 nextRequest = 1;
    int numBands = 3;               // guarded by gate; bandCount mirrors it for other threads
    int capacity = 512;
    int fadeRemaining = 0, fadeLength = 1;
    bool prepared = false;
    juce::String status, presetName;
    std::array<std::atomic<float>*, triad::maxCrossovers> crossoverValues {}, slopeValues {};
    std::array<std::atomic<float>*, triad::maxBands> bandMixValues {}, bandGainValues {}, bandMuteValues {};
    // Song position for the LFOs, read at the start of each block (audio thread).
    double blockPpq = 0.0, blockBpm = 120.0, blockBeatsPerBar = 4.0;
    bool blockPlaying = false;

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void alignLatency();
    // A fingerprint of everything alignLatency() depends on (effects, their latency, channels and tail,
    // slots on or off), so the timer only takes the lock when one of them changed.
    juce::int64 latencySignature() const;
    std::atomic<juce::int64> alignedSignature { 0 };
    void publishInfo();   // call with gate held
    void prepareBand (int index, bool prepareEffects);
    void prepareSlot (Slot&);
    void readCrossovers (float* dest) const;
    void readSlopes (int* dest) const;
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
    // Undo history: snapshots of createPresetXml (history), the one showing now at `historyCursor`.
    struct HistorySignal;            // changes reported from other threads (gestures on knobs)
    struct EffectWatch;              // listens to the hosted effects' knob gestures
    std::shared_ptr<HistorySignal> historySignal;
    std::vector<std::unique_ptr<juce::XmlElement>> history;
    std::vector<size_t> historySizes;
    int historyCursor = -1;
    bool historyDirty = false, historyApplying = false, historyRefresh = false, historyAwaitingLoads = false;
    double lastChange = 0.0;
    void noteChange();
    void historyTick();
    bool historyBusy() const;        // loads pending, a mouse button down or a knob being turned: not now
    void applyHistory (const juce::XmlElement&);
    void watchEffect (juce::AudioPluginInstance&);
    struct OwnWatch;
    std::unique_ptr<OwnWatch> ownWatch;
    void setParameterValue (const juce::String& id, float plainValue);
    float getParameterValue (const juce::String& id) const;
    void refreshWindowTitles();
    void restoreFrom (const juce::XmlElement&);
    void writeBand (juce::XmlElement&, const Band&, bool forHistory = false) const;
    std::unique_ptr<Band> readBand (const juce::XmlElement* node, int version, const std::function<float (int)>& legacyMix,
                                    std::vector<PendingLoad>& loads, bool extra = false) const;
    bool runChain (Band&, juce::AudioBuffer<float>&, int n);   // audio thread: a band's (or PRE's) slots in order
    juce::String destinationName (const Band&, const Destination&) const;
    void stateChanged (bool undoable = true);
    bool sendKeyToHost();

    JUCE_DECLARE_WEAK_REFERENCEABLE (TriadProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadProcessor)
};
