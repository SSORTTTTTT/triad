#include "Modulation.h"
#include "GeneratedShapes.h"
#include <array>
#include <cstring>

// Triad's preset LFO shapes. Mostly hard angles: squares and gates (90-degree corners), stairs,
// triangles and lines at 30, 45 and 60 degrees, hard jumps into slopes and curves, plus a few soft ones.
//
// Shapes are written on a 16 x 16 grid: columns are 1/16 notes of a one-bar LFO, rows are 1/16 of the
// height. Triad's grid is 1.87 times wider than tall, so on screen a line that climbs 1 row per column
// is about 30 degrees, 2 rows per column 45 degrees, 3 rows per column 60 degrees.
namespace triad::mod
{
namespace
{
// One character per column: '.' or '0' = 0%, '1'..'7' = n/8, '8' or 'x' = 100%.
Shape steps (const char* pattern)
{
    const int n = (int) std::strlen (pattern);
    auto level = [] (char c) { return c == '.' ? 0.0f : c == 'x' ? 1.0f : (float) (c - '0') / 8.0f; };
    Shape s;
    for (int i = 0; i < n; ++i)
    {
        const float x = (float) i / (float) n, y = level (pattern[i]);
        if (s.empty()) s.push_back ({ 0.0f, y, 0.0f });
        else if (! juce::exactlyEqual (s.back().y, y))
        {
            s.push_back ({ x, s.back().y, 0.0f });
            s.push_back ({ x, y, 0.0f });
        }
    }
    s.push_back ({ 1.0f, s.back().y, 0.0f });
    normalise (s);
    return removeRedundant (s);
}

struct G { float col, row, curve = 0.0f; };
Shape grid (std::initializer_list<G> points)
{
    Shape s;
    for (const auto& p : points) s.push_back ({ p.col / 16.0f, p.row / 16.0f, p.curve });
    normalise (s);
    return removeRedundant (s);
}

// A sine-like hump from `low` to `high` and back between two columns (four eased quarters).
void hump (std::vector<G>& out, float from, float to, float low, float high, float k = 1.6f)
{
    const float q = (to - from) * 0.25f, mid = 0.5f * (low + high);
    if (out.empty() || ! juce::exactlyEqual (out.back().col, from)) out.push_back ({ from, low, k });
    else out.back() = { from, out.back().row, k };
    out.push_back ({ from + q, mid, -k });
    out.push_back ({ from + 2.0f * q, high, k });
    out.push_back ({ from + 3.0f * q, mid, -k });
    out.push_back ({ to, low, 0.0f });
}
Shape fromGrid (const std::vector<G>& points)
{
    Shape s;
    for (const auto& p : points) s.push_back ({ p.col / 16.0f, p.row / 16.0f, p.curve });
    normalise (s);
    return removeRedundant (s);
}
// Hard jumps into curves, one per cell: `tops` in rows, curve `k` (negative: falls fast and settles).
Shape decays (std::initializer_list<float> widths, std::initializer_list<float> tops, float k)
{
    std::vector<G> out;
    float col = 0.0f;
    auto top = tops.begin();
    for (const float w : widths)
    {
        const float t = top != tops.end() ? *top++ : 16.0f;
        out.push_back ({ col, t, k });   // after the previous cell's end at 0: a vertical jump
        col += w;
        out.push_back ({ col, 0.0f });
    }
    return fromGrid (out);
}

const Shape sine { { 0, 0.5f, -1.6f }, { 0.25f, 1, 1.6f }, { 0.5f, 0.5f, -1.6f }, { 0.75f, 0, 1.6f }, { 1, 0.5f, 0 } };

struct Preset { int category; juce::String name; Shape shape; };
enum Category { basic, squares, stairs, triangles, angles, hardCurve, soft, numHandMade };
constexpr int numCategories = numHandMade + generated::numFolders;
const char* categoryNames[] { "BASIC", "SQUARES", "STEPS", "TRIANGLES", "ANGLES", "HARD + CURVE", "SOFT" };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> p;
        // ---- BASIC (unchanged from earlier versions: projects and tests refer to them by number) ----
        p.push_back ({ basic, "Flat", flat (1.0f) });
        p.push_back ({ basic, "Ramp down", { { 0, 1, 0 }, { 1, 0, 0 } } });
        p.push_back ({ basic, "Ramp up", { { 0, 0, 0 }, { 1, 1, 0 } } });
        p.push_back ({ basic, "Triangle", { { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } } });
        p.push_back ({ basic, "Sine", sine });
        p.push_back ({ basic, "Square", { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } } });
        p.push_back ({ basic, "Pump", { { 0, 0, -4.0f }, { 0.6f, 1, 0 }, { 1, 1, 0 } } });
        p.push_back ({ basic, "Pump 1/4", repeated ({ { 0, 0, -4.0f }, { 0.6f, 1, 0 }, { 1, 1, 0 } }, 4) });
        p.push_back ({ basic, "Saw 1/4", repeated ({ { 0, 1, 0 }, { 1, 0, 0 } }, 4) });
        p.push_back ({ basic, "Gate 1/8", repeated ({ { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } }, 8) });

        // ---- SQUARES: gates, all 90-degree corners ----------------------------------------------------
        p.push_back ({ squares, "Gate 1/4", steps ("xx..xx..xx..xx..") });
        p.push_back ({ squares, "Gate 1/16", steps ("x.x.x.x.x.x.x.x.") });
        p.push_back ({ squares, "Offbeat 1/8", steps ("..xx..xx..xx..xx") });
        p.push_back ({ squares, "Dotted 3-3-2", steps ("xx.xx.x.xx.xx.x.") });
        p.push_back ({ squares, "Trance gate", steps ("x.xx.x.xx.xx.x.x") });
        p.push_back ({ squares, "Stutter end", steps ("xxxx..xxxx..x.x.") });
        p.push_back ({ squares, "Long short", steps ("xxxxxx..xx..xx..") });
        p.push_back ({ squares, "Shrinking", steps ("xxxx.xxx..xx...x") });
        p.push_back ({ squares, "Duck 1/4", steps (".xxx.xxx.xxx.xxx") });
        p.push_back ({ squares, "Double hit", steps ("x.x.....x.x.....") });
        p.push_back ({ squares, "Morse", steps ("xxx.x.xxx...x.x.") });

        // ---- STEPS: stairs and step melodies -----------------------------------------------------------
        p.push_back ({ stairs, "Stairs up", steps ("2222444466668888") });
        p.push_back ({ stairs, "Stairs down", steps ("8888666644442222") });
        p.push_back ({ stairs, "Stairs 8 up", steps ("1122334455667788") });
        p.push_back ({ stairs, "Stairs 8 down", steps ("8877665544332211") });
        p.push_back ({ stairs, "Pyramid", steps ("2244668888664422") });
        p.push_back ({ stairs, "Step melody", steps ("8855773388664422") });
        p.push_back ({ stairs, "Step melody 2", steps ("8463826485736251") });
        p.push_back ({ stairs, "Octave hops", steps ("8844..88448844..") });
        p.push_back ({ stairs, "Stair saw 1/4", steps ("8642864286428642") });
        p.push_back ({ stairs, "Terraces", steps ("4444888822226666") });
        p.push_back ({ stairs, "Steps & rests", steps ("66..88..44..88..") });

        // ---- TRIANGLES: 30, 45 and 60 degrees -----------------------------------------------------------
        p.push_back ({ triangles, "Triangle 30", grid ({ { 0, 0 }, { 8, 8 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Twin peaks 45", grid ({ { 0, 0 }, { 4, 8 }, { 8, 0 }, { 12, 8 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Peak 60", grid ({ { 0, 0 }, { 3, 0 }, { 8, 16 }, { 13, 0 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Steep peaks", grid ({ { 0, 0 }, { 2, 16 }, { 4, 0 }, { 6, 16 }, { 8, 0 }, { 10, 16 }, { 12, 0 }, { 14, 16 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Zigzag 45", grid ({ { 0, 6 }, { 2, 10 }, { 4, 6 }, { 6, 10 }, { 8, 6 }, { 10, 10 }, { 12, 6 }, { 14, 10 }, { 16, 6 } }) });
        p.push_back ({ triangles, "Mountains 45", grid ({ { 0, 0 }, { 4, 8 }, { 6, 4 }, { 12, 16 }, { 16, 8 } }) });
        p.push_back ({ triangles, "Growing peaks", grid ({ { 0, 0 }, { 1, 2 }, { 2, 0 }, { 4, 4 }, { 6, 0 }, { 10, 8 }, { 14, 0 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Plateau 45", grid ({ { 0, 0 }, { 4, 8 }, { 12, 8 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Step peak", grid ({ { 0, 0 }, { 4, 0 }, { 4, 8 }, { 8, 16 }, { 12, 8 }, { 12, 0 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Lean 30/60", grid ({ { 0, 0 }, { 9, 9 }, { 12, 0 }, { 16, 0 } }) });
        p.push_back ({ triangles, "Fins", grid ({ { 0, 0 }, { 4, 8 }, { 6, 0 }, { 8, 0 }, { 12, 8 }, { 14, 0 }, { 16, 0 } }) });

        // ---- ANGLES: a hard 90-degree jump and a straight slope -------------------------------------------
        p.push_back ({ angles, "Saw down 1/8", repeated ({ { 0, 1, 0 }, { 1, 0, 0 } }, 8) });
        p.push_back ({ angles, "Saw up 45", grid ({ { 0, 0 }, { 4, 8 }, { 4, 0 }, { 8, 8 }, { 8, 0 }, { 12, 8 }, { 12, 0 }, { 16, 8 } }) });
        p.push_back ({ angles, "Saw up 60", grid ({ { 0, 0 }, { 4, 12 }, { 4, 0 }, { 8, 12 }, { 8, 0 }, { 12, 12 }, { 12, 0 }, { 16, 12 } }) });
        p.push_back ({ angles, "Ramp hold cut", grid ({ { 0, 0 }, { 8, 8 }, { 12, 8 }, { 12, 0 }, { 16, 0 } }) });
        p.push_back ({ angles, "Cliff 45", grid ({ { 0, 16 }, { 4, 16 }, { 12, 0 }, { 16, 0 } }) });
        p.push_back ({ angles, "Ski jump 30", grid ({ { 0, 0 }, { 12, 12 }, { 12, 0 }, { 16, 0 } }) });
        p.push_back ({ angles, "Linear pump", grid ({ { 0, 0 }, { 8, 16 }, { 16, 16 } }) });
        p.push_back ({ angles, "Slope + gates", grid ({ { 0, 16 }, { 8, 0 }, { 8, 16 }, { 10, 16 }, { 10, 0 }, { 12, 0 }, { 12, 16 }, { 14, 16 }, { 14, 0 }, { 16, 0 } }) });
        p.push_back ({ angles, "Step slope 30", grid ({ { 0, 16 }, { 4, 12 }, { 4, 6 }, { 8, 2 }, { 8, 14 }, { 12, 10 }, { 12, 4 }, { 16, 0 } }) });
        p.push_back ({ angles, "Gate then ramp", grid ({ { 0, 16 }, { 4, 16 }, { 4, 0 }, { 8, 8 }, { 8, 16 }, { 12, 16 }, { 12, 0 }, { 16, 8 } }) });
        p.push_back ({ angles, "Gap & rise 45", grid ({ { 0, 16 }, { 4, 16 }, { 4, 0 }, { 6, 0 }, { 14, 16 }, { 16, 16 } }) });

        // ---- HARD + CURVE: a hard jump into a curve or a wave -------------------------------------------
        p.push_back ({ hardCurve, "Decay 1/4", repeated ({ { 0, 1, -4.5f }, { 1, 0, 0 } }, 4) });
        p.push_back ({ hardCurve, "Decay 1/8", repeated ({ { 0, 1, -4.0f }, { 1, 0, 0 } }, 8) });
        p.push_back ({ hardCurve, "Round drop 1/4", repeated ({ { 0, 1, 4.0f }, { 1, 0, 0 } }, 4) });
        p.push_back ({ hardCurve, "Swell cut 1/4", repeated ({ { 0, 0, 3.5f }, { 1, 1, 0 } }, 4) });
        p.push_back ({ hardCurve, "Gate tail 1/4", repeated ({ { 0, 1, 0 }, { 0.5f, 1, -3.5f }, { 1, 0, 0 } }, 4) });
        p.push_back ({ hardCurve, "Step + wave", []
        {
            std::vector<G> g { { 0, 0 }, { 4, 0 }, { 4, 8 }, { 6, 8 } };
            hump (g, 6, 10, 8, 12);
            hump (g, 10, 14, 8, 12);
            g.push_back ({ 16, 8 });
            return fromGrid (g);
        }() });
        p.push_back ({ hardCurve, "Cascade", decays ({ 4, 4, 4, 4 }, { 4, 8, 12, 16 }, -3.5f) });
        p.push_back ({ hardCurve, "Bounce", decays ({ 4, 4, 4, 4 }, { 16, 11, 7, 4 }, -3.5f) });
        p.push_back ({ hardCurve, "Pluck 3-3-2", decays ({ 3, 3, 2, 3, 3, 2 }, { 16, 16, 16, 16, 16, 16 }, -4.0f) });
        p.push_back ({ hardCurve, "Pulses then fade", grid ({ { 0, 16 }, { 1, 16 }, { 1, 0 }, { 2, 0 }, { 2, 16 }, { 3, 16 }, { 3, 0 }, { 4, 0 },
                                                              { 4, 16 }, { 5, 16 }, { 5, 0 }, { 6, 0 }, { 6, 16 }, { 7, 16 }, { 7, 0 }, { 8, 0 },
                                                              { 8, 16, -3.0f }, { 16, 0 } }) });
        p.push_back ({ hardCurve, "Slope into curve", grid ({ { 0, 16 }, { 4, 8, -3.0f }, { 8, 0 }, { 8, 16 }, { 12, 8, -3.0f }, { 16, 0 } }) });

        // ---- SOFT: curves and waves -------------------------------------------------------------------------
        p.push_back ({ soft, "Sine 2x", repeated (sine, 2) });
        p.push_back ({ soft, "Sine 3x", repeated (sine, 3) });
        p.push_back ({ soft, "Sine 4x", repeated (sine, 4) });
        p.push_back ({ soft, "Hump", [] { std::vector<G> g; hump (g, 0, 16, 0, 16); return fromGrid (g); }() });
        p.push_back ({ soft, "Swell", grid ({ { 0, 0, 3.0f }, { 8, 9, -1.6f }, { 11, 16, 1.6f }, { 13.5f, 8, -1.6f }, { 16, 0 } }) });
        p.push_back ({ soft, "Breath", grid ({ { 0, 0, 1.6f }, { 3, 8, -1.6f }, { 6, 16 }, { 10, 16, 1.6f }, { 13, 8, -1.6f }, { 16, 0 } }) });
        p.push_back ({ soft, "Wobble 1/8", [] { std::vector<G> g; for (int i = 0; i < 8; ++i) hump (g, (float) i * 2, (float) i * 2 + 2, 0, 16); return fromGrid (g); }() });
        p.push_back ({ soft, "Glide steps", grid ({ { 0, 4 }, { 2, 4, 1.6f }, { 3, 8, -1.6f }, { 4, 12 }, { 6, 12, 1.6f }, { 7, 10, -1.6f }, { 8, 8 },
                                                    { 10, 8, 1.6f }, { 11, 12, -1.6f }, { 12, 16 }, { 14, 16, 1.6f }, { 15, 10, -1.6f }, { 16, 4 } }) });
        p.push_back ({ soft, "Ripple", []
        {
            std::vector<G> g;
            const float heights[] { 16, 9, 5, 3 };
            for (int i = 0; i < 4; ++i) hump (g, (float) i * 4, (float) i * 4 + 4, 0, heights[i]);
            return fromGrid (g);
        }() });
        p.push_back ({ soft, "Pluck soft", grid ({ { 0, 0, -2.5f }, { 4, 16, 2.5f }, { 16, 0 } }) });
        // ---- GEN folders: shapes from the generator (GeneratedShapes.h) -----------------------------------
        std::array<int, (size_t) juce::jmax (1, generated::numFolders)> counts {};
        for (const auto& e : generated::entries)
        {
            if (e.folder < 0 || e.folder >= generated::numFolders) continue;
            Shape shape;
            for (const auto& point : juce::StringArray::fromTokens (e.points, ";", ""))
            {
                const auto v = juce::StringArray::fromTokens (point, " ", "");
                if (v.size() == 3) shape.push_back ({ v[0].getFloatValue(), v[1].getFloatValue(), v[2].getFloatValue() });
            }
            if (shape.size() < 2) continue;
            const int n = ++counts[(size_t) e.folder];
            p.push_back ({ numHandMade + e.folder, juce::String (generated::folderWords[e.folder]) + " " + juce::String (n).paddedLeft ('0', 2), shape });
        }
        return p;
    }();
    return list;
}
}

int numPresets() { return (int) presets().size(); }
juce::String presetName (int i) { return i >= 0 && i < numPresets() ? presets()[(size_t) i].name : ""; }
Shape preset (int i) { return i >= 0 && i < numPresets() ? presets()[(size_t) i].shape : flat(); }
int numPresetCategories() { return numCategories; }
juce::String presetCategoryName (int c)
{
    if (c >= 0 && c < numHandMade) return categoryNames[c];
    return c >= numHandMade && c < numCategories ? generated::folderNames[c - numHandMade] : "";
}
bool isGeneratedCategory (int c) { return c >= numHandMade && c < numCategories; }
int presetCategory (int i) { return i >= 0 && i < numPresets() ? presets()[(size_t) i].category : -1; }
std::vector<int> presetsIn (int category)
{
    std::vector<int> list;
    for (int i = 0; i < numPresets(); ++i)
        if (presets()[(size_t) i].category == category) list.push_back (i);
    return list;
}
}
