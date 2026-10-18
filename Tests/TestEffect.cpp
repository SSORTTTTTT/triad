#include <juce_audio_utils/juce_audio_utils.h>
class TestEffect final : public juce::AudioProcessor
{
public:
    TestEffect() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true).withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.25f));
        // A knob the effect leaves out of its saved settings, as a few real effects do (undo must still restore it).
        addParameter (new juce::AudioParameterFloat (juce::ParameterID { "tone", 1 }, "Tone", 0.0f, 1.0f, 0.5f));
        setLatencySamples (73);
    }
    const juce::String getName() const override { return "Triad Test Effect"; }
    void prepareToPlay (double, int) override { delay.setSize (2, 74); delay.clear(); pos = 0; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& b) const override { return b.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        for (int s = 0; s < b.getNumSamples(); ++s)
        {
            for (int c = 0; c < 2; ++c)
            {
                delay.setSample (c, pos, b.getSample (c, s));
                b.setSample (c, s, delay.getSample (c, (pos + 1) % 74) * gain->get());
            }
            pos = (pos + 1) % 74;
        }
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    void getStateInformation (juce::MemoryBlock& b) override { juce::MemoryOutputStream stream (b, false); stream.writeFloat (gain->get()); }
    void setStateInformation (const void* p, int n) override { juce::MemoryInputStream stream (p, (size_t) n, false); *gain = stream.readFloat(); }
private:
    juce::AudioParameterFloat* gain;
    juce::AudioBuffer<float> delay;
    int pos = 0;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestEffect(); }
