#pragma once
#include "Modulation.h"
#include <functional>

// The shape generator behind the LFO tab's dice: new shapes in the style Triad was tuned on (Thomas's
// Infiltrator favourites: 846 drawn shapes), never copies of them. Shapes are built from gestures
// on the grid (gates, steps, stairs, saws, triangles, ramps at 30 / 45 / 60 degrees, jump-and-decay
// curves, waves) laid out on a musical rhythm, usually repeated with small variations and a fill.
namespace triad::mod
{
// Which presets the dice learned from (Thomas: one set per dice, so each keeps its own style).
//   INFILTRATOR    Triad's LFO dice as it was: tuned on his Infiltrator favourites (846 drawn shapes).
//   SHAPERBOX      his ShaperBox presets (4,001 LFO waves): their cells (motifs) and how they repeat them.
//   GROSS BEAT     (TIME) the time envelopes of his Gross Beat banks: runs, octave / reverse lines, chops.
//   TIME PRESETS   (TIME) one of Triad Time's 1,000 TIME patterns; Chaos splices in parts of others.
//   GROSS BEAT     (VOLUME) the volume envelopes of the banks: their cells and how they repeat them.
enum class Source { infiltrator = 0, shaperBox, grossBeatTime, timePresets, grossBeatVolume };
juce::String sourceName (Source);
struct Style
{
    float angular = 1.0f;   // 0..1  hard corners: vertical steps, gates, triangles, straight 30/45/60-degree lines
    float soft = 0.0f;      // 0..1  curves and waves; with Angular too, hard jumps into curves
    float chaos = 0.35f;    // 0..1  more points, faster and less regular rhythm, more levels, fewer repeats
    Source source = Source::infiltrator;
    // Triad's LFO dice (the multiband plug-ins'; Thomas, 0.13.0): Chaos also makes more triangles and more squares, each
    // with more points (runs of triangles, trains of squares, busier cells played twice or four times in their place).
    // Off for Triad Time, whose dice stays as trained on the Gross Beat banks.
    bool lfoChaos = false;
};

struct GridInfo
{
    int columns = 16;       // grid columns in one cycle (the editor's grid)
    int rows = 16;          // value levels points snap to
    float aspect = 1.87f;   // width / height of the grid on screen, so angles are the angles you see
};

constexpr float plotAspect = 1.87f;   // Triad's LFO grid (642 x 344 points)

Shape generate (const Style&, const GridInfo&, juce::Random&);
Shape generate (const Style&, const GridInfo&, juce::int64 seed);

// The angle of a line on screen, in degrees: 0 flat, 90 vertical.
float lineAngle (const Point& from, const Point& to, float aspect = plotAspect);

// What you liked and disliked among the dice's shapes (the green check and the red X under NEW SHAPE), so the dice
// leans towards shapes like the ones you liked and stays away from shapes like the ones you disliked (made with
// similar Angular / Soft / Chaos settings). One per kind of shape (Triad's LFOs, Triad Time's TIME and VOLUME).
class Taste
{
public:
    void add (const Shape&, const Style&, bool liked);
    void clear() { entries.clear(); }
    int countLiked() const;
    int countDisliked() const;
    bool empty() const noexcept { return entries.empty(); }
    // How alike two shapes are, 0..1 (1: the same): their curve, their make-up (steps, curves, straight lines, how
    // many points, how much they move) and the Angular / Soft / Chaos they were made with.
    static float similarity (const Shape&, const Style&, const Shape&, const Style&);
    // Higher: closer to what you liked, further from what you disliked (0 when nothing is rated yet).
    float score (const Shape&, const Style&) const;
    // Too close to a shape you disliked: never offered again.
    bool rejects (const Shape&, const Style&) const;
    // The dice with your taste: `candidates` shapes in this style, then one of the best of them (not always the very
    // best, so it keeps surprising you). Without ratings it is the plain dice.
    Shape pick (const Style&, const GridInfo&, juce::Random&, const std::function<bool (const Shape&)>& allowed = {}, int candidates = 24) const;
    juce::String toText() const;                 // one line per rating
    static Taste fromText (const juce::String&);
    static constexpr int maxEntries = 400;       // the oldest ratings go first
private:
    struct Features
    {
        std::array<float, 64> curve {};
        std::array<float, 5> makeUp {};          // steps, curves, straight lines, points, motion (each 0..1)
    };
    static Features featuresOf (const Shape&);
    static float similarity (const Features&, const Style&, const Features&, const Style&);
    struct Entry { Shape shape; Style style; bool liked = true; Features features; };
    std::vector<Entry> entries;
};
}
