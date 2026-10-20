#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioEngine.h"
#include "SplitLink.h"

// Triad Split, by Thomas Ross: Triad's band split as a plug-in of its own, so each band can go to its own
// mixer insert or track and the DAW's own plug-ins (Gross Beat, Fruity plug-ins, Ableton's devices...) can
// work on one band. It splits the sound with the same crossovers as Triad and plays one band (or all, or
// none); every band also comes out on its own output ("Band 1", "Band 2"...), for FL's Patcher or for
// routing a plug-in's outputs to mixer tracks.
// Put one on each track that should hear a band: Triad Splits in the same LINK group (A to H) share their
// bands, so moving a divider on any of them moves them all, live. With nothing after them, the band tracks
// add back up to the original sound (the same flat all-pass sum as Triad's).
class TriadSplit final : public juce::AudioProcessor, private juce::Timer
{
public:
    TriadSplit();
    ~TriadSplit() override;

    static constexpr int playAll = 0;                       // the PLAY choice: ALL, band 1..10, NONE
    static constexpr int playNone = triad::maxBands + 1;
    static constexpr int defaultGroup = 1;                  // new instances join group A
    static juce::String playName (int play);                // "ALL", "BAND 2", "NONE"

    // ---- Bands (what this instance shows; with a link group they follow the group) --------------
    int getBandCount() const;
    std::vector<float> effectiveCrossovers() const;         // constrained, one per divider
    int getCrossoverSlope (int index) const;
    juce::RangedAudioParameter* crossoverParameter (int index) const;
    juce::RangedAudioParameter* slopeParameter (int index) const;
    double getSampleRateForDisplay() const { return rate.load(); }
    bool addBand (float hz = -1.0f);                         // splits the band at hz (<= 0: the widest), as one edit
    bool removeBand (int band);
    void setAllCrossoverSlopes (int order);
    // What the audio uses right now (the group's values when linked): for tests.
    int getAppliedBandCount() const noexcept { return appliedBands.load(); }
    float getAppliedCrossover (int index) const noexcept { return appliedCuts[(size_t) juce::jlimit (0, triad::maxCrossovers - 1, index)].load(); }

    // ---- Which band the main output plays -------------------------------------------------------
    int getPlay() const;
    void setPlay (int play);                                 // as a gesture (automatable: "Play")

    // ---- Link groups ------------------------------------------------------------------------------
    int getLinkGroup() const noexcept { return linkGroup.load(); }       // 0: not linked, 1..8: A..H
    void setLinkGroup (int group);
    int getLinkedCount() const;                              // Triad Splits in this group (this one included)
    int getPlayers (int band) const;                         // how many in this group play `band` (anyBand: ALL / NONE)
    // Publishes this instance's changes and follows the group's (message thread; the timer does it 60 times
    // a second, tests call it directly).
    void syncLink();

    // ---- Meters (the editor takes the peaks since its last look) --------------------------------
    triad::SpectrumTap spectrum;
    float takeInputPeak() noexcept { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f); }
    float getCpuLoad() const noexcept { return cpuLoad.load(); }

    // ---- AudioProcessor -----------------------------------------------------------------------------
    const juce::String getName() const override { return "Triad Split"; }
    void prepareToPlay (double sampleRate, int maximumBlock) override;
    void releaseResources() override {}
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

    juce::AudioProcessorValueTreeState parameters;
    static juce::String crossoverID (int index) { return "crossover" + juce::String (index + 1); }
    static juce::String slopeID (int index) { return "slope" + juce::String (index + 1); }

private:
    void timerCallback() override { syncLink(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    static BusesProperties splitBuses();
    triad::link::Values readOwn() const noexcept;
    void setOwn (const triad::link::Values&);                // follows the group (message thread)
    void setValue (juce::RangedAudioParameter*, float value);
    void edit (const triad::link::Values& next, int structuralCode);   // a change from the window, as one edit
    void publishLocalChanges();                              // syncLock held
    void join (int group);                                   // message thread
    void joinLocked (int group);
    void leave();                                            // syncLock held
    void updatePlayers();
    void publishApplied (const std::array<float, triad::maxCrossovers>& cuts);
    void writeOutputs (juce::AudioBuffer<float>&, int offset, int count);

    // Parameters (raw values, read on the audio thread).
    std::atomic<float>* bandsValue = nullptr;
    std::atomic<float>* playValue = nullptr;
    std::array<std::atomic<float>*, triad::maxCrossovers> cutValues {}, slopeValues {};

    // Link state: lastOwn and pending are guarded by syncLock (the audio thread only try-locks it).
    juce::SpinLock syncLock;
    triad::link::Values lastOwn {};
    triad::link::Mask pending {};
    std::atomic<int> linkGroup { 0 };
    uint32_t followed = 0;                                   // the group's sequence this instance last followed
    uint32_t editsSeen = 0;                                  // the group's band edits this instance has followed
    std::atomic<uint32_t> editsFollowed { 0 };
    int playersGroup = 0, playersBand = -1;                  // what this instance counts as in the group's players
    juce::SpinLock playersLock;
    triad::link::Values shared {};                           // audio thread: the group's last good values
    bool hasShared = false;
    int audioGroup = -1;

    // Audio.
    std::atomic<double> rate { 48000.0 };
    int capacity = 0;
    triad::Splitter splitter;
    std::array<juce::AudioBuffer<float>, triad::maxBands> bands;
    juce::AudioBuffer<float> input;
    std::atomic<int> appliedBands { 3 };
    std::array<std::atomic<float>, triad::maxCrossovers> appliedCuts {};
    std::array<int, triad::maxCrossovers> appliedSlopes {};
    bool prepared = false;
    // A change in the number of bands restarts the filters: the output fades out, the bands change, it fades in.
    // (After a band was added or removed on another linked one, it stays silent until its PLAY moved along.)
    enum class Fade { none, out, in } fade = Fade::none;
    int fadeLength = 1, fadePosition = 0, heldSamples = 0;
    bool hold = false;
    std::vector<float> gains;
    // A new PLAY choice crossfades from the old one.
    int playFrom = playAll, playTo = playAll, playFadeLeft = 0, playFadeLength = 1;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, cpuLoad { 0.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadSplit)
};
