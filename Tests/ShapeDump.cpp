// Development tool: prints LFO shapes as JSON, to look at the preset bank and the generator's output.
//   TriadShapeDump presets
//   TriadShapeDump generate <angular 0..1> <soft 0..1> <chaos 0..1> <count> [seed] [columns]
//   TriadShapeDump library > Source/GeneratedShapes.h      (the GEN folders of the preset palette)
//   TriadShapeDump extras <sheet.json>... > Source/ExtraShapes.h   (the EXTRA folder: shapes from the preview sheets)
#include "../Source/Generator.h"
#include <cstdio>
#include <iostream>
#include <set>

static void print (const triad::mod::Shape& s, const juce::String& name, const juce::String& category)
{
    std::cout << "{\"name\":\"" << name << "\",\"category\":\"" << category << "\",\"points\":[";
    for (size_t i = 0; i < s.size(); ++i)
        std::cout << (i ? "," : "") << "[" << s[i].x << "," << s[i].y << "," << s[i].curve << "]";
    std::cout << "]}";
}


// ---- The generated folders --------------------------------------------------------------------------
// Shapes from the generator, sorted into folders by what they are made of, each one clearly different
// from the others (and from the hand-made presets), simpler shapes first. Written out as text, so the
// folders stay the same even if the generator changes later.
namespace
{
namespace mod = triad::mod;

struct Look { int steps = 0, holds = 0, lines = 0, curves = 0; };
Look look (const mod::Shape& s)
{
    Look l;
    for (size_t i = 1; i < s.size(); ++i)
    {
        const auto& a = s[i - 1];
        const auto& b = s[i];
        if (juce::exactlyEqual (a.x, b.x)) { if (! juce::exactlyEqual (a.y, b.y)) ++l.steps; continue; }
        if (juce::exactlyEqual (a.y, b.y)) { ++l.holds; continue; }
        if (std::abs (a.curve) > 1.0e-3f) { ++l.curves; continue; }
        ++l.lines;
    }
    return l;
}
std::vector<float> trace (const mod::Shape& s)
{
    std::vector<float> v;
    for (int i = 0; i < 192; ++i) v.push_back (mod::evaluate (s, ((float) i + 0.5f) / 192.0f));
    return v;
}
float difference (const std::vector<float>& a, const std::vector<float>& b)
{
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) sum += std::abs (a[i] - b[i]);
    return sum / (float) a.size();
}

struct Folder
{
    const char* name;
    const char* word;
    int columns;
    std::function<mod::Style (juce::Random&)> style;
    std::function<bool (const Look&)> keep;
};

juce::String number (float v)
{
    if (std::abs (v) < 5.0e-7f) return "0";
    char text[32];
    std::snprintf (text, sizeof (text), "%.6g", (double) v);
    return text;
}

void library()
{
    const std::vector<Folder> folders {
        { "GEN: SQUARES", "Steps", 16, [] (juce::Random& r) { return mod::Style { 1.0f, 0.0f, 0.7f * r.nextFloat() }; },
          [] (const Look& l) { return l.lines == 0 && l.curves == 0 && l.steps >= 2; } },
        { "GEN: ANGLES", "Angles", 16, [] (juce::Random& r) { return mod::Style { 1.0f, 0.0f, 0.75f * r.nextFloat() }; },
          [] (const Look& l) { return l.curves == 0 && l.lines >= 2; } },
        { "GEN: HARD+CURVE", "Hard curve", 16, [] (juce::Random& r) { return mod::Style { 1.0f, 0.35f + 0.5f * r.nextFloat(), 0.7f * r.nextFloat() }; },
          [] (const Look& l) { return l.steps >= 1 && l.curves >= 1; } },
        { "GEN: SOFT", "Soft", 16, [] (juce::Random& r) { return mod::Style { 0.0f, 1.0f, 0.75f * r.nextFloat() }; },
          [] (const Look& l) { return l.steps == 0 && l.curves >= 1; } },
        { "GEN: TRIPLETS", "Triplet", 12, [] (juce::Random& r) { return mod::Style { 1.0f, r.nextFloat() < 0.7f ? 0.0f : 0.5f, 0.65f * r.nextFloat() }; },
          [] (const Look& l) { return l.steps + l.lines >= 2; } },
        { "GEN: CHAOS", "Chaos", 16, [] (juce::Random& r) { return mod::Style { 0.5f + 0.5f * r.nextFloat(), r.nextFloat() < 0.5f ? 0.0f : 0.6f * r.nextFloat(), 0.75f + 0.25f * r.nextFloat() }; },
          [] (const Look&) { return true; } },
    };
    constexpr int perFolder = 44;   // four pages of 11
    std::vector<std::vector<float>> taken;
    for (int i = 0; i < mod::numPresets(); ++i)
        if (! mod::isGeneratedCategory (mod::presetCategory (i))) taken.push_back (trace (mod::preset (i)));

    std::cout << "#pragma once\n"
                 "// The GEN folders of the LFO preset palette: shapes made by Triad's generator (Generator.cpp),\n"
                 "// sorted by what they are made of, simpler ones first. Written by `TriadShapeDump library`; do not\n"
                 "// edit by hand. Each shape is \"x y curve\" points separated by ';'.\n"
                 "namespace triad::mod::generated\n{\n";
    std::cout << "inline constexpr int numFolders = " << folders.size() << ";\n";
    std::cout << "inline constexpr const char* folderNames[] { ";
    for (size_t f = 0; f < folders.size(); ++f) std::cout << (f ? ", " : "") << "\"" << folders[f].name << "\"";
    std::cout << " };\ninline constexpr const char* folderWords[] { ";
    for (size_t f = 0; f < folders.size(); ++f) std::cout << (f ? ", " : "") << "\"" << folders[f].word << "\"";
    std::cout << " };\nstruct Entry { int folder; const char* points; };\ninline constexpr Entry entries[] {\n";
    for (size_t f = 0; f < folders.size(); ++f)
    {
        const auto& folder = folders[f];
        juce::Random random ((juce::int64) (7001 + 131 * (int) f));
        mod::GridInfo grid;
        grid.columns = folder.columns;
        std::vector<mod::Shape> chosen;
        for (int attempt = 0; attempt < 40000 && (int) chosen.size() < perFolder; ++attempt)
        {
            const auto shape = mod::generate (folder.style (random), grid, random);
            if (! folder.keep (look (shape))) continue;
            const auto t = trace (shape);
            bool distinct = true;
            for (const auto& other : taken) if (difference (t, other) < 0.07f) { distinct = false; break; }
            if (! distinct) continue;
            taken.push_back (t);
            chosen.push_back (shape);
        }
        // Simpler shapes first: page 1 is the plainest, the last page the busiest.
        std::stable_sort (chosen.begin(), chosen.end(), [] (const mod::Shape& a, const mod::Shape& b) { return a.size() < b.size(); });
        std::cerr << folder.name << ": " << chosen.size() << " shapes" << std::endl;
        for (const auto& s : chosen)
        {
            juce::String text;
            for (size_t i = 0; i < s.size(); ++i)
                text << (i ? ";" : "") << number (s[i].x) << " " << number (s[i].y) << " " << number (s[i].curve);
            std::cout << "    { " << f << ", \"" << text << "\" },\n";
        }
    }
    std::cout << "};\n}\n";
}

// ---- The EXTRA folder ---------------------------------------------------------------------------------
// Every shape from the generator's preview sheets (JSON written by `TriadShapeDump generate`), minus exact
// repeats, each tagged with the hand-made folder it fits (SQUARES, STEPS, TRIANGLES, ANGLES, HARD + CURVE,
// SOFT), sorted by that and then plainest first.
int typeOf (const mod::Shape& s)
{
    enum { squares = 1, stairs, triangles, angles, hardCurve, soft };
    const auto l = look (s);
    if (l.curves > 0) return l.steps > 0 ? hardCurve : soft;
    if (l.lines == 0)
    {
        std::set<int> levels;
        for (const auto& p : s) levels.insert (juce::roundToInt (p.y * 64.0f));
        return levels.size() <= 2 ? squares : stairs;
    }
    return l.steps == 0 ? triangles : angles;
}
void extras (const juce::StringArray& files)
{
    std::vector<std::vector<float>> handMade;
    for (int i = 0; i < mod::numPresets(); ++i)
        if (! mod::isGeneratedCategory (mod::presetCategory (i)) && mod::presetCategoryName (mod::presetCategory (i)) != "EXTRA")
            handMade.push_back (trace (mod::preset (i)));
    struct Item { int type; mod::Shape shape; std::vector<float> t; };
    std::vector<Item> items;
    int read = 0, repeats = 0, flatOnes = 0;
    for (const auto& name : files)
    {
        const auto json = juce::JSON::parse (juce::File (name));
        if (auto* list = json.getArray())
            for (const auto& entry : *list)
            {
                mod::Shape s;
                if (auto* points = entry["points"].getArray())
                    for (const auto& p : *points)
                        s.push_back ({ (float) (double) p[0], (float) (double) p[1], (float) (double) p[2] });
                ++read;
                if (s.size() < 2) continue;
                float low = 1.0f, high = 0.0f;
                for (const auto& p : s) { low = juce::jmin (low, p.y); high = juce::jmax (high, p.y); }
                if (high - low < 0.1f) { ++flatOnes; continue; }
                const auto t = trace (s);
                bool repeat = false;
                for (const auto& other : handMade) repeat = repeat || difference (t, other) < 0.015f;
                for (const auto& other : items) repeat = repeat || difference (t, other.t) < 0.015f;
                if (repeat) { ++repeats; continue; }
                items.push_back ({ typeOf (s), s, t });
            }
    }
    std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
                      { return a.type != b.type ? a.type < b.type : a.shape.size() < b.shape.size(); });
    std::cerr << read << " shapes read, " << repeats << " repeats and " << flatOnes << " flat ones left out, " << items.size() << " kept" << std::endl;
    std::cout << "#pragma once\n"
                 "// The EXTRA folder of the LFO preset palette: every shape from the generator's preview sheets\n"
                 "// (minus repeats), each also listed in the hand-made folder it fits (`type`: 1 SQUARES, 2 STEPS,\n"
                 "// 3 TRIANGLES, 4 ANGLES, 5 HARD + CURVE, 6 SOFT). Written by `TriadShapeDump extras`; do not edit by\n"
                 "// hand. Each shape is \"x y curve\" points separated by ';'.\n"
                 "namespace triad::mod::extra\n{\n"
                 "struct Entry { int type; const char* points; };\ninline constexpr Entry entries[] {\n";
    for (const auto& item : items)
    {
        juce::String text;
        for (size_t i = 0; i < item.shape.size(); ++i)
            text << (i ? ";" : "") << number (item.shape[i].x) << " " << number (item.shape[i].y) << " " << number (item.shape[i].curve);
        std::cout << "    { " << item.type << ", \"" << text << "\" },\n";
    }
    std::cout << "};\n}\n";
}
}

int main (int argc, char** argv)
{
    namespace mod = triad::mod;
    const juce::String mode = argc > 1 ? argv[1] : "presets";
    if (mode == "library") { library(); return 0; }
    if (mode == "extras")
    {
        juce::StringArray files;
        for (int i = 2; i < argc; ++i) files.add (juce::String::fromUTF8 (argv[i]));
        extras (files);
        return 0;
    }
    std::cout << "[";
    if (mode == "presets")
    {
        for (int i = 0; i < mod::numPresets(); ++i)
        {
            if (i) std::cout << ",";
            print (mod::preset (i), mod::presetName (i), mod::presetCategoryName (mod::presetCategory (i)));
        }
    }
    else if (argc >= 6)
    {
        const mod::Style style { (float) juce::String (argv[2]).getDoubleValue(), (float) juce::String (argv[3]).getDoubleValue(),
                                 (float) juce::String (argv[4]).getDoubleValue() };
        const int count = juce::String (argv[5]).getIntValue();
        const auto seed = argc > 6 ? juce::String (argv[6]).getLargeIntValue() : (juce::int64) 1;
        mod::GridInfo grid;
        if (argc > 7) grid.columns = juce::String (argv[7]).getIntValue();
        juce::Random random (seed);
        for (int i = 0; i < count; ++i)
        {
            if (i) std::cout << ",";
            print (mod::generate (style, grid, random), juce::String (i), "generated");
        }
    }
    std::cout << "]" << std::endl;
    return 0;
}
