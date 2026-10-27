#include "Modulation.h"

namespace triad::mod
{
// Exponential segments: f(t) = (e^(k t) - 1) / (e^k - 1). Reversing a segment is exactly k -> -k,
// and any part of a segment is again such a curve, so 2x, 1/2 and reverse never distort a shape.
float curveFraction (float t, float k)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    if (std::abs (k) < 1.0e-3f) return t;
    return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
}
float curveForMidpoint (float m)
{
    m = juce::jlimit (0.012f, 0.988f, m);
    return juce::jlimit (-maxCurve, maxCurve, 2.0f * std::log (1.0f / m - 1.0f));
}

float evaluate (const Shape& s, float x)
{
    if (s.empty()) return 1.0f;
    if (x <= s.front().x) return s.front().y;
    for (size_t i = 0; i + 1 < s.size(); ++i)
    {
        const auto& a = s[i];
        const auto& b = s[i + 1];
        if (x < b.x)
        {
            const float width = b.x - a.x;
            if (width <= 1.0e-7f) continue;
            return a.y + (b.y - a.y) * curveFraction ((x - a.x) / width, a.curve);
        }
    }
    return s.back().y;
}

void normalise (Shape& s)
{
    for (auto& p : s)
    {
        p.x = juce::jlimit (0.0f, 1.0f, p.x);
        p.y = juce::jlimit (0.0f, 1.0f, p.y);
        p.curve = juce::jlimit (-maxCurve, maxCurve, p.curve);
    }
    std::stable_sort (s.begin(), s.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
    if (s.empty()) { s = flat(); return; }
    if (s.front().x > 0.0f) s.insert (s.begin(), Point { 0.0f, s.front().y, 0.0f });
    if (s.back().x < 1.0f) s.push_back (Point { 1.0f, s.back().y, 0.0f });
    // At most two points share an x (a vertical step).
    Shape out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const bool sameAsTwoBefore = out.size() >= 2 && juce::exactlyEqual (out[out.size() - 2].x, s[i].x)
                                     && juce::exactlyEqual (out.back().x, s[i].x);
        if (sameAsTwoBefore) out.back() = s[i];
        else out.push_back (s[i]);
    }
    out.back().curve = 0.0f;
    s = std::move (out);
}

Shape flat (float y) { return { Point { 0.0f, y, 0.0f }, Point { 1.0f, y, 0.0f } }; }

Shape removeRedundant (const Shape& in)
{
    Shape s = in;
    // Drop a point that duplicates its neighbour exactly (same x and y).
    Shape out;
    for (const auto& p : s)
        if (out.empty() || ! (juce::exactlyEqual (out.back().x, p.x) && juce::exactlyEqual (out.back().y, p.y)))
            out.push_back (p);
        else out.back().curve = p.curve;
    if (out.size() < 2) return flat (in.empty() ? 1.0f : in.front().y);
    return out;
}

Shape repeated (const Shape& in, int times)
{
    times = juce::jlimit (1, 64, times);
    if (times == 1) return in;
    Shape out;
    for (int r = 0; r < times; ++r)
        for (const auto& p : in)
            out.push_back (Point { ((float) r + p.x) / (float) times, p.y, p.curve });
    return removeRedundant (out);
}

Shape halved (const Shape& in)
{
    Shape out;
    bool middleTaken = false;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const auto& p = in[i];
        // At a vertical step exactly in the middle, the value before the step ends the half.
        const bool middle = juce::exactlyEqual (p.x, 0.5f);
        if (p.x < 0.5f || (middle && ! middleTaken))
        {
            middleTaken |= middle;
            out.push_back (Point { p.x * 2.0f, p.y, p.curve });
            // Split the segment that crosses the middle; each part of a curve keeps its bend.
            if (i + 1 < in.size() && in[i + 1].x > 0.5f && p.x < 0.5f)
            {
                const auto& q = in[i + 1];
                const float s = (0.5f - p.x) / (q.x - p.x);
                out.back().curve = p.curve * s;
                out.push_back (Point { 1.0f, p.y + (q.y - p.y) * curveFraction (s, p.curve), 0.0f });
            }
        }
    }
    normalise (out);
    return removeRedundant (out);
}

Shape reversed (const Shape& in)
{
    Shape out;
    for (size_t i = in.size(); i-- > 0;)
    {
        Point p { 1.0f - in[i].x, in[i].y, 0.0f };
        if (i > 0) p.curve = -in[i - 1].curve;   // the segment that now starts here ran the other way
        out.push_back (p);
    }
    out.back().curve = 0.0f;
    return out;
}

Shape shifted (const Shape& in, float dx)
{
    float d = dx - std::floor (dx);              // 0..1: how far right
    if (in.size() < 2 || d < 1.0e-6f || d > 1.0f - 1.0e-6f) return in;
    // Cut the cycle where it will wrap (u: the part from u to 1 comes round to the start), splitting the segment there.
    const float u = 1.0f - d;
    Shape cut;
    for (size_t i = 0; i < in.size(); ++i)
    {
        cut.push_back (in[i]);
        if (i + 1 < in.size() && in[i].x < u && in[i + 1].x > u)
        {
            const auto& a = in[i];
            const auto& b = in[i + 1];
            const float t = (u - a.x) / (b.x - a.x);
            const float y = a.y + (b.y - a.y) * curveFraction (t, a.curve);
            cut.back().curve = a.curve * t;
            cut.push_back (Point { u, y, a.curve * (1.0f - t) });
        }
    }
    Shape out;
    for (const auto& p : cut) if (p.x >= u) out.push_back (Point { p.x - u, p.y, p.curve });   // the end comes round first
    if (! out.empty()) out.back().curve = 0.0f;                                                 // (from u to 1, then a step)
    for (const auto& p : cut) if (p.x <= u) out.push_back (Point { p.x + d, p.y, p.curve });
    normalise (out);
    return removeRedundant (out);
}

Shape flippedVertical (const Shape& in)
{
    Shape out = in;
    for (auto& p : out) p.y = 1.0f - p.y;
    return out;
}

Shape snapped (const Shape& in, int columns, int rows)
{
    Shape out = in;
    for (auto& p : out)
    {
        if (columns > 0) p.x = (float) juce::roundToInt (p.x * (float) columns) / (float) columns;
        if (rows > 0) p.y = (float) juce::roundToInt (p.y * (float) rows) / (float) rows;
    }
    normalise (out);
    return removeRedundant (out);
}

namespace
{
const std::vector<Length> lengthTable {
    { "1/16T", 1.0 / 6.0, 0, 0, 0 }, { "1/8T", 1.0 / 3.0, 0, 0, 0 }, { "1/4T", 2.0 / 3.0, 0, 0, 0 }, { "1/2T", 4.0 / 3.0, 0, 0, 0 },
    { "1/128", 1.0 / 32.0, 0, 0, 1 }, { "1/64", 1.0 / 16.0, 0, 0, 1 }, { "1/32", 0.125, 0, 0, 1 }, { "1/16", 0.25, 0, 0, 1 },
    { "1/8", 0.5, 0, 0, 1 }, { "1/4", 1.0, 0, 0, 1 }, { "1/2", 2.0, 0, 0, 1 }, { "1 Bar", 0, 1, 0, 1 }, { "2 Bars", 0, 2, 0, 1 },
    { "4 Bars", 0, 4, 0, 1 }, { "8 Bars", 0, 8, 0, 1 }, { "16 Bars", 0, 16, 0, 1 }, { "32 Bars", 0, 32, 0, 1 },
    { "1/16d", 0.375, 0, 0, 2 }, { "1/8d", 0.75, 0, 0, 2 }, { "1/4d", 1.5, 0, 0, 2 }, { "3/4", 3.0, 0, 0, 2 },
    { "1.5 Bars", 0, 1.5, 0, 2 }, { "3 Bars", 0, 3, 0, 2 },
    { "0.1 Hz", 0, 0, 0.1, 3 }, { "0.25 Hz", 0, 0, 0.25, 3 }, { "0.5 Hz", 0, 0, 0.5, 3 }, { "1 Hz", 0, 0, 1.0, 3 },
    { "2 Hz", 0, 0, 2.0, 3 }, { "4 Hz", 0, 0, 4.0, 3 }, { "8 Hz", 0, 0, 8.0, 3 }, { "16 Hz", 0, 0, 16.0, 3 },
};
}

const std::vector<Length>& lengths() { return lengthTable; }
int defaultLength() { return 11; }
double lengthInBeats (int index, double beatsPerBar)
{
    if (index < 0 || index >= (int) lengthTable.size()) index = defaultLength();
    const auto& l = lengthTable[(size_t) index];
    if (l.column == 3) return 0.0;
    return l.bars > 0.0 ? l.bars * beatsPerBar : l.beats;
}
double lengthInHz (int index)
{
    if (index < 0 || index >= (int) lengthTable.size()) return 0.0;
    return lengthTable[(size_t) index].hz;
}

Table::Table() { for (auto& v : values) v.store (1.0f, std::memory_order_relaxed); }
void Table::render (const Shape& s)
{
    for (int i = 0; i <= size; ++i)
        values[(size_t) i].store (evaluate (s, (float) i / (float) size), std::memory_order_relaxed);
    // A vertical step lands exactly on a table entry: keep the value just before it too.
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if (juce::exactlyEqual (s[i].x, s[i + 1].x) && s[i].x > 0.0f && s[i].x < 1.0f)
        {
            const auto index = (int) std::floor (s[i].x * (float) size);
            values[(size_t) juce::jlimit (0, size, index)].store (s[i].y, std::memory_order_relaxed);
        }
}
float Table::lookup (double phase) const noexcept
{
    phase -= std::floor (phase);
    const double position = phase * size;
    const int i = juce::jlimit (0, size - 1, (int) position);
    const float f = (float) (position - i);
    const float a = values[(size_t) i].load (std::memory_order_relaxed);
    const float b = values[(size_t) i + 1].load (std::memory_order_relaxed);
    return a + (b - a) * f;
}

std::unique_ptr<juce::XmlElement> toXml (const Shape& s, const juce::String& tag)
{
    auto xml = std::make_unique<juce::XmlElement> (tag);
    for (const auto& p : s)
    {
        auto* e = xml->createNewChildElement ("P");
        e->setAttribute ("x", (double) p.x);
        e->setAttribute ("y", (double) p.y);
        if (! juce::exactlyEqual (p.curve, 0.0f)) e->setAttribute ("c", (double) p.curve);
    }
    return xml;
}
Shape fromXml (const juce::XmlElement* xml)
{
    Shape s;
    if (xml != nullptr)
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
            s.push_back (Point { (float) e->getDoubleAttribute ("x"), (float) e->getDoubleAttribute ("y", 1.0),
                                 (float) e->getDoubleAttribute ("c", 0.0) });
    if (s.size() > 4096) s.resize (4096);
    normalise (s);
    return s;
}
juce::String toText (const Shape& s) { return toXml (s, "TriadLfoShape")->toString (juce::XmlElement::TextFormat().singleLine()); }
Shape fromText (const juce::String& text)
{
    auto xml = juce::parseXML (text);
    if (xml == nullptr || ! xml->hasTagName ("TriadLfoShape")) return {};
    return fromXml (xml.get());
}
}
