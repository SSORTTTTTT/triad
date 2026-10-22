#include "Generator.h"
#include "GeneratorData.h"
#include "TimePresets.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <cstring>

namespace triad::mod
{
namespace
{
constexpr float toRadians = juce::MathConstants<float>::pi / 180.0f;

// What fills one cell of the rhythm.
enum class Kind
{
    rest,                                                                         // a pause at the bottom
    gate, step, stairs, sawDown, sawUp, triangle, trapezoid, ramp, zigzag,         // hard: steps, holds, straight lines
    decay, swellCut, gateTail, stepWave,                                          // a hard jump into a curve
    hump, wave, plateau, swell, ripple, glide, pluck,                             // soft: curves only
    count
};
enum class Family { hard, hybrid, soft };

Family familyOf (Kind k)
{
    if (k <= Kind::zigzag) return Family::hard;
    if (k <= Kind::stepWave) return Family::hybrid;
    return Family::soft;
}

// How often each gesture comes up within its family. Tuned on the favourites: after a jump up, 53% of
// the time the line holds (gates, steps), 29% it curves down (decays) and 15% it falls in a straight
// line (saws); 73% of gestures are followed by the same kind. Triangles, squares and 30/45-degree lines
// are weighted up, as asked for.
float baseWeight (Kind k)
{
    switch (k)
    {
        case Kind::gate:      return 0.26f;
        case Kind::step:      return 0.16f;
        case Kind::stairs:    return 0.09f;
        case Kind::sawDown:   return 0.12f;
        case Kind::sawUp:     return 0.05f;
        case Kind::triangle:  return 0.18f;
        case Kind::trapezoid: return 0.09f;
        case Kind::ramp:      return 0.03f;
        case Kind::zigzag:    return 0.02f;
        case Kind::decay:     return 0.45f;
        case Kind::swellCut:  return 0.18f;
        case Kind::gateTail:  return 0.17f;
        case Kind::stepWave:  return 0.20f;
        case Kind::hump:      return 0.22f;
        case Kind::wave:      return 0.22f;
        case Kind::plateau:   return 0.14f;
        case Kind::swell:     return 0.14f;
        case Kind::ripple:    return 0.10f;
        case Kind::glide:     return 0.14f;
        case Kind::pluck:     return 0.16f;
        case Kind::rest: case Kind::count: break;
    }
    return 0.0f;
}

int minWidth (Kind k)
{
    if (k == Kind::trapezoid || k == Kind::ripple) return 3;
    if (k == Kind::gate || k == Kind::stairs || k == Kind::zigzag || k == Kind::wave || k == Kind::plateau
        || k == Kind::gateTail || k == Kind::stepWave || k == Kind::glide) return 2;
    return 1;
}

struct Cell
{
    int start = 0, width = 1;            // columns
    Kind kind = Kind::gate;
    float top = 1.0f, bottom = 0.0f;
    float angle = 45.0f, angle2 = 45.0f; // degrees on screen, for straight lines
    float duty = 0.5f;                   // the part of the cell a gate is on, a decay falls, ...
    float bend = -4.0f;                  // curve amount (sign picks the flavour)
    int count = 2;                       // steps, teeth, waves
    int pulses = 1;                      // a gate's squares (more with Chaos in Triad's LFOs)
    bool flag = false;                   // direction / variant
    int shade = 0;                       // soft gestures: 0 rise from the floor, 1 hang from the top, 2 swing around the middle
};

// Points on the grid, left to right. Two points at the same x make a vertical step.
class Builder
{
public:
    explicit Builder (const GridInfo& g) : grid (g) {}

    void start (float y) { points = { Point { 0.0f, snap (y), 0.0f } }; position = 0.0; }
    float level() const { return points.back().y; }
    double at() const { return position; }

    // A line (curved by `curve`) from the last point to column `col`, level `y`;
    // at the current column it is a vertical step.
    void to (double col, float y, float curve = 0.0f)
    {
        y = snap (y);
        auto& last = points.back();
        if (col <= position + 1.0e-9)
        {
            if (juce::exactlyEqual (last.y, y)) return;
            if (points.size() == 1) { last.y = y; return; }                      // no step at x = 0
            if (juce::exactlyEqual (points[points.size() - 2].x, last.x))
            {
                last.y = y;                                                       // already a step here: move its end
                if (juce::exactlyEqual (points[points.size() - 2].y, y)) points.pop_back();
                return;
            }
            last.curve = 0.0f;
            points.push_back ({ last.x, y, 0.0f });
            return;
        }
        last.curve = juce::exactlyEqual (last.y, y) ? 0.0f : curve;
        points.push_back ({ (float) (col / grid.columns), y, 0.0f });
        position = col;
    }
    void jump (float y) { to (position, y); }
    void hold (double col) { to (col, level()); }

    float snap (float y) const { return juce::jlimit (0.0f, 1.0f, std::round (y * (float) grid.rows) / (float) grid.rows); }
    float heightFor (double cols, float angle) const
    {
        return (float) (cols / grid.columns) * grid.aspect * std::tan (angle * toRadians);
    }
    double columnsFor (float height, float angle) const
    {
        return (double) height / (grid.aspect * std::tan (angle * toRadians)) * grid.columns;
    }
    // A straight line at `angle` from the current level toward `target`, ending by column `end`: it
    // stops at the target and holds there, or stops at `end` on the way.
    void slope (double end, float target, float angle)
    {
        const float from = level();
        const float height = std::abs (target - from);
        const double room = end - position;
        if (height <= 0.0f || room <= 0.0) { hold (end); return; }
        const double cols = columnsFor (height, angle);
        if (cols <= room)
        {
            const double whole = room >= 1.0 ? juce::jlimit (1.0, room, std::round (cols)) : room;
            to (position + whole, target);
            hold (end);
        }
        else
        {
            const float h = heightFor (room, angle);
            to (end, target > from ? from + h : from - h);
        }
    }

    Shape points;
private:
    const GridInfo& grid;
    double position = 0.0;
};

class Generator
{
public:
    Generator (const Style& s, const GridInfo& g, juce::Random& r) : style (s), grid (g), random (r)
    {
        style.angular = juce::jlimit (0.0f, 1.0f, style.angular);
        style.soft = juce::jlimit (0.0f, 1.0f, style.soft);
        style.chaos = juce::jlimit (0.0f, 1.0f, style.chaos);
        grid.columns = juce::jlimit (2, 256, grid.columns);
        grid.rows = juce::jlimit (2, 64, grid.rows);
        grid.aspect = juce::jlimit (0.25f, 8.0f, grid.aspect);
        userGrid = grid;
        float a = style.angular, s2 = style.soft;
        if (a + s2 < 0.01f) a = s2 = 0.5f;
        family[0] = a * (1.0f - 0.5f * s2);   // hard
        family[1] = 0.7f * a * s2;            // hybrid
        family[2] = s2 * (1.0f - 0.5f * a);   // soft
        softOnly = style.angular < 0.05f && style.soft >= 0.05f;
    }

    Shape run()
    {
        // A few tries: a shape that came out (nearly) flat is thrown away.
        Shape shape;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            shape = attempt_();
            if (acceptable (shape)) break;
        }
        return shape;
    }

private:
    static bool acceptable (const Shape& s)
    {
        if (s.size() < 3) return false;
        float low = 1.0f, high = 0.0f;
        int moves = 0;
        for (size_t i = 0; i < s.size(); ++i)
        {
            low = juce::jmin (low, s[i].y);
            high = juce::jmax (high, s[i].y);
            if (i > 0 && ! juce::exactlyEqual (s[i].y, s[i - 1].y)) ++moves;
        }
        return high - low >= 0.45f && moves >= 2;
    }

    Shape attempt_()
    {
        // High chaos may go faster than the grid: half columns (1/32 notes on a 1/16 grid).
        grid = userGrid;
        if (grid.columns <= 32 && random.nextFloat() < (style.chaos - 0.5f) * 2.0f) grid.columns *= 2;
        makePalette();
        const int n = grid.columns;
        // The main gesture and its angle first: lines at 30 or 45 degrees need room to climb.
        primary = pickKind (n);
        secondary = random.nextFloat() < 0.35f ? primary : pickKind (n);
        motifAngle = pickAngleClass();
        motifAngle2 = random.nextFloat() < 0.7f ? motifAngle : pickAngleClass();
        motifShade = pickWeighted (std::array<float, 3> { 0.5f, 0.25f, 0.25f });
        const int room = roomFor (primary);
        const int events = eventCount();
        const int repeats = repeatCount (events, room);
        const int motifLength = n / repeats;
        const int perMotif = juce::jlimit (1, juce::jmax (1, motifLength / room), juce::roundToInt ((float) events / (float) repeats));

        auto widths = partition (motifLength, perMotif, room);
        // One long line fills the motif: often leave a short cell beside it for a square or a pause.
        if (widths.size() == 1 && room >= 4 && motifLength - room >= 2 && random.nextFloat() < 0.6f)
        {
            const int small = juce::jlimit (2, motifLength - room, motifLength / 4);
            widths = random.nextFloat() < 0.7f ? std::vector<int> { motifLength - small, small } : std::vector<int> { small, motifLength - small };
        }
        base = softOnly || random.nextFloat() < 1.0f - 0.25f * style.chaos ? 0.0f : pickLevel (0.0f, 0.375f);
        std::vector<Cell> motif;
        int x = 0;
        for (const int w : widths) { motif.push_back (makeCell (x, w, (int) motif.size(), (int) widths.size())); x += w; }
        applyContour (motif);

        std::vector<Cell> cells;
        for (int r = 0; r < repeats; ++r)
            for (auto cell : motif)
            {
                cell.start += r * motifLength;
                if (r > 0 && random.nextFloat() < 0.45f * style.chaos) vary (cell);
                cells.push_back (cell);
            }
        if (random.nextFloat() < 0.05f + 0.3f * style.chaos) fill (cells);

        Builder b (grid);
        b.start (entryLevel (cells.front()));
        for (const auto& cell : cells) render (b, cell);
        auto shape = finish (b.points);
        if (! softOnly && random.nextFloat() < 0.1f) shape = flippedVertical (shape);
        return shape;
    }

    // ---- choices --------------------------------------------------------------------------------
    template <typename T>
    int pickWeighted (const T& weights)
    {
        float total = 0.0f;
        for (const float w : weights) total += juce::jmax (0.0f, w);
        if (total <= 0.0f) return 0;
        float roll = random.nextFloat() * total;
        int i = 0;
        for (const float w : weights)
        {
            roll -= juce::jmax (0.0f, w);
            if (roll < 0.0f) return i;
            ++i;
        }
        return i - 1;
    }

    // Chaos (Thomas): more triangles and more squares, each with more points.
    float chaosBoost (Kind k) const
    {
        if (! style.lfoChaos) return 1.0f;
        const float c = style.chaos;
        if (k == Kind::triangle || k == Kind::trapezoid) return 1.0f + 2.0f * c * c;
        if (k == Kind::zigzag) return 1.0f + 8.0f * c * c;
        if (k == Kind::gate || k == Kind::step || k == Kind::stairs) return 1.0f + 1.4f * c * c;
        return 1.0f;
    }
    Kind pickKind (int width)
    {
        std::array<float, (size_t) Kind::count> weights {};
        std::array<float, 3> sums {};
        for (int k = 1; k < (int) Kind::count; ++k) sums[(size_t) familyOf ((Kind) k)] += baseWeight ((Kind) k);
        for (int k = 1; k < (int) Kind::count; ++k)
        {
            const auto f = (size_t) familyOf ((Kind) k);
            if (minWidth ((Kind) k) <= width && sums[f] > 0.0f) weights[(size_t) k] = family[f] * baseWeight ((Kind) k) / sums[f] * chaosBoost ((Kind) k);
        }
        const int k = pickWeighted (weights);
        return k <= 0 ? (softOnly ? Kind::swell : Kind::step) : (Kind) k;
    }

    void makePalette()
    {
        // Levels in use: 0 and 100% always (the favourites use them most), more with chaos.
        palette = { 0.0f, 1.0f };
        const int extra = juce::roundToInt (style.chaos * 6.0f * (0.5f + 0.5f * random.nextFloat())) + (random.nextFloat() < 0.5f ? 1 : 0);
        const int steps = style.chaos < 0.6f ? 8 : 16;
        for (int i = 0; i < extra * 4 && (int) palette.size() < extra + 2; ++i)
        {
            const float level = (float) (1 + random.nextInt (steps - 1)) / (float) steps;
            if (std::find (palette.begin(), palette.end(), level) == palette.end()) palette.push_back (level);
        }
        std::sort (palette.begin(), palette.end());
    }

    float pickLevel (float low, float high)
    {
        std::vector<float> choices;
        for (const float l : palette) if (l >= low - 1.0e-4f && l <= high + 1.0e-4f) choices.push_back (l);
        if (choices.empty()) return juce::jlimit (0.0f, 1.0f, std::round ((low + high) * 4.0f) / 8.0f);
        return choices[(size_t) random.nextInt ((int) choices.size())];
    }

    int eventCount()
    {
        const int most = juce::jmin (grid.columns, 32);
        const float e = 2.0f + (float) (most - 2) * std::pow (style.chaos, 1.4f);
        return juce::jlimit (1, grid.columns, juce::roundToInt (e * (0.8f + 0.4f * random.nextFloat())));
    }

    int repeatCount (int events, int room)
    {
        // Low chaos: a short pattern repeated (2 or 4 times); high chaos: one long pattern.
        const float c = style.chaos;
        const int n = grid.columns;
        std::array<int, 4> options { 1, 2, 4, 8 };
        std::array<float, 4> weights { 0.2f + 0.6f * c, 0.35f, 0.05f + 0.45f * (1.0f - c), n >= 32 ? 0.15f * (1.0f - c) : 0.0f };
        for (size_t i = 0; i < options.size(); ++i)
            if (n % options[i] != 0 || n / options[i] < juce::jmax (2, room) || (options[i] > 1 && events < 2 && c > 0.5f)) weights[i] = 0.0f;
        return options[(size_t) pickWeighted (weights)];
    }

    // Splits `length` columns into `parts` cells the way rhythms are written: halves first, then
    // dotted (3 + 1) and, with chaos, uneven splits.
    std::vector<int> partition (int length, int parts, int smallest)
    {
        smallest = juce::jmax (1, smallest);
        std::vector<int> cells { length };
        const float c = style.chaos;
        auto splittable = [smallest] (int w) { return w >= 2 * smallest; };
        while ((int) cells.size() < parts)
        {
            int index = -1;
            if (random.nextFloat() < 0.3f + 0.5f * c)
            {
                std::vector<float> weights;
                for (const int w : cells) weights.push_back (splittable (w) ? (float) w : 0.0f);
                index = pickWeighted (weights);
                if (! splittable (cells[(size_t) index])) index = -1;
            }
            if (index < 0)
            {
                int widest = 0;
                for (size_t i = 0; i < cells.size(); ++i)
                    if (splittable (cells[i]) && cells[i] > widest) { widest = cells[i]; index = (int) i; }
            }
            if (index < 0) break;
            const int w = cells[(size_t) index];
            int left = w / 2;
            const float roll = random.nextFloat();
            if (w % 4 == 0 && roll < 0.15f + 0.35f * c) left = (random.nextBool() ? 3 : 1) * w / 4;
            else if (w % 3 == 0 && w % 2 != 0) left = (random.nextBool() ? 2 : 1) * w / 3;
            else if (w % 2 != 0) left = w / 2 + (random.nextBool() ? 1 : 0);
            else if (w > 2 && roll > 0.94f - 0.3f * c) left = 1 + random.nextInt (w - 1);
            if (left < smallest || w - left < smallest) left = w / 2;
            left = juce::jlimit (smallest, w - smallest, left);
            cells[(size_t) index] = left;
            cells.insert (cells.begin() + index + 1, w - left);
        }
        return cells;
    }

    // One angle for the lines of a motif, so they run parallel. Thomas: "a lot of 45 degree angles,
    // 30 degree angles"; with chaos (faster rhythms) steeper ones, like the favourites' fast lines.
    float pickAngleClass()
    {
        const float c = style.chaos;
        const std::array<float, 4> angles { 30.0f, 45.0f, 60.0f, 75.0f };
        return angles[(size_t) pickWeighted (std::array<float, 4> { 0.42f - 0.22f * c, 0.48f - 0.13f * c, 0.08f + 0.17f * c, 0.02f + 0.18f * c })];
    }

    static constexpr float visibleRise = 0.45f;    // a line climbs at least this much of the height

    double lineColumns (float angle) const
    {
        return (double) visibleRise / (grid.aspect * std::tan (angle * toRadians)) * grid.columns;
    }

    static bool isLine (Kind k)
    {
        return k == Kind::triangle || k == Kind::sawDown || k == Kind::sawUp || k == Kind::ramp || k == Kind::trapezoid;
    }

    // The columns a cell of this kind needs so its lines, at the motif's angle, climb far enough.
    int roomFor (Kind k) const
    {
        double cols = minWidth (k);
        if (k == Kind::triangle) cols = juce::jmax (cols, lineColumns (motifAngle) + lineColumns (motifAngle2));
        else if (k == Kind::sawDown || k == Kind::sawUp || k == Kind::ramp) cols = juce::jmax (cols, lineColumns (motifAngle));
        else if (k == Kind::trapezoid) cols = juce::jmax (cols, 2.0 + lineColumns (motifAngle));
        else if (familyOf (k) == Family::soft && style.chaos < 0.6f) cols = juce::jmax (cols, 2.0);
        return juce::jlimit (1, grid.columns, (int) std::ceil (cols - 1.0e-6));
    }

    // The motif's angle, or the next steeper one (60, then 75 degrees) when the line has fewer columns
    // than it needs (fills, short cells).
    float angleFor (double columns, float angle) const
    {
        const float run = (float) (columns / grid.columns) * grid.aspect;
        if (run <= 0.0f) return 84.0f;
        const float needed = std::atan (visibleRise / run) / toRadians;
        if (needed <= angle) return angle;
        for (const float a : { 45.0f, 60.0f, 75.0f })
            if (a >= angle && a + 3.0f >= needed) return a;
        return juce::jmin (84.0f, needed);
    }

    float bendFor (Kind k)
    {
        const float s = style.soft;
        switch (familyOf (k))
        {
            case Family::hybrid:
            {
                // Decays mostly fall fast and settle (exponential); some stay up and then plunge.
                const float amount = (2.0f + 3.0f * s) * (0.8f + 0.4f * random.nextFloat());
                return random.nextFloat() < 0.65f ? -amount : amount;
            }
            case Family::soft: return (1.2f + 1.4f * s) * (0.85f + 0.3f * random.nextFloat());
            case Family::hard: break;
        }
        return 0.0f;
    }

    Cell makeCell (int start, int width, int index, int cellsInMotif)
    {
        Cell c;
        c.start = start;
        c.width = width;
        const float roll = random.nextFloat();
        const float keep = 0.85f - 0.3f * style.chaos;
        const float restChance = (0.1f + 0.12f * style.chaos) * (softOnly ? 0.4f : 1.0f);
        if (index > 0 && cellsInMotif >= 3 && random.nextFloat() < restChance) c.kind = Kind::rest;
        else if (roll < keep) c.kind = primary;
        else if (roll < keep + 0.75f * (1.0f - keep)) c.kind = secondary;
        else c.kind = pickKind (width);
        if (c.kind != Kind::rest && minWidth (c.kind) > width) c.kind = pickKind (width);
        // Chaos: a triangle becomes a run of triangles (more peaks, more points).
        if (style.lfoChaos && c.kind == Kind::triangle && width >= 3 && random.nextFloat() < (style.chaos - 0.35f) * 1.5f) c.kind = Kind::zigzag;
        // A line squeezed into a short cell would have to be steep: mostly make it a square instead.
        if (isLine (c.kind) && width < roomFor (c.kind) && random.nextFloat() < 0.95f)
            c.kind = std::array<Kind, 3> { Kind::gate, Kind::rest, Kind::step }[(size_t) pickWeighted (std::array<float, 3> { width >= 2 ? 0.5f : 0.0f, 0.3f, 0.2f })];
        setDetails (c);
        return c;
    }

    void setDetails (Cell& c)
    {
        const double w = c.width;
        c.bottom = base;
        c.top = 1.0f;
        c.bend = bendFor (c.kind);
        c.flag = random.nextBool();
        c.shade = motifShade;
        switch (c.kind)
        {
            case Kind::gate:
            {
                const std::array<float, 4> duties { 0.5f, 0.25f, 0.75f, c.width % 3 == 0 ? 0.3334f : 0.5f };
                c.duty = duties[(size_t) pickWeighted (std::array<float, 4> { 0.45f, 0.2f, 0.25f, 0.1f })];
                // Chaos: a train of squares in the cell instead of one.
                c.pulses = 1;
                if (style.lfoChaos && c.width >= 4 && random.nextFloat() < (style.chaos - 0.35f) * 1.4f)
                    c.pulses = juce::jlimit (2, c.width / 2, 2 + random.nextInt (1 + juce::roundToInt (style.chaos * 3.0f)));
                break;
            }
            case Kind::stairs:
                c.count = juce::jlimit (2, c.width, c.width <= 4 ? c.width : 3 + random.nextInt (2 + juce::roundToInt (style.chaos * 3.0f)));
                c.flag = random.nextFloat() < 0.6f;   // up
                break;
            case Kind::sawDown: case Kind::sawUp: case Kind::ramp:
                c.angle = angleFor (w, motifAngle);
                break;
            case Kind::triangle:
            {
                const double share = w * lineColumns (motifAngle) / (lineColumns (motifAngle) + lineColumns (motifAngle2));
                c.angle = angleFor (share, motifAngle);
                c.angle2 = angleFor (w - share, motifAngle2);
                break;
            }
            case Kind::trapezoid:
                c.angle = angleFor (juce::jmax (1.0, w - 2.0), motifAngle);
                c.angle2 = angleFor (juce::jmax (1.0, (w - 1.0) * 0.5), motifAngle2);
                c.duty = 0.35f + 0.4f * random.nextFloat();
                break;
            case Kind::zigzag:
                c.count = style.lfoChaos ? juce::jlimit (1, c.width, juce::jmax (2, juce::roundToInt ((float) c.width * (0.15f + 0.45f * style.chaos))))
                                         : juce::jlimit (1, c.width, juce::roundToInt ((float) c.width * (0.15f + 0.35f * style.chaos)));
                break;
            case Kind::glide:
                c.duty = -1.0f;   // the middle level, set with the heights
                break;
            case Kind::decay:
                c.duty = random.nextFloat() < 0.6f ? 1.0f : random.nextBool() ? 0.75f : 0.5f;
                break;
            case Kind::gateTail:
                c.duty = random.nextBool() ? 0.5f : 0.25f;
                break;
            case Kind::stepWave:
                c.count = juce::jlimit (1, juce::jmax (1, c.width / 2), 1 + random.nextInt (3));
                c.duty = random.nextBool() ? 0.125f : 0.1875f;   // wave depth
                break;
            case Kind::wave:
                c.count = juce::jlimit (1, c.width, random.nextBool() ? 2 : c.width / 2);
                break;
            case Kind::ripple:
                c.count = juce::jlimit (2, juce::jmax (2, c.width), 2 + random.nextInt (3));
                break;
            case Kind::plateau:
                c.duty = 0.25f + 0.15f * random.nextFloat();
                break;
            case Kind::swell:
                c.duty = 0.5f + 0.3f * random.nextFloat();
                break;
            case Kind::rest: case Kind::step: case Kind::swellCut: case Kind::hump: case Kind::pluck: case Kind::count:
                break;
        }
    }

    // Heights across the motif's cells: all the same, rising, falling, alternating, or a melody.
    void applyContour (std::vector<Cell>& motif)
    {
        const float c = style.chaos;
        const int mode = pickWeighted (std::array<float, 5> { 1.0f - 0.6f * c, 0.12f + 0.15f * c, 0.08f + 0.15f * c,
                                                              0.1f + 0.2f * c, 0.05f + 0.55f * c });
        const float flatTop = random.nextFloat() < 0.8f ? 1.0f : random.nextBool() ? 0.75f : 0.5f;
        const float low = std::array<float, 3> { 0.25f, 0.375f, 0.5f }[(size_t) random.nextInt (3)];
        const float other = std::array<float, 3> { 0.5f, 0.625f, 0.75f }[(size_t) random.nextInt (3)];
        const int count = (int) motif.size();
        float previous = -1.0f;
        for (int i = 0; i < count; ++i)
        {
            auto& cell = motif[(size_t) i];
            const float f = count > 1 ? (float) i / (float) (count - 1) : 1.0f;
            float top = flatTop;
            switch (mode)
            {
                case 1: top = low + (1.0f - low) * f; break;
                case 2: top = 1.0f - (1.0f - low) * f; break;
                case 3: top = i % 2 == 0 ? 1.0f : other; break;
                case 4:
                {
                    for (int tries = 0; tries < 4; ++tries)
                    {
                        top = pickLevel (0.25f, 1.0f);
                        if (! juce::exactlyEqual (top, previous)) break;
                    }
                    break;
                }
                default: break;
            }
            // Steps play a melody of levels whatever the contour.
            if (cell.kind == Kind::step)
                for (int tries = 0; tries < 4; ++tries)
                {
                    top = pickLevel (0.125f, 1.0f);
                    if (! juce::exactlyEqual (top, previous)) break;
                }
            cell.top = juce::jlimit (cell.bottom + 0.25f, 1.0f, top);
            if (cell.bottom > cell.top - 0.25f) cell.bottom = juce::jmax (0.0f, cell.top - 0.25f);
            if (cell.kind == Kind::glide) cell.duty = pickLevel (cell.bottom + 0.125f, cell.top - 0.125f);
            previous = cell.top;
        }
    }

    void vary (Cell& cell)
    {
        if (random.nextBool())
            cell.top = juce::jlimit (cell.bottom + 0.25f, 1.0f, pickLevel (0.25f, 1.0f));
        else
        {
            const int width = cell.width;
            cell.kind = random.nextBool() ? secondary : pickKind (width);
            if (minWidth (cell.kind) > width) cell.kind = pickKind (width);
            if (isLine (cell.kind) && width < roomFor (cell.kind)) cell.kind = width >= 2 ? Kind::gate : Kind::step;
            const float top = cell.top;
            setDetails (cell);
            cell.top = top;
        }
    }

    // A fill at the end: the last cell's gesture again, two or four times as fast.
    void fill (std::vector<Cell>& cells)
    {
        const auto last = cells.back();
        if (last.width < 2) return;
        int parts = last.width >= 4 && random.nextBool() ? 4 : 2;
        while (last.width % parts != 0) --parts;
        if (parts < 2) return;
        const int width = last.width / parts;
        Kind kind = last.kind;
        if (kind == Kind::rest || minWidth (kind) > width || (isLine (kind) && width < roomFor (kind)))
        {
            const int dominant = family[0] >= family[1] && family[0] >= family[2] ? 0 : family[1] >= family[2] ? 1 : 2;
            kind = dominant == 0 ? (width >= 2 ? Kind::gate : Kind::step) : dominant == 1 ? Kind::decay : Kind::hump;
        }
        cells.pop_back();
        const bool rising = random.nextBool();
        for (int i = 0; i < parts; ++i)
        {
            Cell c = last;
            c.start = last.start + i * width;
            c.width = width;
            c.kind = minWidth (kind) <= width ? kind : pickKind (width);
            setDetails (c);
            c.bottom = last.bottom;
            c.top = rising ? juce::jlimit (c.bottom + 0.25f, 1.0f, last.top * (float) (i + 1) / (float) parts + 0.25f) : last.top;
            cells.push_back (c);
        }
    }

    // ---- drawing --------------------------------------------------------------------------------
    // Soft gestures are written rising from their floor; `shade` hangs them from the top instead (dips,
    // like a sidechain pump) or swings them around the middle (waves), with the same start and end level.
    static float shaded (const Cell& c, float y, float sign = 1.0f)
    {
        switch (c.shade)
        {
            case 1:  return 1.0f + c.bottom - y;
            case 2:  return 0.5f + 0.5f * (y - c.bottom) * sign;
            default: return y;
        }
    }
    float entryLevel (const Cell& c) const
    {
        const bool soft = familyOf (c.kind) == Family::soft || (c.kind == Kind::rest && softOnly);
        return soft ? shaded (c, c.bottom) : c.bottom;
    }

    void enterAt (Builder& b, float y)
    {
        // Soft gestures start from where the line is; elsewhere a hard jump is fine.
        if (! juce::exactlyEqual (b.level(), b.snap (y))) b.jump (y);
    }

    void hump (Builder& b, double from, double to, float low, float high, float k)
    {
        const double q = (to - from) * 0.25;
        const float mid = 0.5f * (low + high);
        b.to (from + q, mid, k);
        b.to (from + 2.0 * q, high, -k);
        b.to (from + 3.0 * q, mid, k);
        b.to (to, low, -k);
    }

    void render (Builder& b, const Cell& c)
    {
        const double c0 = c.start, w = c.width, c1 = c0 + w;
        const float top = c.top, bottom = c.bottom;
        switch (c.kind)
        {
            case Kind::rest:
                if (softOnly) enterAt (b, shaded (c, bottom)); else b.jump (bottom);
                b.hold (c1);
                break;
            case Kind::gate:
            {
                const int pulses = juce::jmax (1, c.pulses);
                for (int i = 0; i < pulses; ++i)
                {
                    const double start = c0 + std::round (w * i / pulses), end = c0 + std::round (w * (i + 1) / pulses);
                    const double each = end - start;
                    const double on = juce::jlimit (1.0, juce::jmax (1.0, each - 1.0), std::round (each * c.duty));
                    b.hold (start);
                    b.jump (top);
                    b.hold (start + on);
                    b.jump (bottom);
                }
                b.hold (c1);
                break;
            }
            case Kind::step:
                b.jump (top);
                b.hold (c1);
                break;
            case Kind::stairs:
            {
                const int n = juce::jlimit (2, juce::jmax (2, c.width), c.count);
                for (int i = 0; i < n; ++i)
                {
                    const float y = c.flag ? bottom + (top - bottom) * (float) (i + 1) / (float) n
                                           : top - (top - bottom) * (float) i / (float) n;
                    b.jump (y);
                    b.hold (c0 + std::round (w * (double) (i + 1) / (double) n));
                }
                break;
            }
            case Kind::sawDown:
                b.jump (top);
                b.slope (c1, bottom, c.angle);
                break;
            case Kind::sawUp:
                b.jump (bottom);
                b.slope (c1, top, c.angle);
                b.jump (bottom);
                break;
            case Kind::ramp:
            {
                const bool up = b.level() < 0.5f * (top + bottom) || (juce::exactlyEqual (b.level(), 0.5f * (top + bottom)) && c.flag);
                b.slope (c1, up ? top : bottom, c.angle);
                break;
            }
            case Kind::triangle:
            {
                // Lines at the two angles meet at the peak; when that would be above the top, the peak is
                // cut flat (both lines keep their angles).
                b.jump (bottom);
                const double k1 = b.columnsFor (1.0f, c.angle), k2 = b.columnsFor (1.0f, c.angle2);
                float height = juce::jmin (top - bottom, (float) (w / (k1 + k2)));
                double rise = height * k1, fall = height * k2;
                if (w >= 2.0)
                {
                    rise = juce::jmax (1.0, std::round (rise));
                    fall = juce::jmax (1.0, std::round (fall));
                    while (rise + fall > w) { if (rise >= fall) rise -= 1.0; else fall -= 1.0; }
                }
                b.to (c0 + rise, bottom + height);
                b.hold (c1 - fall);
                b.to (c1, bottom);
                break;
            }
            case Kind::trapezoid:
            {
                // Up at an angle, hold, then down at an angle (or a hard cut).
                b.jump (bottom);
                const double k1 = b.columnsFor (1.0f, c.angle), k2 = c.flag ? b.columnsFor (1.0f, c.angle2) : 0.0;
                const float height = juce::jmin (top - bottom, (float) ((w - 1.0) / (k1 + k2)));
                double rise = juce::jmax (1.0, std::round (height * k1));
                double fall = c.flag ? juce::jmax (1.0, std::round (height * k2)) : 0.0;
                while (rise + fall > w - 1.0 && rise + fall > 1.0) { if (rise >= fall) rise -= 1.0; else fall -= 1.0; }
                b.to (c0 + juce::jmax (1.0, rise), bottom + height);
                if (c.flag)
                {
                    b.hold (c1 - fall);
                    b.to (c1, bottom);
                }
                else
                {
                    const double held = juce::jlimit (1.0, juce::jmax (1.0, w - rise), std::round ((w - rise) * (0.5 + 0.5 * c.duty)));
                    b.hold (c0 + rise + held);
                    b.jump (bottom);
                    b.hold (c1);
                }
                break;
            }
            case Kind::zigzag:
            {
                const int n = juce::jmax (1, c.count);
                const double tooth = w / n;
                b.jump (bottom);
                for (int i = 0; i < n; ++i)
                {
                    b.to (c0 + tooth * (i + 0.5), top);
                    b.to (c0 + tooth * (i + 1), bottom);
                }
                break;
            }
            case Kind::decay:
            {
                const double fall = juce::jlimit (1.0, w, std::round (w * c.duty));
                b.jump (top);
                b.to (c0 + fall, bottom, c.bend);
                b.hold (c1);
                break;
            }
            case Kind::swellCut:
                b.jump (bottom);
                b.to (c1, top, std::abs (c.bend));
                b.jump (bottom);
                break;
            case Kind::gateTail:
            {
                const double on = juce::jlimit (1.0, w - 1.0, std::round (w * c.duty));
                b.jump (top);
                b.hold (c0 + on);
                b.to (c1, bottom, -std::abs (c.bend));
                break;
            }
            case Kind::stepWave:
            {
                const float level = juce::jlimit (0.25f, 1.0f, top - 0.25f);
                const float depth = c.duty;
                const bool down = level + depth > 1.0f;
                const double lead = juce::jmax (1.0, std::round (w * 0.25));
                b.jump (level);
                b.hold (c0 + lead);
                const int n = juce::jmax (1, c.count);
                const double each = (w - lead) / n;
                const float k = 1.2f + 1.4f * style.soft;
                for (int i = 0; i < n; ++i)
                    hump (b, c0 + lead + each * i, c0 + lead + each * (i + 1), level, down ? level - depth : level + depth, k);
                break;
            }
            case Kind::hump:
                enterAt (b, shaded (c, bottom));
                hump (b, c0, c1, shaded (c, bottom), shaded (c, top), c.bend);
                break;
            case Kind::wave:
            {
                enterAt (b, shaded (c, bottom));
                const int n = juce::jmax (1, c.count);
                for (int i = 0; i < n; ++i)
                    hump (b, c0 + w * i / n, c0 + w * (i + 1) / n, shaded (c, bottom), shaded (c, top, i % 2 == 0 ? 1.0f : -1.0f), c.bend);
                break;
            }
            case Kind::ripple:
            {
                enterAt (b, shaded (c, bottom));
                const int n = juce::jmax (2, c.count);
                for (int i = 0; i < n; ++i)
                {
                    const float height = (top - bottom) * std::pow (0.55f, (float) i);
                    if (height < 0.06f) { b.hold (c1); break; }
                    hump (b, c0 + w * i / n, c0 + w * (i + 1) / n, shaded (c, bottom),
                          shaded (c, bottom + height, i % 2 == 0 ? 1.0f : -1.0f), c.bend);
                }
                break;
            }
            case Kind::plateau:
            {
                enterAt (b, shaded (c, bottom));
                const double edge = juce::jmax (0.5, w * c.duty);
                const float mid = 0.5f * (top + bottom);
                b.to (c0 + edge * 0.5, shaded (c, mid), c.bend);
                b.to (c0 + edge, shaded (c, top), -c.bend);
                b.hold (c1 - edge);
                b.to (c1 - edge * 0.5, shaded (c, mid), c.bend);
                b.to (c1, shaded (c, bottom), -c.bend);
                break;
            }
            case Kind::swell:
            {
                enterAt (b, shaded (c, bottom));
                // A slow exponential rise easing into a rounded top, then a fall that settles.
                const double rise = juce::jlimit (0.5, w - 0.25, w * c.duty);
                const float k = c.bend;
                b.to (c0 + rise * 0.65, shaded (c, bottom + (top - bottom) * 0.6f), k * 1.8f);
                b.to (c0 + rise, shaded (c, top), -k);
                b.to (c0 + rise + (w - rise) * 0.5, shaded (c, 0.5f * (top + bottom)), k);
                b.to (c1, shaded (c, bottom), -k);
                break;
            }
            case Kind::glide:
            {
                // Eased steps: up to the top, across to a middle level, back down.
                enterAt (b, shaded (c, bottom));
                const double edge = juce::jmax (0.5, w * 0.2);
                const float middle = c.duty >= 0.0f ? c.duty : 0.5f * (top + bottom);
                const float k = c.bend;
                auto ease = [&b, k] (double end, float y)
                {
                    const double mid = 0.5 * (b.at() + end);
                    b.to (mid, 0.5f * (b.level() + y), k);
                    b.to (end, y, -k);
                };
                ease (c0 + edge, shaded (c, top));
                b.hold (c0 + w * 0.5 - edge * 0.5);
                ease (c0 + w * 0.5 + edge * 0.5, shaded (c, middle));
                b.hold (c1 - edge);
                ease (c1, shaded (c, bottom));
                break;
            }
            case Kind::pluck:
            {
                // A quick rounded rise and a long settling fall (hung from the top: a pump).
                enterAt (b, shaded (c, bottom));
                const double attack = juce::jlimit (0.25, w * 0.4, w * 0.12);
                b.to (c0 + attack, shaded (c, top), -1.5f);
                b.to (c1, shaded (c, bottom), -std::abs (c.bend) * 1.8f);
                break;
            }
            case Kind::count: break;
        }
        if (b.at() < c1) b.hold (c1);
    }

    Shape finish (Shape s)
    {
        normalise (s);
        s = removeRedundant (s);
        // Drop points in the middle of a flat line, and a step at the very end (the loop jumps anyway).
        Shape out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (i > 0 && i + 1 < s.size())
            {
                const auto& a = out.back();
                const auto& p = s[i];
                const auto& q = s[i + 1];
                const bool flat = juce::exactlyEqual (a.y, p.y) && juce::exactlyEqual (p.y, q.y);
                const bool stepHere = juce::exactlyEqual (a.x, p.x) || juce::exactlyEqual (p.x, q.x);
                if (flat && ! stepHere) continue;
            }
            out.push_back (s[i]);
        }
        while (out.size() > 2 && juce::exactlyEqual (out[out.size() - 2].x, 1.0f) && juce::exactlyEqual (out.back().x, 1.0f))
            out.pop_back();
        if (softOnly && ! juce::exactlyEqual (out.front().y, out.back().y)) out.back().y = out.front().y;
        out.back().curve = 0.0f;
        if (out.size() > 512) out.resize (512);
        return out;
    }

    Style style;
    GridInfo grid, userGrid;
    juce::Random& random;
    std::array<float, 3> family {};
    bool softOnly = false;
    float motifAngle = 45.0f, motifAngle2 = 45.0f;
    int motifShade = 0;
    std::vector<float> palette;
    float base = 0.0f;
    Kind primary = Kind::gate, secondary = Kind::step;
};
}

// ---- The dice's other sources -------------------------------------------------------------------------------------------
juce::String sourceName (Source s)
{
    switch (s)
    {
        case Source::infiltrator:     return "INFILTRATOR";
        case Source::shaperBox:       return "SHAPERBOX";
        case Source::grossBeatTime:   return "GROSS BEAT";
        case Source::timePresets:     return "TIME PRESETS";
        case Source::grossBeatVolume: return "GROSS BEAT";
    }
    return {};
}
namespace
{
template <typename T, typename Weight>
int pickIndex (const T* items, int count, juce::Random& r, Weight&& weight)
{
    double total = 0.0;
    for (int i = 0; i < count; ++i) total += juce::jmax (0.0, (double) weight (items[i], i));
    if (total <= 0.0) return r.nextInt (juce::jmax (1, count));
    double x = r.nextDouble() * total;
    for (int i = 0; i < count; ++i)
        if ((x -= juce::jmax (0.0, (double) weight (items[i], i))) <= 0.0) return i;
    return count - 1;
}
Shape tidy (Shape s, const GridInfo& grid)
{
    // On the grid, left to right, no repeated points.
    for (auto& p : s)
    {
        p.x = juce::jlimit (0.0f, 1.0f, std::round (p.x * (float) grid.columns) / (float) grid.columns);
        p.y = juce::jlimit (0.0f, 1.0f, std::round (p.y * (float) grid.rows) / (float) grid.rows);
    }
    std::stable_sort (s.begin(), s.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
    normalise (s);
    return removeRedundant (s);
}

// A cell's make-up (worked out once per library): how much of it curves, its steps, its points.
struct CellLook { float soft = 0.0f; int steps = 0, points = 2, peaks = 0, lines = 0; bool flat = true; };
const std::vector<CellLook>& looksOf (const sources::Library& lib, int resolution)
{
    static std::map<std::pair<const sources::Library*, int>, std::vector<CellLook>> cache;
    auto& looks = cache[{ &lib, resolution }];
    if (looks.empty())
        for (int i = 0; i < lib.numCells[resolution]; ++i)
        {
            const auto& c = lib.cells[resolution][i];
            CellLook l;
            l.points = c.count;
            int slope = 0;   // the last sloped segment's direction, for counting peaks (triangles)
            for (int k = 0; k + 1 < c.count; ++k)
            {
                const auto& a = lib.points[c.first + k];
                const auto& b = lib.points[c.first + k + 1];
                if (std::abs (b.y - a.y) > 1.0e-4f) l.flat = false;
                if (b.x - a.x < 1.0e-6f && std::abs (b.y - a.y) > 1.0e-4f) ++l.steps;
                else if (std::abs (a.curve) > 1.0e-3f && std::abs (b.y - a.y) > 1.0e-4f) l.soft += b.x - a.x;
                if (b.x - a.x > 1.0e-6f && std::abs (b.y - a.y) > 1.0e-4f)
                {
                    if (std::abs (a.curve) <= 1.0e-3f) ++l.lines;   // a straight slope: a triangle's or a saw's side
                    const int dir = b.y > a.y ? 1 : -1;
                    if (slope != 0 && dir != slope) ++l.peaks;
                    slope = dir;
                }
            }
            looks.push_back (l);
        }
    return looks;
}

// SHAPERBOX and GROSS BEAT VOLUME: a repeat scheme the presets use (AAAB, ABAB ...), each letter a cell (motif) from
// them, chosen to suit Angular / Soft; Chaos: eighths more often, more different cells, cells varied. For Triad's LFOs
// (SHAPERBOX; Thomas, 0.13.0) Chaos also favours triangles and squares and busier cells, and plays some cells two or
// four times in their place: more peaks, more steps, more points.
Shape fromCells (const sources::Library& lib, const Style& style, const GridInfo& grid, juce::Random& r)
{
    const bool lfo = style.lfoChaos && &lib == &sources::shaperBox;
    const float wild = style.chaos * style.chaos;
    const int resolution = r.nextFloat() < 0.2f + 0.55f * style.chaos ? 1 : 0;
    const int n = resolution == 0 ? 4 : 8;
    const auto* schemes = lib.schemes[resolution];
    const int scheme = pickIndex (schemes, lib.numSchemes[resolution], r, [&] (const sources::Scheme& s, int)
    {
        std::set<char> letters (s.letters, s.letters + std::strlen (s.letters));
        const float variety = (float) letters.size() / (float) n;
        return std::pow (s.weight, 1.0f - 0.5f * style.chaos) * (1.0f + 3.0f * style.chaos * variety) * (1.0f + 2.0f * (1.0f - style.chaos) * (1.0f - variety));
    });
    const juce::String letters (schemes[scheme].letters);
    const auto& looks = looksOf (lib, resolution);
    std::map<juce::juce_wchar, int> cellFor;
    std::map<juce::juce_wchar, bool> flipped;
    std::map<juce::juce_wchar, int> repeatsOf;
    for (int i = 0; i < letters.length(); ++i)
    {
        const auto letter = letters[i];
        if (cellFor.count (letter)) continue;
        cellFor[letter] = pickIndex (lib.cells[resolution], lib.numCells[resolution], r, [&] (const sources::Cell& c, int k)
        {
            const auto& l = looks[(size_t) k];
            const float span = 1.0f;
            const float softness = juce::jlimit (0.0f, 1.0f, l.soft / span);
            const float fit = 0.08f + style.angular * (1.0f - softness) * (l.steps > 0 || ! l.flat ? 1.0f : 0.5f) + style.soft * softness * 1.6f;
            const float busy = (float) juce::jmax (0, l.points - 2);
            const float complexity = (1.0f + style.chaos * busy * 0.35f) / (1.0f + (1.0f - style.chaos) * busy * 0.15f);
            const float shapes = lfo ? (1.0f + wild * (3.0f * (float) juce::jmin (l.peaks, 6) + 1.2f * (float) juce::jmin (l.lines, 8)
                                                        + 0.9f * (float) juce::jmin (l.steps, 8) + 0.25f * busy))
                                         * (l.peaks + l.steps + l.lines == 0 ? 1.0f - 0.8f * wild : 1.0f)
                                     : 1.0f;
            // (With Triad's LFO Chaos the presets' favourite cells count for less, so the busier ones come up.)
            return std::pow (c.weight, lfo ? 0.6f - 0.35f * wild : 0.6f) * fit * complexity * shapes * (l.flat ? 0.5f : 1.0f);
        });
        flipped[letter] = r.nextFloat() < style.chaos * 0.3f;
        // How many times the cell plays in its place (Triad's LFOs, with Chaos).
        int times = 1;
        if (lfo && style.chaos > 0.35f && r.nextFloat() < (style.chaos - 0.35f) * 0.9f)
            times = style.chaos > 0.75f && r.nextBool() ? 4 : 2;
        while (times > 1 && grid.columns / (n * times) < 2) times /= 2;
        repeatsOf[letter] = times;
    }
    Shape s;
    for (int i = 0; i < n; ++i)
    {
        const auto letter = letters[juce::jmin (i, letters.length() - 1)];
        const auto& c = lib.cells[resolution][cellFor[letter]];
        const bool flip = flipped[letter] != (i > 0 && style.chaos > 0.6f && r.nextFloat() < (style.chaos - 0.6f) * 0.5f);
        const int times = juce::jmax (1, repeatsOf[letter]);
        for (int t = 0; t < times; ++t)
            for (int k = 0; k < c.count; ++k)
            {
                const auto& p = lib.points[c.first + k];
                const float x = ((float) i + ((float) t + p.x) / (float) times) / (float) n, y = flip ? 1.0f - p.y : p.y;
                if (! s.empty() && std::abs (s.back().x - x) < 1.0e-6f && std::abs (s.back().y - y) < 1.0e-6f) { s.back().curve = flip ? -p.curve : p.curve; continue; }
                s.push_back ({ x, y, flip ? -p.curve : p.curve });
            }
    }
    s = tidy (std::move (s), grid);
    if (s.size() < 2) return { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    return s;
}

// GROSS BEAT (TIME): runs between jumps, as long as the banks' runs; each run stays, bends the pitch (mostly an octave
// down, reversed, an octave up), or tape-stops; the jumps go back to live, replay the run, or further back (chops).
// Angular: straight lines and chops; Soft: tape stops and starts; Chaos: more runs, rarer moves and jumps.
Shape timeRuns (const Style& style, const GridInfo& grid, juce::Random& r)
{
    constexpr float beats = 4.0f;
    const float column = beats / (float) juce::jmax (1, grid.columns), row = beats / (float) juce::jmax (1, grid.rows);
    const float chaos = style.chaos;
    const auto onGrid = [] (float v, float unit) { return std::abs (v / unit - std::round (v / unit)) < 1.0e-4f; };
    for (int attempt = 0; attempt < 600; ++attempt)
    {
        const int runs = (int) sources::timeRunsPerBar[pickIndex (sources::timeRunsPerBar, (int) std::size (sources::timeRunsPerBar), r, [&] (const sources::Weighted& w, int)
        {
            return w.weight * std::pow (w.value, (chaos - 0.35f) * 2.2f);
        })].value;
        // Lengths: learned, each starting where it would in a bar of its kind, on the editor's grid.
        std::vector<float> lengths;
        float t = 0.0f;
        bool ok = true;
        for (int i = 0; i < runs && ok; ++i)
        {
            const float left = beats - t;
            if (i == runs - 1) { lengths.push_back (left); t = beats; break; }
            const int k = pickIndex (sources::timeRunBeats, (int) std::size (sources::timeRunBeats), r, [&] (const sources::Weighted& w, int)
            {
                const float d = w.value, unit = juce::jmin (d, 1.0f);
                if (d > left - 0.0625f * (float) (runs - 1 - i) || ! onGrid (t, unit) || ! onGrid (d, column)) return 0.0f;
                return std::pow (w.weight, 1.0f - 0.4f * chaos) * (d < 0.25f ? 0.5f + chaos : 1.0f);
            });
            const float d = sources::timeRunBeats[k].value;
            if (d > left - 1.0e-6f || ! onGrid (d, column)) { ok = false; break; }
            lengths.push_back (d);
            t += d;
        }
        if (! ok || lengths.empty() || std::abs (t - beats) > 1.0e-4f || ! onGrid (lengths.back(), column)) continue;
        struct Run { float t0, t1, d0, d1; int kind; };
        std::vector<Run> made;
        float at = 0.0f, d = r.nextFloat() < 0.1f + 0.2f * chaos ? row * (float) (1 + r.nextInt (4)) * 2.0f : 0.0f;
        for (size_t i = 0; i < lengths.size() && ok; ++i)
        {
            const float len = lengths[i];
            if (i > 0)
            {
                const auto& prev = made.back();
                const float further = sources::timeFurther[pickIndex (sources::timeFurther, (int) std::size (sources::timeFurther), r,
                                                                      [] (const sources::Weighted& w, int) { return w.weight; })].value;
                const auto target = [&] (int jump)
                {
                    switch (jump)
                    {
                        case sources::jumpLive:    return 0.0f;
                        case sources::jumpRepeat:  return prev.d0 + (prev.t1 - prev.t0);
                        case sources::jumpFurther: return prev.d1 + further;
                        case sources::jumpTowards: return juce::jmax (0.0f, prev.d1 - further);
                        case sources::jumpSame:    return prev.d0;
                        default:                   return prev.d1;
                    }
                };
                // Only jumps that land on the grid and within reach.
                const int jump = (int) sources::timeJumps[pickIndex (sources::timeJumps, (int) std::size (sources::timeJumps), r, [&] (const sources::Weighted& w, int)
                {
                    const int kind = (int) w.value;
                    const float to = target (kind);
                    if (! onGrid (to, row) || to > beats - 1.0e-4f) return 0.0f;
                    return w.weight * (kind == sources::jumpLive ? 1.0f - 0.5f * chaos : kind == sources::jumpFurther || kind == sources::jumpRepeat ? 0.6f + chaos + 0.5f * style.angular : 1.0f);
                })].value;
                d = target (jump);
            }
            // Only moves that end on the grid and within reach (one bar back).
            const int content = pickIndex (sources::timeContents, (int) std::size (sources::timeContents), r, [&] (const sources::TimeContent& c, int)
            {
                const float end = d + (1.0f - (c.kind == 0 ? 1.0f : c.speed)) * len;
                if (! onGrid (end, row) || end < -1.0e-4f || end > beats + 1.0e-4f) return 0.0f;
                // Staying live is the most common run in the banks; with Chaos less of it.
                const float stay = d < 1.0e-4f ? 1.0f - 0.6f * chaos : 1.0f;
                const float kindWeight = c.kind == 0 ? stay : c.kind == 1 ? 0.3f + 1.2f * style.angular : 0.08f + 2.0f * style.soft;
                // What Triad Time is best at (Thomas): an octave or two down or up, forwards or reversed.
                const bool octave = std::abs (c.speed - 0.5f) < 1.0e-3f || std::abs (c.speed - 0.25f) < 1.0e-3f || std::abs (c.speed - 2.0f) < 1.0e-3f
                                    || std::abs (c.speed - 4.0f) < 1.0e-3f || std::abs (c.speed + 1.0f) < 1.0e-3f || std::abs (c.speed + 0.5f) < 1.0e-3f
                                    || std::abs (c.speed + 2.0f) < 1.0e-3f;
                const float pitchBias = c.kind == 0 ? 1.0f : octave ? 1.6f : 0.5f;
                // Long runs mostly play on (or slow to half speed); the bending happens in the short ones.
                const float longRun = len > 1.0f + 1.0e-4f && c.kind != 0 && std::abs (c.speed - 0.5f) > 1.0e-3f ? 0.25f : 1.0f;
                return std::pow (c.weight, 1.0f - 0.45f * chaos) * kindWeight * pitchBias * longRun;
            });
            const auto& c = sources::timeContents[content];
            const float speed = c.kind == 0 ? 1.0f : c.speed;
            const float d1 = d + (1.0f - speed) * len;
            if (! onGrid (d, row) || ! onGrid (d1, row) || d < -1.0e-4f || d1 < -1.0e-4f || d > beats + 1.0e-4f || d1 > beats + 1.0e-4f) { ok = false; break; }
            made.push_back ({ at, at + len, d, d1, c.kind });
            at += len;
        }
        if (! ok) continue;
        // Something to hear (a bent line, a chop, a tape stop), and with Chaos more of it: runs that are not just live.
        bool bends = false;
        int active = 0;
        for (size_t i = 0; i < made.size(); ++i)
        {
            const bool chop = i > 0 && made[i].d0 > made[i - 1].d1 + 1.0e-4f;
            bends |= (made[i].kind != 0 && std::abs (made[i].d1 - made[i].d0) > 1.0e-4f) || chop;
            active += made[i].kind != 0 || made[i].d0 > 1.0e-4f || chop ? 1 : 0;
        }
        if (! bends || made.size() < 2 || active < juce::jmin ((int) made.size(), 2 + juce::roundToInt (chaos * 3.0f))) continue;
        Shape s;
        const float tape = 2.0f * std::log (1.0f / 0.25f - 1.0f);
        for (const auto& run : made)
        {
            const Point a { run.t0 / beats, 1.0f - run.d0 / beats, run.kind == 2 ? (r.nextFloat() < 0.7f ? tape : -tape) : 0.0f };
            const Point b { run.t1 / beats, 1.0f - run.d1 / beats, 0.0f };
            if (! s.empty() && std::abs (s.back().x - a.x) < 1.0e-6f && std::abs (s.back().y - a.y) < 1.0e-6f) s.back().curve = a.curve;
            else s.push_back (a);
            s.push_back (b);
        }
        return tidy (std::move (s), grid);
    }
    return timePresets::shape (r.nextInt (timePresets::numPresets));
}

// The value just before / just after x (at a vertical step these differ).
float valueBefore (const Shape& s, float x)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (std::abs (s[i].x - x) < 1.0e-6f) return s[i].y;
    return evaluate (s, x);
}
float valueAfter (const Shape& s, float x)
{
    for (size_t i = s.size(); i-- > 0;)
        if (std::abs (s[i].x - x) < 1.0e-6f) return s[i].y;
    return evaluate (s, x);
}
// `base` with its part from x0 to x1 replaced by the same part of `donor` (vertical steps where they meet).
Shape splice (const Shape& base, const Shape& donor, float x0, float x1)
{
    Shape out;
    for (const auto& p : base) if (p.x < x0 - 1.0e-6f) out.push_back (p);
    if (out.empty() || out.back().x < x0 - 1.0e-6f) out.push_back ({ x0, valueBefore (base, x0), 0.0f });
    if (! out.empty() && std::abs (out.back().x - x0) < 1.0e-6f) out.back().curve = 0.0f;
    float curveAt = 0.0f;
    for (size_t i = 0; i + 1 < donor.size(); ++i)
        if (donor[i].x <= x0 + 1.0e-6f && donor[i + 1].x > x0 + 1.0e-6f) curveAt = donor[i].curve;
    out.push_back ({ x0, valueAfter (donor, x0), curveAt });
    for (const auto& p : donor) if (p.x > x0 + 1.0e-6f && p.x < x1 - 1.0e-6f) out.push_back (p);
    out.push_back ({ x1, valueBefore (donor, x1), 0.0f });
    float curveBase = 0.0f;
    for (size_t i = 0; i + 1 < base.size(); ++i)
        if (base[i].x <= x1 + 1.0e-6f && base[i + 1].x > x1 + 1.0e-6f) curveBase = base[i].curve;
    out.push_back ({ x1, valueAfter (base, x1), curveBase });
    for (const auto& p : base) if (p.x > x1 + 1.0e-6f) out.push_back (p);
    normalise (out);
    return removeRedundant (out);
}
// TIME PRESETS: one of the 1,000 TIME patterns (curved ones more with Soft, straight ones with Angular); Chaos splices in
// quarters (eighths too, with more chaos) of other patterns, so it moves further from the ones there are.
Shape fromTimePresets (const Style& style, const GridInfo& grid, juce::Random& r)
{
    static const std::vector<bool> curved = []
    {
        std::vector<bool> v;
        for (int i = 0; i < timePresets::numPresets; ++i)
        {
            bool c = false;
            for (const auto& p : timePresets::shape (i)) c |= std::abs (p.curve) > 1.0e-3f;
            v.push_back (c);
        }
        return v;
    }();
    const auto choose = [&]
    {
        std::vector<int> all ((size_t) timePresets::numPresets);
        for (int i = 0; i < timePresets::numPresets; ++i) all[(size_t) i] = i;
        return pickIndex (all.data(), (int) all.size(), r, [&] (int, int i)
        {
            return curved[(size_t) i] ? 0.15f + 1.8f * style.soft : 0.15f + style.angular;
        });
    };
    auto s = timePresets::shape (choose());
    const int splices = juce::roundToInt (style.chaos * 4.0f + r.nextFloat() * style.chaos);
    for (int k = 0; k < splices; ++k)
    {
        const bool eighth = r.nextFloat() < style.chaos * 0.6f;
        const int parts = eighth ? 8 : 4;
        const int at = r.nextInt (parts);
        s = splice (s, timePresets::shape (choose()), (float) at / (float) parts, (float) (at + 1) / (float) parts);
    }
    return tidy (std::move (s), grid);
}
}

Shape generate (const Style& style, const GridInfo& grid, juce::Random& random)
{
    switch (style.source)
    {
        case Source::shaperBox:
        {
            auto shape = fromCells (sources::shaperBox, style, grid, random);
            // Triad's LFOs: never a flat line (a still LFO) from the dice.
            for (int retry = 0; style.lfoChaos && retry < 8 && std::all_of (shape.begin(), shape.end(), [&] (const Point& p) { return juce::exactlyEqual (p.y, shape.front().y); }); ++retry)
                shape = fromCells (sources::shaperBox, style, grid, random);
            return shape;
        }
        case Source::grossBeatVolume: return fromCells (sources::grossBeatVolume, style, grid, random);
        case Source::grossBeatTime:   return timeRuns (style, grid, random);
        case Source::timePresets:     return fromTimePresets (style, grid, random);
        case Source::infiltrator:     break;
    }
    return Generator (style, grid, random).run();
}

// ---- Taste: the green check and the red X ---------------------------------------------------------------------------
Taste::Features Taste::featuresOf (const Shape& s)
{
    Features f;
    for (size_t i = 0; i < f.curve.size(); ++i) f.curve[i] = evaluate (s, ((float) i + 0.5f) / (float) f.curve.size());
    float steps = 0.0f, curves = 0.0f, lines = 0.0f, motion = 0.0f;
    for (size_t i = 0; i + 1 < s.size(); ++i)
    {
        const auto& a = s[i];
        const auto& b = s[i + 1];
        motion += std::abs (b.y - a.y);
        if (juce::exactlyEqual (a.x, b.x)) steps += 1.0f;
        else if (std::abs (a.curve) > 0.05f) curves += 1.0f;
        else if (! juce::exactlyEqual (a.y, b.y)) lines += 1.0f;
    }
    const float segments = (float) juce::jmax ((size_t) 1, s.size() - 1);
    f.makeUp = { steps / segments, curves / segments, lines / segments, juce::jmin (1.0f, (float) s.size() / 40.0f), juce::jmin (1.0f, motion / 12.0f) };
    return f;
}
float Taste::similarity (const Features& a, const Style& sa, const Features& b, const Style& sb)
{
    float curve = 0.0f;
    for (size_t i = 0; i < a.curve.size(); ++i) curve += std::abs (a.curve[i] - b.curve[i]);
    curve /= (float) a.curve.size();
    float makeUp = 0.0f;
    for (size_t i = 0; i < a.makeUp.size(); ++i) makeUp += std::abs (a.makeUp[i] - b.makeUp[i]);
    makeUp /= (float) a.makeUp.size();
    const float style = (std::abs (sa.angular - sb.angular) + std::abs (sa.soft - sb.soft) + std::abs (sa.chaos - sb.chaos)) / 3.0f;
    const float distance = 0.5f * curve + 0.3f * makeUp + 0.2f * style;
    return std::exp (-distance / 0.12f);
}
float Taste::similarity (const Shape& a, const Style& sa, const Shape& b, const Style& sb)
{
    return similarity (featuresOf (a), sa, featuresOf (b), sb);
}
void Taste::add (const Shape& s, const Style& style, bool liked)
{
    if (s.size() < 2) return;
    // Rating the same shape again changes its rating.
    entries.erase (std::remove_if (entries.begin(), entries.end(), [&s] (const Entry& e) { return e.shape == s; }), entries.end());
    entries.push_back ({ s, style, liked, featuresOf (s) });
    if ((int) entries.size() > maxEntries) entries.erase (entries.begin(), entries.begin() + ((int) entries.size() - maxEntries));
}
int Taste::countLiked() const { return (int) std::count_if (entries.begin(), entries.end(), [] (const Entry& e) { return e.liked; }); }
int Taste::countDisliked() const { return (int) entries.size() - countLiked(); }
float Taste::score (const Shape& s, const Style& style) const
{
    if (entries.empty()) return 0.0f;
    const auto f = featuresOf (s);
    // The closest liked and disliked shapes count most; the rest a little (newer ratings a little more).
    float bestLiked = 0.0f, bestDisliked = 0.0f, sumLiked = 0.0f, sumDisliked = 0.0f, weightLiked = 0.0f, weightDisliked = 0.0f;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        const float sim = similarity (f, style, e.features, e.style);
        const float recency = 0.5f + 0.5f * (float) (i + 1) / (float) entries.size();
        if (e.liked) { bestLiked = juce::jmax (bestLiked, sim); sumLiked += sim * recency; weightLiked += recency; }
        else { bestDisliked = juce::jmax (bestDisliked, sim); sumDisliked += sim * recency; weightDisliked += recency; }
    }
    const float liked = weightLiked > 0.0f ? 0.7f * bestLiked + 0.3f * sumLiked / weightLiked : 0.0f;
    const float disliked = weightDisliked > 0.0f ? 0.7f * bestDisliked + 0.3f * sumDisliked / weightDisliked : 0.0f;
    // A near copy of a liked shape is no new idea: a little less.
    const float copy = bestLiked > 0.97f ? 0.3f : 0.0f;
    return liked - 1.3f * disliked - copy;
}
bool Taste::rejects (const Shape& s, const Style& style) const
{
    const auto f = featuresOf (s);
    for (const auto& e : entries)
        if (! e.liked && similarity (f, style, e.features, e.style) > 0.8f) return true;
    return false;
}
Shape Taste::pick (const Style& style, const GridInfo& grid, juce::Random& random, const std::function<bool (const Shape&)>& allowed, int candidates) const
{
    auto ok = [&] (const Shape& s) { return ! allowed || allowed (s); };
    if (entries.empty())
    {
        auto s = generate (style, grid, random);
        for (int tries = 0; tries < 8 && ! ok (s); ++tries) s = generate (style, grid, random);
        return s;
    }
    std::vector<std::pair<float, Shape>> scored;
    for (int i = 0; i < juce::jmax (1, candidates); ++i)
    {
        auto s = generate (style, grid, random);
        if (! ok (s) || rejects (s, style)) continue;
        scored.push_back ({ score (s, style), std::move (s) });
    }
    if (scored.empty())   // everything looked like something disliked: the least disliked of a fresh batch
    {
        for (int i = 0; i < juce::jmax (1, candidates); ++i)
        {
            auto s = generate (style, grid, random);
            if (ok (s)) scored.push_back ({ score (s, style), std::move (s) });
        }
        if (scored.empty()) return generate (style, grid, random);
    }
    // One of the best: chances fall off quickly with the score, so better shapes come up far more often.
    float best = -1.0e9f;
    for (const auto& c : scored) best = juce::jmax (best, c.first);
    double total = 0.0;
    std::vector<double> weights;
    for (const auto& c : scored) { weights.push_back (std::exp ((c.first - best) / 0.06)); total += weights.back(); }
    double r = random.nextDouble() * total;
    for (size_t i = 0; i < scored.size(); ++i)
        if ((r -= weights[i]) <= 0.0) return scored[i].second;
    return scored.back().second;
}
juce::String Taste::toText() const
{
    juce::StringArray lines;
    for (const auto& e : entries)
        lines.add (juce::String (e.liked ? "like" : "dislike") + "|" + juce::String (e.style.angular, 3) + "," + juce::String (e.style.soft, 3) + ","
                   + juce::String (e.style.chaos, 3) + "|" + mod::toText (e.shape));
    return lines.joinIntoString ("\n");
}
Taste Taste::fromText (const juce::String& text)
{
    Taste t;
    for (const auto& line : juce::StringArray::fromLines (text))
    {
        const auto parts = juce::StringArray::fromTokens (line, "|", "");
        if (parts.size() != 3 || (parts[0] != "like" && parts[0] != "dislike")) continue;
        const auto values = juce::StringArray::fromTokens (parts[1], ",", "");
        if (values.size() != 3) continue;
        const Style style { juce::jlimit (0.0f, 1.0f, values[0].getFloatValue()), juce::jlimit (0.0f, 1.0f, values[1].getFloatValue()),
                            juce::jlimit (0.0f, 1.0f, values[2].getFloatValue()) };
        const auto shape = mod::fromText (parts[2]);
        if (shape.size() >= 2) t.add (shape, style, parts[0] == "like");
    }
    return t;
}

Shape generate (const Style& style, const GridInfo& grid, juce::int64 seed)
{
    juce::Random random (seed);
    return generate (style, grid, random);
}

float lineAngle (const Point& from, const Point& to, float aspect)
{
    const float dx = std::abs (to.x - from.x) * aspect, dy = std::abs (to.y - from.y);
    if (dx <= 1.0e-7f) return dy > 0.0f ? 90.0f : 0.0f;
    return std::atan (dy / dx) / toRadians;
}
}
