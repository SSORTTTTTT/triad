#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

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

// Stereo 4th-order Linkwitz-Riley filter (two cascaded 2nd-order TPT state-variable
// sections, Zavalishin). Self-contained so the plugin does not need the juce_dsp module,
// which pulls in the audio-file codecs (FLAC, Ogg Vorbis, ...) and enlarges the binary.
// Numerically matches JUCE's dsp::LinkwitzRileyFilter; the integration tests compare them.
class LinkwitzRiley
{
public:
    void prepare (double newSampleRate) { sampleRate = newSampleRate; update(); reset(); }
    void reset() { for (auto* s : { &s1, &s2, &s3, &s4 }) s->fill (0.0f); }
    void setCutoffFrequency (float hz) { cutoff = hz; update(); }

    // All-pass response (used to phase-align the low band with the upper split).
    float processAllpass (int ch, float x) noexcept
    {
        float yH, yB, yL;
        firstSection (ch, x, yH, yB, yL);
        return yL - R2 * yB + yH;
    }
    // Simultaneous LR4 low-pass and complementary high-pass outputs.
    void processSample (int ch, float x, float& low, float& high) noexcept
    {
        float yH, yB, yL;
        firstSection (ch, x, yH, yB, yL);
        const auto c = (size_t) ch;
        const auto yH2 = (yL - (R2 + g) * s3[c] - s4[c]) * h;
        const auto yB2 = g * yH2 + s3[c];
        s3[c] = g * yH2 + yB2;
        const auto yL2 = g * yB2 + s4[c];
        s4[c] = g * yB2 + yL2;
        low = yL2;
        high = yL - R2 * yB + yH - yL2;
    }
    void snapToZero() noexcept
    {
        for (auto* s : { &s1, &s2, &s3, &s4 })
            for (auto& v : *s)
                if (! (v < -1.0e-8f || v > 1.0e-8f)) v = 0.0f;
    }

private:
    void firstSection (int ch, float x, float& yH, float& yB, float& yL) noexcept
    {
        const auto c = (size_t) ch;
        yH = (x - (R2 + g) * s1[c] - s2[c]) * h;
        yB = g * yH + s1[c];
        s1[c] = g * yH + yB;
        yL = g * yB + s2[c];
        s2[c] = g * yB + yL;
    }
    void update()
    {
        g  = (float) std::tan (juce::MathConstants<double>::pi * cutoff / sampleRate);
        R2 = (float) std::sqrt (2.0);
        h  = (float) (1.0 / (1.0 + R2 * g + g * g));
    }
    std::array<float, 2> s1 {}, s2 {}, s3 {}, s4 {};
    double sampleRate = 44100.0;
    float cutoff = 2000.0f, g = 0.0f, R2 = 0.0f, h = 0.0f;
};

// LR4 low split followed by LR4 upper split. Compensating all-pass on
// the low branch makes L + M + H equal to the two cascaded all-passes.
class Splitter
{
public:
    void prepare (double sr, int blockSize)
    {
        sampleRate = sr;
        juce::ignoreUnused (blockSize);
        lower.prepare (sr); upper.prepare (sr); lowPhase.prepare (sr);
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
                bands[0].setSample (ch, s, lowPhase.processAllpass (ch, l));
                bands[1].setSample (ch, s, m);
                bands[2].setSample (ch, s, h);
            }
        }
        lower.snapToZero(); upper.snapToZero(); lowPhase.snapToZero();
    }
private:
    double sampleRate = 44100.0;
    unsigned count = 0;
    LinkwitzRiley lower, upper, lowPhase;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowSmooth, highSmooth;
};
}
