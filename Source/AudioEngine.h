#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace triad
{
constexpr int maxLatency = 262144;

// Integer delay, allocated only during prepare. Zero delay is exactly transparent.
class StereoDelay
{
public:
    void prepare (int capacity = maxLatency)
    {
        memory.setSize (2, capacity + 1);
        reset();
    }
    void reset() { memory.clear(); position = 0; }
    float tick (int channel, float input, int delay)
    {
        auto* p = memory.getWritePointer (channel);
        p[position] = input;
        return p[(position + memory.getNumSamples() - delay) % memory.getNumSamples()];
    }
    void advance() { if (++position == memory.getNumSamples()) position = 0; }
private:
    juce::AudioBuffer<float> memory;
    int position = 0;
};

// LR4 low split followed by LR4 upper split. Compensating all-pass on
// the low branch makes L + M + H equal to the two cascaded all-passes.
class Splitter
{
public:
    void prepare (double sr, int blockSize)
    {
        sampleRate = sr;
        const juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 2 };
        lower.prepare (spec); upper.prepare (spec); lowPhase.prepare (spec);
        lowPhase.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        lowSmooth.reset (sr, 0.035); highSmooth.reset (sr, 0.035);
        lowSmooth.setCurrentAndTargetValue (250.0f);
        highSmooth.setCurrentAndTargetValue (2500.0f);
        count = 0;
        setCutoffs (250.0f, 2500.0f, true);
    }
    std::pair<float, float> constrain (float lo, float hi) const
    {
        hi = juce::jlimit (30.0f, (float) juce::jmin (20000.0, sampleRate * 0.45), hi);
        lo = juce::jlimit (20.0f, hi / 1.01f, lo);
        return { lo, hi };
    }
    void setCutoffs (float lo, float hi, bool immediate = false)
    {
        const auto cuts = constrain (lo, hi);
        if (immediate)
        {
            lowSmooth.setCurrentAndTargetValue (cuts.first);
            highSmooth.setCurrentAndTargetValue (cuts.second);
        }
        else { lowSmooth.setTargetValue (cuts.first); highSmooth.setTargetValue (cuts.second); }
    }
    void process (const juce::AudioBuffer<float>& input, int offset, int n,
                  std::array<juce::AudioBuffer<float>, 3>& bands)
    {
        for (int s = 0; s < n; ++s)
        {
            const auto lo = lowSmooth.getNextValue(), hi = highSmooth.getNextValue();
            if ((count++ & 15) == 0)
            {
                lower.setCutoffFrequency (lo);
                upper.setCutoffFrequency (hi);
                lowPhase.setCutoffFrequency (hi);
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                float l, remainder, m, h;
                lower.processSample (ch, input.getSample (ch, offset + s), l, remainder);
                upper.processSample (ch, remainder, m, h);
                bands[0].setSample (ch, s, lowPhase.processSample (ch, l));
                bands[1].setSample (ch, s, m);
                bands[2].setSample (ch, s, h);
            }
        }
        lower.snapToZero(); upper.snapToZero(); lowPhase.snapToZero();
    }
private:
    double sampleRate = 44100.0;
    unsigned count = 0;
    juce::dsp::LinkwitzRileyFilter<float> lower, upper, lowPhase;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowSmooth, highSmooth;
};
}
