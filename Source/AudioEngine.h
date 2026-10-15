#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace triad
{
constexpr int maxLatency = 262144;   // per hosted effect, and per band chain
constexpr int maxBands = 10;
constexpr int maxSlots = 10;         // effect slots per band, like an FL Studio mixer insert
constexpr int maxCrossovers = maxBands - 1;
constexpr float minCrossoverRatio = 1.01f;  // adjacent crossovers stay at least 1% apart
constexpr float lowestCrossover = 20.0f;

// Highest usable crossover for a sample rate (also capped at 20 kHz).
inline float crossoverCeiling (double sampleRate) { return (float) juce::jmin (20000.0, sampleRate * 0.45); }

// Keeps crossovers strictly increasing and inside [20 Hz, ceiling]. Higher crossovers take
// priority, matching Triad 0.1 where the low/mid split was limited below the mid/high split.
inline void constrainCrossovers (float* cuts, int count, double sampleRate)
{
    if (count <= 0) return;
    const auto ceiling = crossoverCeiling (sampleRate);
    for (int j = count - 1; j >= 0; --j)
    {
        const auto floorHz = lowestCrossover * std::pow (minCrossoverRatio, (float) j);
        const auto upper = j == count - 1 ? ceiling : cuts[j + 1] / minCrossoverRatio;
        cuts[j] = juce::jlimit (juce::jmin (floorHz, upper), upper, cuts[j]);
    }
}

// Integer delay line. Memory is allocated by ensureCapacity() on a non-audio thread and only
// as large as the latency it must compensate, so empty or zero-latency slots cost nothing.
class StereoDelay
{
public:
    void ensureCapacity (int maxDelay)
    {
        const int needed = maxDelay > 0 ? maxDelay + 1 : 0;
        if (needed == 0 && memory.getNumSamples() == 0) return;
        if (memory.getNumSamples() >= needed && memory.getNumSamples() <= juce::jmax (needed * 4, 4096)) return;
        memory.setSize (2, needed, false, true, false);
        reset();
    }
    void reset() { memory.clear(); position = 0; }
    float tick (int channel, float input, int delay) noexcept
    {
        const int size = memory.getNumSamples();
        if (delay <= 0 || size == 0) return input;
        jassert (delay < size);
        auto* p = memory.getWritePointer (channel);
        p[position] = input;
        return p[(position + size - juce::jmin (delay, size - 1)) % size];
    }
    void advance() noexcept { if (memory.getNumSamples() > 0 && ++position >= memory.getNumSamples()) position = 0; }
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

// N-band Linkwitz-Riley crossover (1 to 10 bands). Band b is the LR4 low-pass at crossover b
// of what remains above crossover b-1, followed by all-pass filters at every higher crossover.
// The bands therefore sum to the cascade of all-passes: flat magnitude, phase-rotated.
class Splitter
{
public:
    void prepare (double sr, int blockSize)
    {
        juce::ignoreUnused (blockSize);
        sampleRate = sr;
        for (auto& f : split) f.prepare (sr);
        for (auto& row : phase) for (auto& f : row) f.prepare (sr);
        for (auto& s : smooth) s.reset (sr, 0.035);
        count = 0;
    }
    // Structural change (called with the processor locked): filter memory restarts.
    void setBandCount (int bands, const float* cuts)
    {
        numBands = juce::jlimit (1, maxBands, bands);
        for (auto& f : split) f.reset();
        for (auto& row : phase) for (auto& f : row) f.reset();
        setCutoffs (cuts, true);
    }
    int getBandCount() const noexcept { return numBands; }
    void setCutoffs (const float* raw, bool immediate = false)
    {
        std::array<float, maxCrossovers> cuts {};
        const int n = numBands - 1;
        for (int j = 0; j < n; ++j) cuts[(size_t) j] = raw[j];
        constrainCrossovers (cuts.data(), n, sampleRate);
        for (int j = 0; j < n; ++j)
        {
            auto& s = smooth[(size_t) j];
            if (immediate) s.setCurrentAndTargetValue (cuts[(size_t) j]);
            else s.setTargetValue (cuts[(size_t) j]);
        }
        if (immediate) { count = 0; updateCoefficients(); }
    }
    void process (const juce::AudioBuffer<float>& input, int offset, int n,
                  std::array<juce::AudioBuffer<float>, maxBands>& bands)
    {
        const int crossovers = numBands - 1;
        for (int s = 0; s < n; ++s)
        {
            for (int j = 0; j < crossovers; ++j) current[(size_t) j] = smooth[(size_t) j].getNextValue();
            if ((count++ & 15) == 0) updateCoefficients();
            for (int ch = 0; ch < 2; ++ch)
            {
                float x = input.getSample (ch, offset + s);
                for (int b = 0; b < crossovers; ++b)
                {
                    float low, high;
                    split[(size_t) b].processSample (ch, x, low, high);
                    for (int j = b + 1; j < crossovers; ++j)
                        low = phase[(size_t) b][(size_t) j].processAllpass (ch, low);
                    bands[(size_t) b].setSample (ch, s, low);
                    x = high;
                }
                bands[(size_t) crossovers].setSample (ch, s, x);
            }
        }
        for (int b = 0; b < crossovers; ++b)
        {
            split[(size_t) b].snapToZero();
            for (int j = b + 1; j < crossovers; ++j) phase[(size_t) b][(size_t) j].snapToZero();
        }
    }
private:
    void updateCoefficients()
    {
        const int crossovers = numBands - 1;
        for (int j = 0; j < crossovers; ++j)
        {
            const auto hz = smooth[(size_t) j].getCurrentValue();
            current[(size_t) j] = hz;
            split[(size_t) j].setCutoffFrequency (hz);
            for (int b = 0; b < j; ++b) phase[(size_t) b][(size_t) j].setCutoffFrequency (hz);
        }
    }
    double sampleRate = 44100.0;
    unsigned count = 0;
    int numBands = 3;
    std::array<LinkwitzRiley, maxCrossovers> split;
    std::array<std::array<LinkwitzRiley, maxCrossovers>, maxBands> phase;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, maxCrossovers> smooth;
    std::array<float, maxCrossovers> current {};
};

// Lock-free tap of the input signal for the spectrum display. The audio thread writes a mono
// mix into a ring; the editor copies the most recent samples. Atomics avoid data races.
class SpectrumTap
{
public:
    static constexpr int size = 8192;
    void push (const juce::AudioBuffer<float>& audio, int numSamples) noexcept
    {
        if (! active.load (std::memory_order_relaxed) || audio.getNumChannels() < 2) return;
        auto w = writeIndex.load (std::memory_order_relaxed);
        const auto* l = audio.getReadPointer (0);
        const auto* r = audio.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            ring[(size_t) (w++ & (size - 1))].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
        writeIndex.store (w, std::memory_order_release);
    }
    // Copies the newest `count` samples (oldest first). Returns false if nothing was written yet.
    bool read (float* dest, int count) const noexcept
    {
        const auto w = writeIndex.load (std::memory_order_acquire);
        if (w == 0) return false;
        for (int i = 0; i < count; ++i)
            dest[i] = ring[(size_t) ((w - (uint32_t) count + (uint32_t) i) & (size - 1))].load (std::memory_order_relaxed);
        return true;
    }
    uint32_t getWriteIndex() const noexcept { return writeIndex.load (std::memory_order_acquire); }
    std::atomic<bool> active { false };
private:
    std::array<std::atomic<float>, size> ring {};
    std::atomic<uint32_t> writeIndex { 0 };
};

// Small in-place radix-2 FFT, enough for the spectrum display (avoids the juce_dsp module).
inline void fft (std::vector<std::complex<float>>& data)
{
    const size_t n = data.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (data[i], data[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto angle = -2.0 * juce::MathConstants<double>::pi / (double) len;
        const std::complex<float> step ((float) std::cos (angle), (float) std::sin (angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = data[i + k], v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}
}
