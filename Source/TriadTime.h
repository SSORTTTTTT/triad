#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Modulation.h"

// Triad Time: a time and volume gate in the spirit of Gross Beat, by Thomas Ross. It keeps the last
// moments of the sound in a rolling buffer and plays them back the way two drawn envelopes say:
//   TIME   - how far back it plays (top = live, lower = further back: a step repeats, a slope down slows
//            the sound down or plays it backwards, a slope back up catches up).
//   VOLUME - the gate (top = 100%, bottom = silence).
// Both are drawn with Triad's LFO system (presets, USER folder, generator) and follow the song's tempo.
// Jumps in time are crossfaded (SMOOTHING) and the gate has attack and release, so nothing clicks.
// Built into Triad (its only built-in effect) and installed on its own as a VST3 / AU plug-in.
class TriadTime final : public juce::AudioPluginInstance, private juce::AsyncUpdater
{
public:
    TriadTime();
    ~TriadTime() override;

    static constexpr const char* formatName = "Triad";        // Triad's built-in format
    static constexpr const char* identifier = "triad:time";
    static juce::PluginDescription makeDescription();

    enum Envelope { timeEnvelope = 0, volumeEnvelope = 1 };
    static constexpr int numSlots = 12;                        // patterns per envelope (like presets you switch)
    static constexpr double historySeconds = 12.0;             // the rolling buffer
    static juce::String slotName (int envelope, int slot);
    static triad::mod::Shape slotPreset (int envelope, int slot);
    // How far back the TIME envelope's bottom reaches: twice its length (a 1-bar pattern reaches 2 bars back),
    // so a line from the top-left corner to the bottom-right corner plays backwards at normal speed and a line
    // to the middle of the right edge holds the sound still.
    static double timeDepthInBeats (double lengthBeats) { return 2.0 * lengthBeats; }
    static bool isTimeLength (int lengthIndex);                // the TIME envelope: synced lengths up to 2 bars
    static bool isVolumeLength (int lengthIndex);              // the VOLUME envelope: any synced length

    // Envelopes (message thread). The slot shown and played is the slot parameter's.
    triad::mod::Shape getShape (int envelope, int slot) const;
    void setShape (int envelope, int slot, const triad::mod::Shape&, bool undoable);
    int getSlot (int envelope) const;                          // 0 .. numSlots - 1
    void setSlot (int envelope, int slot);                     // as a gesture (automation follows)
    int getLength (int envelope) const;                        // index into triad::mod::lengths()
    void setLength (int envelope, int lengthIndex);
    float getPhase (int envelope) const;                       // the playing position 0..1, -1 before it plays
    void beginEdit (int envelope);
    bool undo (int envelope);
    bool redo (int envelope);
    bool canUndo (int envelope) const { return ! undoStack[(size_t) envelope].empty(); }
    bool canRedo (int envelope) const { return ! redoStack[(size_t) envelope].empty(); }
    int getJumpCount() const noexcept { return jumps.load(); }   // time jumps crossfaded so far (tests)

    // Parameters (automatable; Triad's LFOs can move them too).
    juce::AudioParameterInt* timeSlot = nullptr;
    juce::AudioParameterInt* volumeSlot = nullptr;
    juce::AudioParameterChoice* timeLength = nullptr;
    juce::AudioParameterChoice* volumeLength = nullptr;
    juce::AudioParameterFloat* timeMix = nullptr;      // %: 0 = dry, 100 = the TIME envelope's sound
    juce::AudioParameterFloat* volumeMix = nullptr;    // %: how much of the gate is applied
    juce::AudioParameterFloat* smoothing = nullptr;    // ms: crossfade at each jump in time
    juce::AudioParameterFloat* attack = nullptr;       // ms: the gate opening
    juce::AudioParameterFloat* release = nullptr;      // ms: the gate closing

    // AudioProcessor
    const juce::String getName() const override { return "Triad Time"; }
    void prepareToPlay (double sampleRate, int maximumBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = makeDescription(); }

    // The synced lengths the length parameters choose from (indices into triad::mod::lengths()).
    static const std::vector<int>& syncedLengths();

private:
    void handleAsyncUpdate() override;
    struct Cursor { size_t segment = 0; float lastX = -1.0f; };
    static float evaluate (const triad::mod::Shape&, float x, Cursor&) noexcept;
    float readAt (int channel, double position) const noexcept;
    void pullShapes (bool force);

    mutable juce::SpinLock shapeLock;
    std::array<std::array<triad::mod::Shape, numSlots>, 2> shapes;   // guarded by shapeLock
    std::atomic<int> shapeVersion { 1 };
    std::array<std::vector<std::pair<int, triad::mod::Shape>>, 2> undoStack, redoStack;   // (slot, shape)

    // Audio thread.
    std::array<triad::mod::Shape, 2> playing;                          // reserved: no allocation when refreshed
    std::array<int, 2> playingSlot { -1, -1 };
    int playingVersion = 0;
    std::array<Cursor, 2> cursors;
    juce::AudioBuffer<float> history;
    int historySize = 0;
    juce::int64 written = 0;                                           // samples written since prepare
    double rate = 48000.0, freeBeats = 0.0, lastBpm = 120.0;
    double previousDelay = -1.0, previousSpeed = 1.0, fadePosition = 0.0, fadeSpeed = 1.0;
    int fadeLeft = 0, fadeLength = 0;
    float gate = 1.0f;
    juce::SmoothedValue<float> timeMixSmooth, volumeMixSmooth;
    std::atomic<float> phases[2] { -1.0f, -1.0f };
    std::atomic<int> jumps { 0 };
    bool prepared = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadTime)
};

// Triad's built-in effects as a plug-in format ("Triad"), so they load, save and reload like any other.
class TriadBuiltInFormat final : public juce::AudioPluginFormat
{
public:
    static std::vector<juce::PluginDescription> effects();       // what it offers: Triad Time
    juce::String getName() const override { return TriadTime::formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& identifier) override;
    bool fileMightContainThisPluginType (const juce::String& identifier) override { return identifier.startsWith ("triad:"); }
    juce::String getNameOfPluginFromIdentifier (const juce::String& identifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& d) override { return d.fileOrIdentifier == TriadTime::identifier; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
protected:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                               PluginCreationCallback) override;
};
