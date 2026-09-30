#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"

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
    const juce::String getName() const override { return "Triad"; }
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
    struct SlotInfo { juce::String name, status; bool loaded = false; };
    SlotInfo getSlotInfo (int);
    juce::String loadFile (int, const juce::File&);
    void removeEffect (int);
    void showEffect (int);
    void closeWindows();
    std::pair<float, float> effectiveCutoffs() const;
    float nyquistFrequency() const { return (float) rate.load() * 0.5f; }
    juce::String getStatus() const;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> effect;
        std::unique_ptr<juce::DocumentWindow> window;
        juce::PluginDescription description;
        juce::MemoryBlock savedState;
        juce::String error;
        int latency = 0;
        triad::StereoDelay dryDelay, wetDelay;
        juce::SmoothedValue<float> mix;
    };
    juce::AudioPluginFormatManager formats;
    std::array<Slot, 3> slots;
    std::array<juce::AudioBuffer<float>, 3> bands, dry;
    triad::Splitter splitter;
    triad::StereoDelay bypassDelay;
    mutable juce::CriticalSection gate, stateGate;
    std::unique_ptr<juce::XmlElement> pendingState;
    std::atomic<bool> restoring { false };
    std::atomic<double> tail { 0.0 };
    std::atomic<double> rate { 44100.0 };
    std::atomic<int> totalLatency { 0 };
    int capacity = 512;
    bool prepared = false;
    juce::String status;
    std::array<std::atomic<float>*, 3> mixValues;
    std::atomic<float> *lowValue, *highValue;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void alignLatency();
    juce::String install (int, const juce::PluginDescription&, const juce::MemoryBlock&);
    void process (juce::AudioBuffer<float>&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadProcessor)
};
