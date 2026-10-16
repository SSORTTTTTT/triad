#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

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

int numPresets();
juce::String presetName (int);
Shape preset (int);

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
