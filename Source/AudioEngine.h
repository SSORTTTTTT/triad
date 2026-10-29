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
// Effect slots per band are unlimited (each band starts with one, like an FL Studio mixer insert).
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

// One section of a crossover filter, as a TPT (Zavalishin) structure: a 2nd-order state-variable
// filter with damping r2 = 1/Q, or a 1st-order filter (r2 < 0). It gives the low-pass or the
// all-pass response of the section. g = tan(pi * cutoff / sampleRate).
struct FilterSection
{
    float r2 = -1.0f, g = 0.0f, h = 0.0f, G = 0.0f;
    std::array<float, 2> s1 {}, s2 {};
    bool firstOrder() const noexcept { return r2 < 0.0f; }
    void setG (float newG) noexcept
    {
        g = newG;
        if (firstOrder()) G = g / (1.0f + g);
        else h = 1.0f / (1.0f + r2 * g + g * g);
    }
    void reset() noexcept { s1.fill (0.0f); s2.fill (0.0f); }
    float lowpass (int ch, float x) noexcept
    {
        const auto c = (size_t) ch;
        if (firstOrder())
        {
            const float v = (x - s1[c]) * G, y = v + s1[c];
            s1[c] = y + v;
            return y;
        }
        const float yH = (x - (r2 + g) * s1[c] - s2[c]) * h;
        const float yB = g * yH + s1[c];
        s1[c] = g * yH + yB;
        const float yL = g * yB + s2[c];
        s2[c] = g * yB + yL;
        return yL;
    }
    float allpass (int ch, float x) noexcept
    {
        const auto c = (size_t) ch;
        if (firstOrder())
        {
            const float v = (x - s1[c]) * G, y = v + s1[c];
            s1[c] = y + v;
            return 2.0f * y - x;
        }
        const float yH = (x - (r2 + g) * s1[c] - s2[c]) * h;
        const float yB = g * yH + s1[c];
        s1[c] = g * yH + yB;
        const float yL = g * yB + s2[c];
        s2[c] = g * yB + yL;
        return yL - r2 * yB + yH;
    }
    void snapToZero() noexcept
    {
        for (auto* st : { &s1, &s2 })
            for (auto& v : *st)
                if (! (v < -1.0e-8f || v > 1.0e-8f)) v = 0.0f;
    }
};

// Stereo crossover filter with a slope of 6 dB/octave per order, orders 1 to 16 (6 to 96 dB/oct;
// 54 dB/oct and up are the near-brickwall cuts).
// Odd orders are Butterworth, even orders Linkwitz-Riley (a Butterworth of half the order, twice).
// For both, low-pass + high-pass is an all-pass, so the high band is computed as all-pass minus
// low-pass and the two always add back exactly to that all-pass (flat magnitude). processAllpass()
// gives the same all-pass on its own, to keep lower bands in phase with this split.
// Order 4 is Triad's original 24 dB/oct Linkwitz-Riley, which the tests compare with JUCE's.
class CrossoverFilter
{
public:
    static constexpr int minOrder = 1, maxOrder = 16, defaultOrder = 4, steepestClassic = 8;
    static constexpr int dbPerOctave (int order) { return 6 * order; }

    CrossoverFilter() { configure(); }
    void prepare (double newSampleRate) { sampleRate = newSampleRate; setCutoffFrequency (cutoff); reset(); }
    void reset() noexcept
    {
        for (int i = 0; i < numLp; ++i) lp[(size_t) i].reset();
        for (int i = 0; i < numAp; ++i) ap[(size_t) i].reset();
    }
    // Structural change: the filter memory restarts.
    void setOrder (int newOrder)
    {
        order = juce::jlimit (minOrder, maxOrder, newOrder);
        configure();
        setCoefficient (g);
        reset();
    }
    int getOrder() const noexcept { return order; }
    static float coefficientFor (float hz, double sr) { return (float) std::tan (juce::MathConstants<double>::pi * hz / sr); }
    void setCutoffFrequency (float hz) { cutoff = hz; setCoefficient (coefficientFor (hz, sampleRate)); }
    void setCoefficient (float newG) noexcept
    {
        g = newG;
        for (int i = 0; i < numLp; ++i) lp[(size_t) i].setG (g);
        for (int i = 0; i < numAp; ++i) ap[(size_t) i].setG (g);
    }
    float processAllpass (int ch, float x) noexcept
    {
        for (int i = 0; i < numAp; ++i) x = ap[(size_t) i].allpass (ch, x);
        return x;
    }
    void processSample (int ch, float x, float& low, float& high) noexcept
    {
        float l = x;
        for (int i = 0; i < numLp; ++i) l = lp[(size_t) i].lowpass (ch, l);
        low = l;
        high = processAllpass (ch, x) - l;
    }
    void snapToZero() noexcept
    {
        for (int i = 0; i < numLp; ++i) lp[(size_t) i].snapToZero();
        for (int i = 0; i < numAp; ++i) ap[(size_t) i].snapToZero();
    }
    // Magnitude of the low-pass and high-pass at a frequency (for drawing the bands).
    static float lowMagnitude (int order, float hz, float cutoffHz, double sr)
    {
        const double r = std::tan (juce::MathConstants<double>::pi * juce::jlimit (1.0, sr * 0.4999, (double) hz) / sr)
                       / std::tan (juce::MathConstants<double>::pi * (double) cutoffHz / sr);
        if (order % 2 == 1) return (float) (1.0 / std::sqrt (1.0 + std::pow (r, 2.0 * order)));
        return (float) (1.0 / (1.0 + std::pow (r, (double) order)));
    }
    static float highMagnitude (int order, float hz, float cutoffHz, double sr)
    {
        const double r = std::tan (juce::MathConstants<double>::pi * juce::jlimit (1.0, sr * 0.4999, (double) hz) / sr)
                       / std::tan (juce::MathConstants<double>::pi * (double) cutoffHz / sr);
        if (order % 2 == 1) return (float) (std::pow (r, (double) order) / std::sqrt (1.0 + std::pow (r, 2.0 * order)));
        const double p = std::pow (r, (double) order);
        return (float) (p / (1.0 + p));
    }

private:
    void configure()
    {
        numLp = numAp = 0;
        // Pole pairs of a Butterworth filter of order n, as angles from the negative real axis.
        auto pairAngle = [] (int n, int j) { return n % 2 == 0 ? (2.0 * j - 1.0) * juce::MathConstants<double>::pi / (2.0 * n)
                                                               : j * juce::MathConstants<double>::pi / n; };
        auto addPair = [] (auto& list, int& count, double angle) { list[(size_t) count++].r2 = (float) (2.0 * std::cos (angle)); };
        auto addReal = [] (auto& list, int& count) { list[(size_t) count++].r2 = -1.0f; };
        std::array<FilterSection, maxAp> apSections {};
        if (order % 2 == 1)
        {
            // Butterworth: low-pass uses every pole; the all-pass keeps the pole pairs that are not
            // cancelled by 1 + s^n (those at odd multiples of pi/n).
            for (int j = 1; j <= order / 2; ++j)
            {
                addPair (lp, numLp, pairAngle (order, j));
                if (j % 2 == 1) addPair (apSections, numAp, pairAngle (order, j));
            }
            addReal (lp, numLp);
        }
        else
        {
            // Linkwitz-Riley: Butterworth of order k twice; the all-pass has the same poles once.
            const int k = order / 2;
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int j = 1; j <= k / 2; ++j) addPair (lp, numLp, pairAngle (k, j));
                if (k % 2 == 1) addReal (lp, numLp);
            }
            for (int j = 1; j <= k / 2; ++j) addPair (apSections, numAp, pairAngle (k, j));
            if (k % 2 == 1) addReal (apSections, numAp);
        }
        for (int i = 0; i < numAp; ++i) ap[(size_t) i] = apSections[(size_t) i];
    }
    static constexpr int maxLp = maxOrder / 2, maxAp = maxOrder / 4;
    std::array<FilterSection, maxLp> lp {};
    std::array<FilterSection, maxAp> ap {};
    int numLp = 0, numAp = 0, order = defaultOrder;
    double sampleRate = 44100.0;
    float cutoff = 2000.0f, g = 0.0f;
};

// How much later the sound right at a crossover comes out when the bands are added back together (the
// group delay of the crossover's all-pass sum at that frequency), in milliseconds. Steeper slopes delay it
// more; it falls off away from the crossover (Triad itself reports no latency: nothing is delayed overall).
inline double crossoverGroupDelayMs (int order, float hz, double sampleRate)
{
    CrossoverFilter filter;
    filter.prepare (sampleRate);
    filter.setOrder (order);
    filter.setCutoffFrequency (hz);
    // Impulse response of the all-pass sum, long enough to ring out; then Re(sum(n h e^-jwn) / sum(h e^-jwn)).
    const int length = juce::jlimit (4096, 1 << 18, (int) (sampleRate / juce::jmax (1.0f, hz) * 64.0));
    const double w = juce::MathConstants<double>::twoPi * hz / sampleRate;
    std::complex<double> sum, weighted;
    for (int n = 0; n < length; ++n)
    {
        const double h = filter.processAllpass (0, n == 0 ? 1.0f : 0.0f);
        const std::complex<double> e (std::cos (w * n), -std::sin (w * n));
        sum += h * e;
        weighted += (double) n * h * e;
    }
    return std::abs (sum) > 1.0e-12 ? (weighted / sum).real() / sampleRate * 1000.0 : 0.0;
}

// "24 dB/oct" for a slope order (shared by Triad and Triad Split).
inline juce::String slopeText (int order) { return juce::String (CrossoverFilter::dbPerOctave (order)) + " dB/oct"; }

// Where the dividers start for a number of bands (Triad's defaults: 3 bands at 250 Hz and 2.5 kHz).
inline float defaultCrossover (int index, int count)
{
    static const std::vector<std::vector<float>> table { {}, {}, { 1000.0f }, { 250.0f, 2500.0f },
                                                          { 200.0f, 1000.0f, 5000.0f }, { 120.0f, 500.0f, 2000.0f, 7000.0f } };
    if (count < (int) table.size() && index < (int) table[(size_t) count].size())
        return table[(size_t) count][(size_t) index];
    const auto t = (float) index / (float) juce::jmax (1, count - 2);
    return 80.0f * std::pow (12000.0f / 80.0f, t);
}

// N-band crossover (1 to 10 bands) with a slope per crossover. Band b is the low-pass at crossover b
// of what remains above crossover b-1, followed by the all-pass of every higher crossover, so the
// bands sum to the cascade of all-passes: flat magnitude, phase-rotated.
// Changing a slope restarts that filter structure, so it happens on a second set of filters: the
// new set runs for 10 ms to settle, then the bands crossfade to it over 20 ms (no clicks).
class Splitter
{
public:
    void prepare (double sr, int blockSize)
    {
        juce::ignoreUnused (blockSize);
        sampleRate = sr;
        for (auto& set : sets) set.prepare (sr);
        for (auto& s : smooth) s.reset (sr, 0.035);
        warmupLength = juce::jmax (1, (int) (sr * 0.010));
        fadeLength = juce::jmax (1, (int) (sr * 0.020));
        count = 0;
        phase = Phase::none;
        pending = false;
        moveNow.fill (0.0f); moveTarget.fill (0.0f); moveStep.fill (0.0f);
        settling = 0;
        moving = false;
    }
    // Structural change (called with the processor locked): filter memory restarts.
    // `orders` (optional) gives each crossover's slope order; otherwise the current ones are kept.
    void setBandCount (int bands, const float* cuts, const int* orders = nullptr)
    {
        numBands = juce::jlimit (1, maxBands, bands);
        if (orders != nullptr)
            for (int j = 0; j < maxCrossovers; ++j) wanted[(size_t) j] = juce::jlimit (CrossoverFilter::minOrder, CrossoverFilter::maxOrder, orders[j]);
        phase = Phase::none;
        pending = false;
        sets[(size_t) active].configure (numBands, wanted.data());
        setCutoffs (cuts, true);
    }
    int getBandCount() const noexcept { return numBands; }
    int getOrder (int crossover) const noexcept { return wanted[(size_t) juce::jlimit (0, maxCrossovers - 1, crossover)]; }
    bool isChangingSlope() const noexcept { return phase != Phase::none || pending; }
    // Slope orders per crossover (read from the parameters every block; changes crossfade).
    void setSlopes (const int* orders)
    {
        bool changed = false;
        for (int j = 0; j < numBands - 1; ++j)
        {
            const int o = juce::jlimit (CrossoverFilter::minOrder, CrossoverFilter::maxOrder, orders[j]);
            if (o != wanted[(size_t) j]) { wanted[(size_t) j] = o; changed = true; }
        }
        if (! changed) return;
        if (phase == Phase::none) startTransition();
        else pending = true;
    }
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
    // `moves` (optional, 0.18.0: LFOs on the band edges): for each piece of `step` samples, how far each divider is
    // moved from where its knob is, in octaves (maxCrossovers values a piece). The dividers glide from one piece's
    // value to the next, sample by sample, and never cross each other.
    void process (const juce::AudioBuffer<float>& input, int offset, int n,
                  std::array<juce::AudioBuffer<float>, maxBands>& bands, const float* moves = nullptr, int step = 32)
    {
        const int crossovers = numBands - 1;
        std::array<float, maxBands> a {}, b {};
        if (moves == nullptr && moving)
        {
            // The LFOs let go: glide back to the knobs over one piece.
            for (int j = 0; j < maxCrossovers; ++j) { moveTarget[(size_t) j] = 0.0f; moveStep[(size_t) j] = -moveNow[(size_t) j] / (float) juce::jmax (1, step); }
            settling = juce::jmax (1, step);
        }
        for (int s = 0; s < n; ++s)
        {
            if (moves != nullptr && s % step == 0)
            {
                const float* piece = moves + (size_t) (s / step) * maxCrossovers;
                for (int j = 0; j < crossovers; ++j)
                {
                    moveTarget[(size_t) j] = piece[j];
                    moveStep[(size_t) j] = (piece[j] - moveNow[(size_t) j]) / (float) juce::jmin (step, n - s);
                }
                moving = true;
                settling = juce::jmin (step, n - s);
            }
            if (settling > 0)
            {
                for (int j = 0; j < crossovers; ++j) moveNow[(size_t) j] += moveStep[(size_t) j];
                if (--settling == 0)
                {
                    for (int j = 0; j < crossovers; ++j) moveNow[(size_t) j] = moveTarget[(size_t) j];
                    if (moves == nullptr) { moving = false; moveNow.fill (0.0f); }
                }
            }
            for (int j = 0; j < crossovers; ++j) current[(size_t) j] = smooth[(size_t) j].getNextValue();
            if ((count++ & 15) == 0) updateCoefficients();
            const float t = phase == Phase::fade ? (float) position / (float) fadeLength : 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = input.getSample (ch, offset + s);
                sets[(size_t) active].processSample (ch, x, a.data());
                if (phase == Phase::none)
                {
                    for (int band = 0; band < numBands; ++band) bands[(size_t) band].setSample (ch, s, a[(size_t) band]);
                    continue;
                }
                sets[(size_t) (1 - active)].processSample (ch, x, b.data());
                for (int band = 0; band < numBands; ++band)
                    bands[(size_t) band].setSample (ch, s, phase == Phase::warmup ? a[(size_t) band]
                                                                               : a[(size_t) band] + t * (b[(size_t) band] - a[(size_t) band]));
            }
            advanceTransition();
        }
        sets[(size_t) active].snapToZero();
        if (phase != Phase::none) sets[(size_t) (1 - active)].snapToZero();
    }
private:
    // One complete set of crossover and phase-alignment filters.
    struct FilterSet
    {
        std::array<CrossoverFilter, maxCrossovers> split;
        std::array<std::array<CrossoverFilter, maxCrossovers>, maxBands> phase;   // [band][higher crossover]
        int numBands = 3;
        void prepare (double sr)
        {
            for (auto& f : split) f.prepare (sr);
            for (auto& row : phase) for (auto& f : row) f.prepare (sr);
        }
        void configure (int bands, const int* orders)
        {
            numBands = bands;
            for (int j = 0; j < maxCrossovers; ++j)
            {
                split[(size_t) j].setOrder (orders[j]);
                for (int b = 0; b < j; ++b) phase[(size_t) b][(size_t) j].setOrder (orders[j]);
            }
        }
        void setCoefficients (const float* g, int crossovers)
        {
            for (int j = 0; j < crossovers; ++j)
            {
                split[(size_t) j].setCoefficient (g[j]);
                for (int b = 0; b < j; ++b) phase[(size_t) b][(size_t) j].setCoefficient (g[j]);
            }
        }
        void processSample (int ch, float x, float* out) noexcept
        {
            const int crossovers = numBands - 1;
            for (int b = 0; b < crossovers; ++b)
            {
                float low, high;
                split[(size_t) b].processSample (ch, x, low, high);
                for (int j = b + 1; j < crossovers; ++j) low = phase[(size_t) b][(size_t) j].processAllpass (ch, low);
                out[b] = low;
                x = high;
            }
            out[crossovers] = x;
        }
        void snapToZero() noexcept
        {
            const int crossovers = numBands - 1;
            for (int b = 0; b < crossovers; ++b)
            {
                split[(size_t) b].snapToZero();
                for (int j = b + 1; j < crossovers; ++j) phase[(size_t) b][(size_t) j].snapToZero();
            }
        }
    };
    enum class Phase { none, warmup, fade };
    void startTransition()
    {
        auto& next = sets[(size_t) (1 - active)];
        next.configure (numBands, wanted.data());
        next.setCoefficients (coefficients.data(), numBands - 1);
        phase = Phase::warmup;
        position = 0;
    }
    void advanceTransition() noexcept
    {
        if (phase == Phase::warmup && ++position >= warmupLength) { phase = Phase::fade; position = 0; }
        else if (phase == Phase::fade && ++position >= fadeLength)
        {
            active = 1 - active;
            phase = Phase::none;
            if (pending)
            {
                pending = false;
                bool differs = false;
                for (int j = 0; j < numBands - 1; ++j) differs |= sets[(size_t) active].split[(size_t) j].getOrder() != wanted[(size_t) j];
                if (differs) startTransition();
            }
        }
    }
    void updateCoefficients()
    {
        const int crossovers = numBands - 1;
        for (int j = 0; j < crossovers; ++j)
        {
            current[(size_t) j] = smooth[(size_t) j].getCurrentValue();
            if (moving) current[(size_t) j] *= std::exp2 (moveNow[(size_t) j]);
        }
        if (moving) constrainCrossovers (current.data(), crossovers, sampleRate);   // moved dividers keep their order
        for (int j = 0; j < crossovers; ++j)
        {
            coefficients[(size_t) j] = CrossoverFilter::coefficientFor (current[(size_t) j], sampleRate);
            heard[(size_t) j].store (current[(size_t) j], std::memory_order_relaxed);
        }
        sets[(size_t) active].setCoefficients (coefficients.data(), crossovers);
        if (phase != Phase::none) sets[(size_t) (1 - active)].setCoefficients (coefficients.data(), crossovers);
    }
    double sampleRate = 44100.0;
    unsigned count = 0;
    int numBands = 3, active = 0, position = 0, warmupLength = 441, fadeLength = 882;
    Phase phase = Phase::none;
    bool pending = false;
    std::array<FilterSet, 2> sets;
    std::array<int, maxCrossovers> wanted { 4, 4, 4, 4, 4, 4, 4, 4, 4 };
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, maxCrossovers> smooth;
    std::array<float, maxCrossovers> current {}, coefficients {};
    // LFOs on the band edges: each divider's move from its knob, in octaves (now, the piece's target, per sample).
    std::array<float, maxCrossovers> moveNow {}, moveTarget {}, moveStep {};
    int settling = 0;
    bool moving = false;
    std::array<std::atomic<float>, maxCrossovers> heard {};
public:
    // Where each divider is as heard (with its LFO), for the screen.
    float getHeard (int j) const noexcept { return heard[(size_t) juce::jlimit (0, maxCrossovers - 1, j)].load (std::memory_order_relaxed); }
    bool isMoving() const noexcept { return moving; }
};

// Lock-free tap of the input signal for the spectrum display. The audio thread writes a mono
// mix into a ring; the editor copies the most recent samples. Atomics avoid data races.
class SpectrumTap
{
public:
    static constexpr int size = 16384;   // 0.20.0: room for the 8192-point spectrum while it is written
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
