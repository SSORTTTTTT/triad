#pragma once
#include "Modulation.h"

// The shape generator behind the LFO tab's dice: new shapes in the style Triad was tuned on (Thomas's
// Infiltrator favourites: 846 drawn shapes), never copies of them. Shapes are built from gestures
// on the grid (gates, steps, stairs, saws, triangles, ramps at 30 / 45 / 60 degrees, jump-and-decay
// curves, waves) laid out on a musical rhythm, usually repeated with small variations and a fill.
namespace triad::mod
{
struct Style
{
    float angular = 1.0f;   // 0..1  hard corners: vertical steps, gates, triangles, straight 30/45/60-degree lines
    float soft = 0.0f;      // 0..1  curves and waves; with Angular too, hard jumps into curves
    float chaos = 0.35f;    // 0..1  more points, faster and less regular rhythm, more levels, fewer repeats
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
}
