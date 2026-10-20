#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>
#include <cmath>
#include <cstring>

// Triad's LFOs: a shape drawn with points on a grid (ShaperBox / Gross Beat / Serum style), played
// back in sync with the DAW's tempo. Values run from 0 (bottom, 0%) to 1 (top, 100%).
namespace triad::mod
{
// A breakpoint. `curve` bends the segment from this point to the next one:
// 0 is a straight line; positive values start slowly and finish fast, negative the opposite.
struct Point
{
    float x = 0.0f, y = 1.0f, curve = 0.0f;
    bool operator== (const Point& o) const { return juce::exactlyEqual (x, o.x) && juce::exactlyEqual (y, o.y) && juce::exactlyEqual (curve, o.curve); }
};
using Shape = std::vector<Point>;

constexpr float maxCurve = 9.0f;
float curveFraction (float t, float curve);       // 0..1 along a segment -> 0..1 of its rise
float curveForMidpoint (float fraction);          // the curve whose middle sits at `fraction` of the rise
float evaluate (const Shape&, float x);           // x in 0..1
void normalise (Shape&);                          // sort, clamp, first point at x = 0 and last at x = 1
Shape flat (float y = 1.0f);
Shape repeated (const Shape&, int times);         // "2x": the pattern twice (three times...) per cycle
Shape halved (const Shape&);                      // "1/2": the first half stretched over the cycle (undoes 2x)
Shape reversed (const Shape&);                    // flip horizontal
Shape flippedVertical (const Shape&);
Shape snapped (const Shape&, int columns, int rows);
Shape removeRedundant (const Shape&);

// The preset shapes (ShapeLibrary.cpp), in categories: BASIC, SQUARES, STEPS, TRIANGLES, ANGLES,
// HARD + CURVE, SOFT (hand-made, followed by the EXTRA shapes that fit them), then the GEN folders made
// by the generator (GeneratedShapes.h), then EXTRA: every shape from the generator's preview sheets
// (ExtraShapes.h). Folders have several pages. The first ten (BASIC) keep their numbers from earlier versions.
int numPresets();
juce::String presetName (int);
Shape preset (int);
int numPresetCategories();
juce::String presetCategoryName (int);
int presetCategory (int preset);
std::vector<int> presetsIn (int category);
bool isGeneratedCategory (int category);
bool isExtraCategory (int category);
int numHandMadeShapes (int category);              // a hand-made folder's own shapes (before its EXTRA ones)

// LFO lengths, as in ShaperBox's length menu: triplet, straight, dotted and free (Hz) columns.
struct Length
{
    const char* label;
    double beats;         // quarter notes (0 for free-running)
    double bars;          // or whole bars (uses the song's time signature)
    double hz;            // free-running rate
    int column;           // 0 triplet, 1 straight, 2 dotted, 3 free
};
const std::vector<Length>& lengths();
int defaultLength();                              // "1 Bar"
double lengthInBeats (int index, double beatsPerBar);   // 0 for free-running lengths
double lengthInHz (int index);

// The shape rendered to a table the audio thread reads without locks.
class Table
{
public:
    static constexpr int size = 2048;
    Table();
    void render (const Shape&);
    float lookup (double phase) const noexcept;
private:
    std::array<std::atomic<float>, size + 1> values;
};

std::unique_ptr<juce::XmlElement> toXml (const Shape&, const juce::String& tag);
Shape fromXml (const juce::XmlElement*);
juce::String toText (const Shape&);               // clipboard
Shape fromText (const juce::String&);
}

namespace triad
{
// A playing position as the audio thread last set it, stamped with the time it did and how fast it moves, so the
// screen can carry it on smoothly between audio blocks, at the screen's own frame rate (60 or 120 a second), instead
// of in block-sized steps. Phase and time are one atomic value, so the two always belong together.
class PhaseClock
{
public:
    // Audio thread: the position (0..1, < 0: not playing) and its speed in cycles per second.
    void set (float phase, double cyclesPerSecond) noexcept
    {
        rate.store ((float) cyclesPerSecond, std::memory_order_relaxed);
        uint32_t bits = 0;
        std::memcpy (&bits, &phase, sizeof (bits));
        state.store (((uint64_t) bits << 32) | micros(), std::memory_order_release);
    }
    float last() const noexcept { return phaseOf (state.load (std::memory_order_acquire)); }
    // Where it is now: the last position carried on at its speed (for at most `maxAhead` seconds, so a DAW that
    // stopped sending audio leaves it standing where it was).
    float now (double maxAhead = 0.1) const noexcept
    {
        const auto s = state.load (std::memory_order_acquire);
        const float phase = phaseOf (s);
        if (phase < 0.0f) return phase;
        const auto elapsed = (double) (uint32_t) (micros() - (uint32_t) s) * 1.0e-6;
        const double p = (double) phase + (double) rate.load (std::memory_order_relaxed) * juce::jlimit (0.0, maxAhead, elapsed);
        return (float) (p - std::floor (p));
    }
private:
    static uint32_t micros() noexcept { return (uint32_t) (uint64_t) (juce::Time::getMillisecondCounterHiRes() * 1000.0); }
    static float phaseOf (uint64_t s) noexcept
    {
        const auto bits = (uint32_t) (s >> 32);
        float phase;
        std::memcpy (&phase, &bits, sizeof (phase));
        return phase;
    }
    std::atomic<uint64_t> state { 0xbf80000000000000ull };   // -1.0f: not playing yet
    std::atomic<float> rate { 0.0f };
};
}
