#pragma once
#include "../Source/Generator.h"
#include "../Source/TimePresets.h"
#include <algorithm>
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
        int count = 0;
        for (int c = 0; c < mod::numPresetCategories(); ++c)
        {
            const auto list = mod::presetsIn (c);
            if (mod::isGeneratedCategory (c)) require (list.size() >= 33, "Every generated folder has at least three pages of shapes");
            else if (mod::isExtraCategory (c)) require (list.size() >= 300, "EXTRA has every preview shape (300 or more)");
            else require (mod::numHandMadeShapes (c) >= 1 && mod::numHandMadeShapes (c) <= 11, "Every hand-made folder has 1 to 11 hand-made shapes (its first page)");
            std::set<juce::String> names;
            for (int i : list) names.insert (mod::presetName (i));
            require (names.size() == list.size(), "Shape names are unique within a folder");
            // The hand-made shapes come first, then the EXTRA shapes that fit the folder.
            for (size_t k = 0; k < list.size(); ++k)
                if (! mod::isGeneratedCategory (c) && ! mod::isExtraCategory (c))
                    require (mod::presetName (list[k]).startsWith ("Extra ") == ((int) k >= mod::numHandMadeShapes (c)), "Hand-made shapes first in their folder");
            count += (int) list.size();
        }
        require (count == mod::numPresets(), "Every preset is in a category");
        std::set<int> angleClasses;
        int tripletPoints = 0, tripletOnGrid = 0;
        for (int i = 0; i < mod::numPresets(); ++i)
        {
            const auto s = mod::preset (i);
            require (validShape (s), "Preset shapes are valid");
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
        // Every EXTRA shape is also in exactly one of the folders by type.
        {
            int extras = 0, filed = 0;
            for (int i = 0; i < mod::numPresets(); ++i)
            {
                if (mod::isExtraCategory (mod::presetCategory (i))) ++extras;
                else if (mod::presetName (i).startsWith ("Extra ")) ++filed;
            }
            require (extras >= 300 && filed == extras, "Every EXTRA shape is also filed under its type (squares, steps, triangles, angles, hard + curve, soft)");
        }
        require (tripletPoints > 0 && tripletOnGrid >= tripletPoints * 85 / 100, "Generated triplets sit on the triplet grid");
        require (angleClasses.count (30) && angleClasses.count (45) && angleClasses.count (60), "Presets have lines at 30, 45 and 60 degrees");
        std::cout << "LFO presets: " << mod::numPresets() << " shapes in " << mod::numPresetCategories() << " categories: passed" << std::endl;
    }
    // ---- Training the dice: the green check and the red X ------------------------------------------------------------
    {
        const mod::Style mix { 1.0f, 0.6f, 0.4f };   // hard angles into curves: both kinds of shape come up
        mod::GridInfo grid;
        auto curvy = [] (const mod::Shape& s)
        {
            const auto l = look (s);
            const int segments = l.steps + l.holds + l.lines + l.curves;
            return segments > 0 ? (float) l.curves / (float) segments : 0.0f;
        };
        // Untrained: how curvy the dice's shapes are in this style.
        mod::Taste none;
        juce::Random r1 (11);
        float before = 0.0f;
        for (int i = 0; i < 60; ++i) before += curvy (none.pick (mix, grid, r1));
        before /= 60.0f;
        // Thomas likes the shapes with few curves and dislikes the curvy ones (40 ratings).
        mod::Taste taste;
        juce::Random r2 (12);
        for (int i = 0; i < 40; ++i)
        {
            const auto s = mod::generate (mix, grid, r2);
            taste.add (s, mix, curvy (s) < 0.15f);
        }
        juce::Random r3 (13);
        float after = 0.0f;
        for (int i = 0; i < 60; ++i) after += curvy (taste.pick (mix, grid, r3));
        after /= 60.0f;
        std::cout << "Generator taste: curves in its shapes " << juce::roundToInt (before * 100.0f) << "% untrained, "
                  << juce::roundToInt (after * 100.0f) << "% after liking straight ones and disliking curvy ones" << std::endl;
        require (after < before * 0.75f, "Liked and disliked shapes train the dice (fewer of what you disliked)");
        // A disliked shape (and anything nearly the same) never comes back.
        juce::Random r4 (14);
        const auto disliked = mod::generate (mix, grid, r4);
        mod::Taste one;
        one.add (disliked, mix, false);
        require (one.rejects (disliked, mix) && one.countDisliked() == 1, "A disliked shape is never offered again");
        require (mod::Taste::similarity (disliked, mix, disliked, mix) > 0.999f, "A shape is the same as itself");
        require (mod::Taste::similarity (disliked, mix, disliked, mod::Style { 0.0f, 1.0f, 1.0f }) < 0.95f,
                 "...and less alike made with other Angular / Soft / Chaos settings");
        // Kept between sessions.
        const auto restored = mod::Taste::fromText (taste.toText());
        require (restored.countLiked() == taste.countLiked() && restored.countDisliked() == taste.countDisliked(), "Ratings are saved and read back");
        mod::Taste many;
        juce::Random r5 (15);
        for (int i = 0; i < mod::Taste::maxEntries + 50; ++i) many.add (mod::generate (mix, grid, r5), mix, i % 2 == 0);
        require (many.countLiked() + many.countDisliked() <= mod::Taste::maxEntries, "Only the newest ratings are kept");
        std::cout << "Generator taste: passed" << std::endl;
    }
    // ---- The dice's sources: each set of presets Thomas sent, kept apart (0.13.0) --------------------------------------------
    {
        const std::array<mod::Source, 5> all { mod::Source::infiltrator, mod::Source::shaperBox, mod::Source::grossBeatTime, mod::Source::timePresets,
                                               mod::Source::grossBeatVolume };
        juce::Random r (99);
        int invalid = 0;
        for (const auto source : all)
            for (int i = 0; i < 400; ++i)
            {
                mod::GridInfo grid;
                grid.columns = source == mod::Source::grossBeatTime || source == mod::Source::timePresets ? 64 : (i % 2 == 0 ? 16 : 32);
                grid.aspect = source == mod::Source::grossBeatTime || source == mod::Source::timePresets ? 3.48f : mod::plotAspect;
                const mod::Style style { r.nextFloat(), r.nextFloat(), r.nextFloat(), source };
                if (! validShape (mod::generate (style, grid, r))) ++invalid;
            }
        require (invalid == 0, "Every source makes valid shapes in every style");
        for (const auto source : all)
        {
            const mod::Style style { 0.7f, 0.3f, 0.5f, source };
            require (mod::generate (style, mod::GridInfo(), (juce::int64) 4242) == mod::generate (style, mod::GridInfo(), (juce::int64) 4242), "...the same seed, the same shape");
        }
    }
    {
        // SHAPERBOX: repeats cells the way the ShaperBox presets do (less with Chaos), curves with Soft, and is not the Infiltrator dice.
        const auto repeats = [] (const mod::Shape& s)
        {
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                {
                    float diff = 0.0f;
                    for (int k = 0; k < 32; ++k)
                    {
                        const float t = ((float) k + 0.5f) / 32.0f * 0.25f;
                        diff += std::abs (mod::evaluate (s, (float) a * 0.25f + t) - mod::evaluate (s, (float) b * 0.25f + t));
                    }
                    if (diff / 32.0f < 0.02f) return true;
                }
            return false;
        };
        juce::Random r (7);
        int calmRepeats = 0, wildRepeats = 0, softCurves = 0, hardCurves = 0, softSegments = 0, hardSegments = 0;
        std::set<juce::String> sb, inf;
        for (int i = 0; i < 400; ++i)
        {
            calmRepeats += repeats (mod::generate ({ 0.7f, 0.3f, 0.0f, mod::Source::shaperBox }, mod::GridInfo(), r)) ? 1 : 0;
            wildRepeats += repeats (mod::generate ({ 0.7f, 0.3f, 1.0f, mod::Source::shaperBox }, mod::GridInfo(), r)) ? 1 : 0;
            const auto soft = look (mod::generate ({ 0.0f, 1.0f, 0.4f, mod::Source::shaperBox }, mod::GridInfo(), r));
            const auto hard = look (mod::generate ({ 1.0f, 0.0f, 0.4f, mod::Source::shaperBox }, mod::GridInfo(), r));
            softCurves += soft.curves; softSegments += soft.curves + soft.lines + soft.holds + soft.steps;
            hardCurves += hard.curves; hardSegments += hard.curves + hard.lines + hard.holds + hard.steps;
            sb.insert (mod::toText (mod::generate ({ 1.0f, 0.3f, 0.35f, mod::Source::shaperBox }, mod::GridInfo(), (juce::int64) i)));
            inf.insert (mod::toText (mod::generate ({ 1.0f, 0.3f, 0.35f, mod::Source::infiltrator }, mod::GridInfo(), (juce::int64) i)));
        }
        int shared = 0;
        for (const auto& t : sb) shared += inf.count (t) > 0 ? 1 : 0;
        const float softShare = (float) softCurves / (float) juce::jmax (1, softSegments), hardShare = (float) hardCurves / (float) juce::jmax (1, hardSegments);
        std::cout << "SHAPERBOX dice: a repeated quarter in " << calmRepeats / 4 << "% at Chaos 0, " << wildRepeats / 4 << "% at Chaos 100; curved segments "
                  << juce::roundToInt (softShare * 100.0f) << "% at Soft 100, " << juce::roundToInt (hardShare * 100.0f) << "% at Angular 100; "
                  << sb.size() << " different of 400, " << shared << " also made by the INFILTRATOR dice" << std::endl;
        require (calmRepeats > 160 && calmRepeats > wildRepeats + 40, "SHAPERBOX repeats cells as the ShaperBox presets do, less with Chaos");
        require (softShare > hardShare * 2.0f, "...curves more with Soft");
        require (sb.size() > 390 && shared <= 4, "...and is its own dice, not the Infiltrator one (at most a simple gate in common)");
    }
    {
        // GROSS BEAT (TIME): on Triad Time's grid, octave and reverse lines and chops as in the banks; Chaos: more going on.
        mod::GridInfo grid;
        grid.columns = 64;
        grid.aspect = 3.48f;
        juce::Random r (11);
        int withMove = 0, octaveDown = 0, reverse = 0, octaveUp = 0, otherPitch = 0, calmPoints = 0, wildPoints = 0, softCurves = 0, hardCurves = 0;
        for (int i = 0; i < 500; ++i)
        {
            const auto s = mod::generate ({ 0.8f, 0.2f, 0.35f, mod::Source::grossBeatTime }, grid, r);
            bool move = false;
            for (size_t k = 1; k < s.size(); ++k)
            {
                const auto& a = s[k - 1];
                const auto& b = s[k];
                if (juce::exactlyEqual (a.x, b.x)) { move |= b.y < a.y; continue; }   // a jump back: a chop
                if (juce::exactlyEqual (a.y, b.y)) continue;
                if (std::abs (a.curve) > 1.0e-3f) { move = true; continue; }          // a tape stop or start
                const double speed = 1.0 + ((double) b.y - a.y) / ((double) b.x - a.x);
                move = true;
                if (std::abs (speed - 0.5) < 1.0e-3 || std::abs (speed - 0.25) < 1.0e-3) ++octaveDown;
                else if (speed < 0.0) ++reverse;
                else if (std::abs (speed - 2.0) < 1.0e-3 || std::abs (speed - 4.0) < 1.0e-3) ++octaveUp;
                else ++otherPitch;
            }
            withMove += move ? 1 : 0;
            calmPoints += (int) mod::generate ({ 0.8f, 0.2f, 0.0f, mod::Source::grossBeatTime }, grid, r).size();
            wildPoints += (int) mod::generate ({ 0.8f, 0.2f, 1.0f, mod::Source::grossBeatTime }, grid, r).size();
            softCurves += look (mod::generate ({ 0.2f, 1.0f, 0.35f, mod::Source::grossBeatTime }, grid, r), 3.48f).curves;
            hardCurves += look (mod::generate ({ 1.0f, 0.0f, 0.35f, mod::Source::grossBeatTime }, grid, r), 3.48f).curves;
        }
        std::cout << "GROSS BEAT (TIME) dice: " << withMove / 5 << "% with a line or a chop; lines: octave down " << octaveDown << ", reverse " << reverse
                  << ", octave up " << octaveUp << ", other " << otherPitch << "; points " << calmPoints / 500 << " at Chaos 0, " << wildPoints / 500
                  << " at Chaos 100; curves " << softCurves << " (Soft) / " << hardCurves << " (Angular)" << std::endl;
        require (withMove == 500, "GROSS BEAT (TIME) patterns bend, chop or tape-stop (none is flat)");
        require (octaveDown > octaveUp && reverse > octaveUp && octaveDown + reverse + octaveUp > 4 * otherPitch,
                 "...mostly octave-down and reversed lines (then up), as in the banks; other pitches far fewer");
        require (wildPoints > calmPoints * 13 / 10 && softCurves > hardCurves * 3, "...more going on with Chaos, tape stops with Soft");
    }
    {
        // TIME PRESETS: Chaos 0 is one of the 1,000 patterns as it is; Chaos 100 moves away from them.
        mod::GridInfo grid;
        grid.columns = 64;
        std::set<juce::String> presets;
        for (int i = 0; i < triad::timePresets::numPresets; ++i) presets.insert (mod::toText (triad::timePresets::shape (i)));
        juce::Random r (5);
        int calmSame = 0, wildSame = 0;
        for (int i = 0; i < 200; ++i)
        {
            calmSame += presets.count (mod::toText (mod::generate ({ 0.7f, 0.3f, 0.0f, mod::Source::timePresets }, grid, r))) > 0 ? 1 : 0;
            wildSame += presets.count (mod::toText (mod::generate ({ 0.7f, 0.3f, 1.0f, mod::Source::timePresets }, grid, r))) > 0 ? 1 : 0;
        }
        std::cout << "TIME PRESETS dice: " << calmSame / 2 << "% a TIME pattern as it is at Chaos 0, " << wildSame / 2 << "% at Chaos 100" << std::endl;
        require (calmSame == 200 && wildSame < 20, "TIME PRESETS: the patterns themselves at Chaos 0, further from them with Chaos");
    }
    {
        // GROSS BEAT (VOLUME): gates, as the banks' volume patterns.
        juce::Random r (13);
        int gated = 0;
        for (int i = 0; i < 300; ++i) gated += look (mod::generate ({ 0.8f, 0.2f, 0.35f, mod::Source::grossBeatVolume }, mod::GridInfo(), r)).steps >= 2 ? 1 : 0;
        std::cout << "GROSS BEAT (VOLUME) dice: " << gated / 3 << "% gates (2 or more steps)" << std::endl;
        require (gated > 180, "GROSS BEAT (VOLUME) makes gates, as the banks' volume patterns");
    }
    {
        // Triad's LFO dice (Thomas, 0.13.0): Chaos also makes more triangles and more squares, each with more points.
        // Peaks: a rising line then a falling one (or the other way); squares: steps.
        const auto peaks = [] (const mod::Shape& s)
        {
            int count = 0, slope = 0;
            for (size_t k = 1; k < s.size(); ++k)
            {
                const auto& a = s[k - 1];
                const auto& b = s[k];
                if (juce::exactlyEqual (a.x, b.x) || juce::exactlyEqual (a.y, b.y)) continue;
                const int dir = b.y > a.y ? 1 : -1;
                if (slope != 0 && dir != slope) ++count;
                slope = dir;
            }
            return count;
        };
        for (const auto source : { mod::Source::infiltrator, mod::Source::shaperBox })
        {
            juce::Random r (21);
            int calmPeaks = 0, wildPeaks = 0, calmSteps = 0, wildSteps = 0, calmPoints = 0, wildPoints = 0, plainPeaks = 0, plainPoints = 0;
            for (int i = 0; i < 400; ++i)
            {
                mod::GridInfo grid;
                grid.columns = i % 2 == 0 ? 16 : 32;
                const auto calm = mod::generate ({ 0.8f, 0.2f, 0.1f, source, true }, grid, r);
                const auto wild = mod::generate ({ 0.8f, 0.2f, 1.0f, source, true }, grid, r);
                const auto plain = mod::generate ({ 0.8f, 0.2f, 1.0f, source, false }, grid, r);
                calmPeaks += peaks (calm); wildPeaks += peaks (wild); plainPeaks += peaks (plain);
                calmSteps += look (calm).steps; wildSteps += look (wild).steps;
                calmPoints += (int) calm.size(); wildPoints += (int) wild.size(); plainPoints += (int) plain.size();
                require (validShape (calm) && validShape (wild) && validShape (plain), "...valid shapes");
                for (const auto* shape : { &calm, &wild })
                    require (std::any_of (shape->begin(), shape->end(), [shape] (const mod::Point& p) { return ! juce::exactlyEqual (p.y, shape->front().y); }),
                             "...and never a flat line");
            }
            std::cout << "Triad's LFO dice, " << mod::sourceName (source) << ", Chaos 10 -> 100: peaks " << calmPeaks / 4 << " -> " << wildPeaks / 4
                      << ", squares (steps) " << calmSteps / 4 << " -> " << wildSteps / 4 << ", points " << calmPoints / 4 << " -> " << wildPoints / 4
                      << " a hundred shapes (without the LFO Chaos, as Triad Time's dice: peaks " << plainPeaks / 4 << ", points " << plainPoints / 4 << ")" << std::endl;
            require (wildPeaks > calmPeaks * 2 && wildPeaks > plainPeaks * 13 / 10, ("Triad's LFO dice: Chaos makes more triangles (" + mod::sourceName (source) + ")").toRawUTF8());
            require (wildSteps > calmSteps * 3 / 2, "...more squares");
            // More points than Triad Time's dice too (by 6 to 11% in these hundred shapes, depending on the system's maths
            // library: only "more" is required).
            require (wildPoints > calmPoints * 3 / 2 && wildPoints > plainPoints, "...and more points");
        }
    }
}
}
