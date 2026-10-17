#pragma once
#include "../Source/Generator.h"
#include <array>
#include <cmath>
#include <iostream>
#include <set>

// The LFO shape generator and the preset bank.
namespace generatorTests
{
namespace mod = triad::mod;

inline bool validShape (const mod::Shape& s)
{
    if (s.size() < 2 || s.size() > 512) return false;
    if (! juce::exactlyEqual (s.front().x, 0.0f) || ! juce::exactlyEqual (s.back().x, 1.0f)) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto& p = s[i];
        if (! std::isfinite (p.x) || ! std::isfinite (p.y) || ! std::isfinite (p.curve)) return false;
        if (p.y < 0.0f || p.y > 1.0f || std::abs (p.curve) > mod::maxCurve) return false;
        if (i > 0 && p.x < s[i - 1].x) return false;
        if (i > 1 && juce::exactlyEqual (p.x, s[i - 1].x) && juce::exactlyEqual (p.x, s[i - 2].x)) return false;   // at most a step
    }
    return true;
}

struct Look
{
    int steps = 0, holds = 0, lines = 0, curves = 0;
    std::set<int> angles;    // classes (30, 45, 60, 75) of the straight lines; 0 = none of them
    float low = 1.0f, high = 0.0f;
};
inline Look look (const mod::Shape& s, float aspect = mod::plotAspect)
{
    Look l;
    for (size_t i = 0; i < s.size(); ++i)
    {
        l.low = juce::jmin (l.low, s[i].y);
        l.high = juce::jmax (l.high, s[i].y);
        if (i == 0) continue;
        const auto& a = s[i - 1];
        const auto& b = s[i];
        if (juce::exactlyEqual (a.x, b.x)) { if (! juce::exactlyEqual (a.y, b.y)) ++l.steps; continue; }
        if (juce::exactlyEqual (a.y, b.y)) { ++l.holds; continue; }
        if (std::abs (a.curve) > 1.0e-3f) { ++l.curves; continue; }
        ++l.lines;
        const float angle = mod::lineAngle (a, b, aspect);
        int cls = 0;
        for (int c : { 30, 45, 60, 75 })
            if (std::abs (angle - (float) c) <= 6.0f) cls = c;
        l.angles.insert (cls);
    }
    return l;
}

inline float halfDifference (const mod::Shape& s, float period)
{
    float most = 0.0f;
    for (int i = 0; i < 256; ++i)
    {
        const float x = (float) i / 256.0f;
        most = juce::jmax (most, std::abs (mod::evaluate (s, x) - mod::evaluate (s, std::fmod (x + period, 1.0f))));
    }
    return most;
}

inline void run()
{
    // Every style, on every grid, makes a valid shape that moves.
    {
        juce::Random r (2024);
        const std::array<int, 8> grids { 8, 12, 16, 24, 32, 48, 64, 128 };
        for (int i = 0; i < 3000; ++i)
        {
            mod::Style style { r.nextFloat(), r.nextFloat(), r.nextFloat() };
            if (i % 7 == 0) style.angular = style.soft = 0.0f;
            if (i % 11 == 0) style.chaos = (float) (i % 2);
            mod::GridInfo grid;
            grid.columns = grids[(size_t) r.nextInt ((int) grids.size())];
            grid.aspect = 1.0f + 2.0f * r.nextFloat();
            const auto s = mod::generate (style, grid, (juce::int64) i);
            require (validShape (s), "Generated shapes are valid LFO shapes");
            const auto l = look (s, grid.aspect);
            require (l.high - l.low >= 0.45f - 1.0e-4f, "Generated shapes span at least 45% of the height (never flat)");
        }
    }
    // Same seed, same shape; the dice gives new shapes each time.
    {
        const mod::Style style;
        const mod::GridInfo grid;
        require (mod::generate (style, grid, (juce::int64) 77) == mod::generate (style, grid, (juce::int64) 77), "The same seed gives the same shape");
        juce::Random r (5);
        std::set<juce::String> distinct;
        for (int i = 0; i < 100; ++i) distinct.insert (mod::toText (mod::generate (style, grid, r)));
        require (distinct.size() >= 80, "The generator rarely repeats itself");
    }
    // Angular only: hard corners. Mostly vertical steps and holds; straight lines at 30/45/60/75 degrees; points on the grid.
    {
        juce::Random r (11);
        const mod::GridInfo grid;   // 16 columns, Triad's aspect
        int withSteps = 0, squaresOnly = 0, with30or45 = 0, lines = 0, classed = 0, points = 0, onGrid = 0;
        int steps = 0, holds = 0, all = 0;
        for (int i = 0; i < 600; ++i)
        {
            const auto s = mod::generate ({ 1.0f, 0.0f, 0.35f }, grid, r);
            const auto l = look (s);
            require (l.curves == 0, "Angular 100% / Soft 0%: no curves at all");
            withSteps += l.steps > 0;
            squaresOnly += l.lines == 0;
            with30or45 += l.angles.count (30) + l.angles.count (45) > 0;
            steps += l.steps; holds += l.holds; all += l.steps + l.holds + l.lines;
            for (size_t k = 0; k + 1 < s.size(); ++k)
                if (! juce::exactlyEqual (s[k].x, s[k + 1].x) && ! juce::exactlyEqual (s[k].y, s[k + 1].y))
                {
                    ++lines;
                    const float angle = mod::lineAngle (s[k], s[k + 1]);
                    for (int c : { 30, 45, 60, 75 }) if (std::abs (angle - (float) c) <= 6.0f) { ++classed; break; }
                }
            for (const auto& p : s) { ++points; onGrid += std::abs (p.x * 16.0f - std::round (p.x * 16.0f)) < 1.0e-4f; }
        }
        std::cout << "Generator, Angular 100%: " << withSteps * 100 / 600 << "% of shapes have 90-degree steps, "
                  << squaresOnly * 100 / 600 << "% only squares/steps, " << with30or45 * 100 / 600 << "% have 30 or 45-degree lines; "
                  << classed * 100 / juce::jmax (1, lines) << "% of lines at 30/45/60/75 degrees; steps+holds "
                  << (steps + holds) * 100 / juce::jmax (1, all) << "% of segments" << std::endl;
        require (withSteps >= 480, "Angular: most shapes have hard 90-degree steps");
        require (squaresOnly >= 180, "Angular: many shapes are pure squares and steps");
        require (with30or45 >= 90, "Angular: many shapes have 30 or 45-degree lines");
        require (classed >= lines * 85 / 100, "Angular: straight lines run at 30, 45, 60 or 75 degrees on screen");
        require ((steps + holds) * 100 >= all * 70, "Angular: mostly 90-degree corners (steps and holds), like the favourites");
        require (onGrid >= points * 95 / 100, "Angular: points sit on the grid");
    }
    // Soft only: curves, no hard corners, and it loops without a jump.
    {
        juce::Random r (12);
        for (int i = 0; i < 400; ++i)
        {
            const auto s = mod::generate ({ 0.0f, 1.0f, r.nextFloat() }, mod::GridInfo(), r);
            const auto l = look (s);
            require (l.steps == 0 && l.lines == 0 && l.curves > 0, "Soft 100% / Angular 0%: only curves (no steps, no straight lines)");
            require (juce::exactlyEqual (s.front().y, s.back().y), "Soft shapes loop without a jump");
        }
    }
    // Both: hard jumps into curves are common.
    {
        juce::Random r (13);
        int both = 0;
        for (int i = 0; i < 400; ++i)
        {
            const auto l = look (mod::generate ({ 1.0f, 0.6f, 0.35f }, mod::GridInfo(), r));
            both += l.steps > 0 && l.curves > 0;
        }
        std::cout << "Generator, Angular 100% + Soft 60%: " << both * 100 / 400 << "% of shapes mix hard jumps and curves" << std::endl;
        require (both >= 100, "Angular + Soft: shapes mix hard jumps and curves");
    }
    // Chaos: more points; low chaos repeats a pattern, high chaos mostly does not.
    {
        auto stats = [] (float chaos, double& points, int& repeating)
        {
            juce::Random r (14);
            points = 0.0; repeating = 0;
            for (int i = 0; i < 400; ++i)
            {
                const auto s = mod::generate ({ 1.0f, 0.0f, chaos }, mod::GridInfo(), r);
                points += (double) s.size() / 400.0;
                repeating += halfDifference (s, 0.5f) < 0.01f || halfDifference (s, 0.25f) < 0.01f;
            }
        };
        double p0, p5, p1; int r0, r5, r1;
        stats (0.0f, p0, r0); stats (0.5f, p5, r5); stats (1.0f, p1, r1);
        std::cout << "Generator, chaos 0 / 50 / 100%: " << juce::roundToInt (p0) << " / " << juce::roundToInt (p5) << " / " << juce::roundToInt (p1)
                  << " points on average; repeating halves or quarters " << r0 * 100 / 400 << "% / " << r5 * 100 / 400 << "% / " << r1 * 100 / 400 << "%" << std::endl;
        require (p0 < p5 && p5 < p1, "More chaos, more points");
        require (r0 >= 160 && r0 > r1, "Low chaos repeats a simple pattern (not too random)");
    }
    // Triplet grid: points on 1/12 columns.
    {
        juce::Random r (15);
        mod::GridInfo grid;
        grid.columns = 12;
        int points = 0, onGrid = 0;
        for (int i = 0; i < 200; ++i)
            for (const auto& p : mod::generate ({ 1.0f, 0.0f, 0.5f }, grid, r))
            {
                ++points;
                onGrid += std::abs (p.x * 12.0f - std::round (p.x * 12.0f)) < 1.0e-4f;
            }
        require (onGrid >= points * 95 / 100, "The generator follows a triplet grid");
    }
    std::cout << "LFO generator: passed" << std::endl;

    // ---- Presets -------------------------------------------------------------------------------
    {
        require (mod::numPresets() >= 70 + 5 * 33, "75 hand-made preset shapes and the generated folders");
        const char* first[] { "Flat", "Ramp down", "Ramp up", "Triangle", "Sine", "Square", "Pump", "Pump 1/4", "Saw 1/4", "Gate 1/8" };
        for (int i = 0; i < 10; ++i) require (mod::presetName (i) == first[i], "The first ten presets keep their numbers");
        std::set<juce::String> names;
        int count = 0;
        for (int c = 0; c < mod::numPresetCategories(); ++c)
        {
            const auto list = mod::presetsIn (c);
            if (mod::isGeneratedCategory (c)) require (list.size() >= 33, "Every generated folder has at least three pages of shapes");
            else require (! list.empty() && list.size() <= 11, "Every hand-made preset category has 1 to 11 shapes (one palette row)");
            count += (int) list.size();
        }
        require (count == mod::numPresets(), "Every preset is in a category");
        std::set<int> angleClasses;
        int tripletPoints = 0, tripletOnGrid = 0;
        for (int i = 0; i < mod::numPresets(); ++i)
        {
            const auto s = mod::preset (i);
            require (validShape (s), "Preset shapes are valid");
            names.insert (mod::presetName (i));
            const auto category = mod::presetCategoryName (mod::presetCategory (i));
            const auto l = look (s);
            if (category == "SQUARES" || category == "STEPS")
                require (l.lines == 0 && l.curves == 0 && l.steps > 0, "Squares and steps: only 90-degree corners");
            if (category == "TRIANGLES" || category == "ANGLES")
            {
                require (l.curves == 0 && l.lines > 0, "Triangles and angles: straight lines, no curves");
                for (int a : l.angles) angleClasses.insert (a);
            }
            if (category == "HARD + CURVE") require (l.steps > 0 && l.curves > 0, "Hard + curve: a hard jump and a curve");
            if (category == "SOFT" || category == "GEN: SOFT") require (l.steps == 0 && l.curves > 0, "Soft presets: curves, no hard jumps");
            if (category == "GEN: SQUARES") require (l.lines == 0 && l.curves == 0 && l.steps > 0, "Generated squares: only 90-degree corners");
            if (category == "GEN: ANGLES") require (l.curves == 0 && l.lines >= 2, "Generated angles: straight lines, no curves");
            if (category == "GEN: HARD+CURVE") require (l.steps > 0 && l.curves > 0, "Generated hard + curve: a hard jump and a curve");
            if (category == "GEN: TRIPLETS")
                for (const auto& p : s)
                {
                    ++tripletPoints;
                    tripletOnGrid += std::abs (p.x * 12.0f - std::round (p.x * 12.0f)) < 1.0e-3f;
                    // (Curves through a cell put their bend points at quarters of a triplet column.)
                    require (std::abs (p.x * 48.0f - std::round (p.x * 48.0f)) < 1.0e-3f, "Generated triplets: every point on a triplet column or a quarter of one");
                }
            if (mod::isGeneratedCategory (mod::presetCategory (i))) require (l.high - l.low >= 0.45f - 1.0e-4f, "Generated presets move (never flat)");
        }
        require (names.size() == (size_t) mod::numPresets(), "Preset names are unique");
        require (tripletPoints > 0 && tripletOnGrid >= tripletPoints * 85 / 100, "Generated triplets sit on the triplet grid");
        require (angleClasses.count (30) && angleClasses.count (45) && angleClasses.count (60), "Presets have lines at 30, 45 and 60 degrees");
        std::cout << "LFO presets: " << mod::numPresets() << " shapes in " << mod::numPresetCategories() << " categories: passed" << std::endl;
    }
}
}
