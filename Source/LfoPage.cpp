#include "LfoPage.h"
#include "CrossoverDisplay.h"
#include <set>

using namespace triad::ui;
namespace mod = triad::mod;

namespace
{
// A tab per LFO: "LFO 2" over the name of what it moves, with a light when it is switched on.
class LfoTab final : public juce::Button
{
public:
    LfoTab (juce::String top, juce::String bottom, bool on, juce::Colour c)
        : Button (bottom), line1 (std::move (top)), line2 (std::move (bottom)), active (on), colour (c)
    {
        setRadioGroupId (4711);
        setClickingTogglesState (false);
        setTooltip (line1 + ": " + line2 + (active ? " (on)" : " (off)") + ". Click the light: on / off. Right-click: more.");
    }
    // 0.20.0 (Thomas): the light switches the LFO on and off; right-click for its menu.
    std::function<void()> onLight, onMenu;
    juce::Rectangle<float> lightArea() const { return { (float) getWidth() - 26.0f, 0.0f, 26.0f, 22.0f }; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        handled = e.mods.isPopupMenu() || lightArea().contains (e.position);
        if (! handled) { Button::mouseDown (e); return; }
        // Later (either may rebuild the tabs, this one too).
        auto call = e.mods.isPopupMenu() ? onMenu : onLight;
        if (call) juce::MessageManager::callAsync (call);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! handled) Button::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (handled) { handled = false; return; }
        Button::mouseUp (e);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (getToggleState() ? colour.withAlpha (0.22f) : over ? slotRowHover : slotRow);
        g.fillRoundedRectangle (r, 5.0f);
        if (getToggleState()) { g.setColour (colour); g.fillRoundedRectangle (r.removeFromBottom (3.0f), 1.5f); }
        auto area = getLocalBounds().reduced (10, 4).withTrimmedRight (16);
        g.setColour (getToggleState() ? colour : dimTextColour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (line1, area.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (line2, area, juce::Justification::centredLeft, 1, 0.8f);
        const auto light = juce::Rectangle<float> ((float) getWidth() - 16.0f, 7.0f, 8.0f, 8.0f);
        g.setColour (active ? ledColour : currentPalette.ledOff);
        g.fillEllipse (light);
    }
private:
    juce::String line1, line2;
    bool active;
    juce::Colour colour;
    bool handled = false;
};

}

// ---- Band-edge LFOs: what the grid's height means ------------------------------------------------------------------
namespace
{
using D = TriadProcessor::Destination;
float uOf (float hz) { return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (1.0f, hz) / 20.0f) / std::log (1000.0f)); }
float hzOf (float u) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, u)); }
juce::String shortHz (float hz)
{
    if (hz >= 9950.0f) return juce::String (juce::roundToInt (hz / 1000.0f)) + "k";
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, 1) + "k";
    return juce::String (juce::roundToInt (hz));
}
// The band's edges (0..1 in octaves from 20 Hz) and what a line height v (0..1) takes them to, at 100%.
struct EdgeMap
{
    int edge = -1;
    float uL = 0.0f, uH = 1.0f;
    bool locked = false;   // design: kept inside the band as set (the grid's height is the band's range)
    float c() const { return 0.5f * (uL + uH); }
    float w() const { return uH - uL; }
    std::pair<float, float> at (float v) const   // the low and high edge (u)
    {
        if (locked && edge == D::lowEdge) return { uL + v * w(), uH };
        if (locked && edge == D::highEdge) return { uL, uL + v * w() };
        if (locked && edge == D::openEdges) { const float half = 0.5f * v * w(); return { c() - half, c() + half }; }
        if (edge == D::lowEdge) return { v, uH };
        if (edge == D::highEdge) return { uL, v };
        if (edge == D::moveEdges) { const float m = 0.5f * w() + v * (1.0f - w()); return { m - 0.5f * w(), m + 0.5f * w() }; }
        const float half = v * juce::jmax (c(), 1.0f - c());
        return { juce::jmax (0.0f, c() - half), juce::jmin (1.0f, c() + half) };
    }
    float mainHz (float v) const   // what the line stands for: the edge, or the band's middle when it sweeps
    {
        const auto [lo, hi] = at (v);
        return edge == D::lowEdge ? hzOf (lo) : edge == D::highEdge ? hzOf (hi) : hzOf (0.5f * (lo + hi));
    }
};
EdgeMap edgeMap (TriadProcessor& p, int band, int lfo)
{
    EdgeMap m;
    m.edge = p.getLfoInfo (band, lfo).edge;
    if (m.edge < 0 || TriadProcessor::isExtraChain (band)) { m.edge = -1; return m; }
    const auto cuts = p.effectiveCrossovers();
    if (band > 0 && band - 1 < (int) cuts.size()) m.uL = uOf (cuts[(size_t) band - 1]);
    if (band < (int) cuts.size()) m.uH = uOf (cuts[(size_t) band]);
    m.locked = m.edge != D::moveEdges && p.isEdgeLfoLocked (band, lfo);
    return m;
}
juce::String hzText (float hz) { return triad::ui::formatHz (hz); }
// The frequencies at the spectrum's two ends, small, in the grid's bottom corners (0.21.0).
void paintSpectrumEnds (juce::Graphics& g, juce::Rectangle<float> plot, std::pair<float, float> view)
{
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (const bool right : { false, true })
    {
        const auto text = triad::ui::formatHz (right ? view.second : view.first);
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 10.0f;
        const auto tag = juce::Rectangle<float> (right ? plot.getRight() - w - 4.0f : plot.getX() + 4.0f, plot.getBottom() - 18.0f, w, 14.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (triad::ui::dimTextColour); g.drawText (text, tag, juce::Justification::centred);
    }
}
juce::String changeText (float from, float to)
{
    const float d = to - from;
    if (std::abs (d) < 0.5f) return {};
    return (d > 0.0f ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + triad::ui::formatHz (std::abs (d));
}
}
// ---- BAND NOW: the band's curve as it is right now, and where the LFO takes it --------------------------------------
class LfoPage::EdgeNow final : public juce::Component, private juce::Timer
{
public:
    explicit EdgeNow (TriadProcessor& p) : processor (p) { setInterceptsMouseClicks (false, false); }
    void show (int b, int l) { band = b; lfo = l; key.clear(); repaint(); }
    void visibilityChanged() override { if (isVisible()) startTimerHz (30); else stopTimer(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (currentPalette.controls.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 6.0f);
        r.reduce (10.0f, 7.0f);
        const int count = processor.getBandCount();
        const auto cuts = processor.heardCrossovers();
        if (band < 0 || band >= count || (int) cuts.size() < count - 1) return;
        const auto colour = bandColour (band);
        const float top = processor.nyquistFrequency();
        const float lo = band > 0 ? cuts[(size_t) band - 1] : 0.0f, hi = band < count - 1 ? cuts[(size_t) band] : top;
        auto head = r.removeFromTop (16.0f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (dimTextColour);
        g.drawText ("BAND " + juce::String (band + 1) + " NOW", head, juce::Justification::centredLeft);
        g.setColour (colour);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (formatHz (lo) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + formatHz (hi), head.withTrimmedLeft (78.0f).toNearestInt(),
                          juce::Justification::centredRight, 1, 0.8f);
        const auto plot = r.withTrimmedTop (4.0f);
        // 20 Hz .. 20 kHz across, 0 .. -36 dB down; a faint line at 100 Hz, 1 kHz and 10 kHz.
        const auto xOf = [&] (float hz) { return plot.getX() + plot.getWidth() * uOf (hz); };
        g.setColour (currentPalette.gridLines.withAlpha (0.12f));
        for (float hz : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        std::vector<int> slopes;
        for (int j = 0; j + 1 < count; ++j) slopes.push_back (processor.getCrossoverSlope (j));
        const double sr = processor.getSampleRateForDisplay();
        const auto curve = [&] (int b, const std::vector<float>& at, bool floor = true)
        {
            juce::Path p;
            bool started = false;
            for (int i = 0; i <= 120; ++i)
            {
                const float f = hzOf ((float) i / 120.0f);
                float m = 1.0f;
                if (b > 0) m *= triad::CrossoverFilter::highMagnitude (slopes[(size_t) b - 1], f, at[(size_t) b - 1], sr);
                if (b < count - 1) m *= triad::CrossoverFilter::lowMagnitude (slopes[(size_t) b], f, at[(size_t) b], sr);
                const float db = juce::jlimit (-36.0f, 0.0f, juce::Decibels::gainToDecibels (m, -100.0f));
                const auto pt = juce::Point<float> (plot.getX() + plot.getWidth() * (float) i / 120.0f, plot.getY() + plot.getHeight() * (-db / 36.0f));
                if (! floor && db <= -35.9f) { started = false; continue; }   // a line: not along the floor
                if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
            }
            return p;
        };
        const auto filled = [&] (const juce::Path& line)
        {
            juce::Path fill (line);
            fill.lineTo (plot.getRight(), plot.getBottom()); fill.lineTo (plot.getX(), plot.getBottom()); fill.closeSubPath();
            return fill;
        };
        // The other bands, faint; this band, lit.
        for (int b = 0; b < count; ++b)
            if (b != band) { g.setColour (bandColour (b).withAlpha (0.1f)); g.fillPath (filled (curve (b, cuts))); }
        // Where the LFO takes the band: its lowest and highest (dashed).
        for (const auto& at : extremes (cuts, count))
        {
            juce::Path dashed;
            const float dash[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, curve (band, at, false), dash, 2);
            g.setColour (colour.withAlpha (0.6f));
            g.fillPath (dashed);
        }
        g.setColour (colour.withAlpha (0.24f));
        g.fillPath (filled (curve (band, cuts)));
        g.setColour (colour);
        g.strokePath (curve (band, cuts, false), juce::PathStrokeType (1.6f));
    }
private:
    // The crossovers with this band's moving edges at the two ends of their sweeps (which ends go together depends on
    // how the LFO moves the band).
    std::vector<std::vector<float>> extremes (const std::vector<float>& cuts, int count) const
    {
        const int edge = processor.getLfoInfo (band, lfo).edge;
        if (edge < 0) return {};
        const auto sweepOf = [&] (int j) -> std::pair<float, float>
        {
            if (j < 0 || j >= count - 1) return {};
            const auto s = processor.crossoverSweep (j);
            return s.second > 0.0f ? s : std::pair<float, float> { cuts[(size_t) j], cuts[(size_t) j] };
        };
        const int low = band - 1, high = band;
        const auto sl = sweepOf (low), sh = sweepOf (high);
        std::vector<std::vector<float>> ends (2, cuts);
        const auto set = [&] (std::vector<float>& at, int j, float hz) { if (j >= 0 && j < count - 1) at[(size_t) j] = hz; };
        if (edge == D::openEdges)
        {
            set (ends[0], low, sl.first); set (ends[0], high, sh.second);   // open
            set (ends[1], low, sl.second); set (ends[1], high, sh.first);   // closed
        }
        else
        {
            if (edge != D::highEdge) { set (ends[0], low, sl.first); set (ends[1], low, sl.second); }
            if (edge != D::lowEdge) { set (ends[0], high, sh.first); set (ends[1], high, sh.second); }
        }
        return ends;
    }
    void timerCallback() override
    {
        // Redrawn only when something it shows has changed.
        auto k = processor.heardCrossovers();
        k.push_back ((float) processor.getBandCount());
        for (int j = 0; j + 1 < processor.getBandCount(); ++j) k.push_back ((float) processor.getCrossoverSlope (j));
        for (int j : { band - 1, band }) { const auto s = processor.crossoverSweep (j); k.push_back (s.first); k.push_back (s.second); }
        if (k != key) { key = std::move (k); repaint(); }
    }
    TriadProcessor& processor;
    int band = 0, lfo = 0;
    std::vector<float> key;
};
bool LfoPage::isEdgeNowShowing() const { return edgeNow != nullptr && edgeNow->isVisible(); }

juce::String LfoShapeHost::valueLabel (int percent) const
{
    // A band-edge LFO: the grid's height is a frequency (the edge, or the band's middle when it sweeps), or how open the
    // band is (widen / narrow).
    const auto m = edgeMap (processor, band, lfo);
    if (m.edge < 0) return ShapeHost::valueLabel (percent);
    if (m.edge == D::openEdges) return percent == 100 ? "open" : percent == 0 ? "closed" : ShapeHost::valueLabel (percent);
    return shortHz (m.mainHz ((float) percent / 100.0f));
}
juce::String LfoShapeHost::pointLabel (const triad::mod::Point& p) const
{
    const auto m = edgeMap (processor, band, lfo);
    if (m.edge < 0) return {};
    if (m.edge == D::openEdges || m.edge == D::moveEdges)
    {
        // Both edges move: the band's range at this point.
        const auto [lo, hi] = m.at (p.y);
        return shortHz (hzOf (lo)) + juce::String::fromUTF8 ("\xe2\x80\x93") + shortHz (hzOf (hi)) + " Hz";
    }
    return hzText (m.mainHz (p.y));
}
juce::String LfoShapeHost::segmentLabel (const triad::mod::Point& from, const triad::mod::Point& to) const
{
    const auto m = edgeMap (processor, band, lfo);
    if (m.edge < 0 || m.edge == D::openEdges) return {};
    if (m.edge == D::moveEdges)
    {
        // Both edges move by the same number of octaves (in Hz each by its own amount).
        const float oct = (m.at (to.y).first - m.at (from.y).first) * std::log2 (1000.0f);
        if (std::abs (oct) < 0.05f) return {};
        return (oct > 0.0f ? juce::String::fromUTF8 ("\xe2\x86\x91 ") : juce::String::fromUTF8 ("\xe2\x86\x93 ")) + juce::String (std::abs (oct), 1) + " oct";
    }
    return changeText (m.mainHz (from.y), m.mainHz (to.y));
}
int LfoShapeHost::segmentLabelMode() const { return edgeMap (processor, band, lfo).edge >= 0 ? 2 : 0; }
bool LfoShapeHost::fillsUnderLine() const { return edgeMap (processor, band, lfo).edge < 0; }
std::vector<std::pair<float, juce::String>> LfoShapeHost::valueMarks() const
{
    // A frequency scale for an edge (or, sweeping, for the band's middle): 20, 50, 100, 200, 500, 1k ... 20k labelled,
    // with an unlabelled tick at every other step of each decade (30, 40, 60 ... 9k).
    const auto m = edgeMap (processor, band, lfo);
    std::vector<std::pair<float, juce::String>> marks;
    if (m.edge < 0 || m.edge == D::openEdges) return marks;
    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int k = 1; k <= 9; ++k)
        {
            const float hz = decade * (float) k;
            if (hz < 19.9f || hz > 20001.0f) continue;
            const float u = uOf (hz);
            const float v = m.locked ? (u - m.uL) / juce::jmax (0.001f, m.w())
                          : m.edge == D::moveEdges ? (u - 0.5f * m.w()) / juce::jmax (0.001f, 1.0f - m.w()) : u;
            const bool labelled = (k == 1 || k == 2 || k == 5) && ! (m.locked && (std::abs (v) < 0.06f || std::abs (v - 1.0f) < 0.06f));
            if (v >= -0.001f && v <= 1.001f) marks.push_back ({ juce::jlimit (0.0f, 1.0f, v), labelled ? shortHz (hz) : juce::String() });
        }
    if (m.locked) { marks.push_back ({ 0.0f, shortHz (hzOf (m.uL)) }); marks.push_back ({ 1.0f, shortHz (hzOf (m.uH)) }); }   // the band's edges
    return marks;
}
// ---- Behind the grid: the sound coming in --------------------------------------------------------------------------
bool LfoShapeHost::hasBackdrop() const { return page.hasBackdrop(); }
bool LfoPage::showsEdges() const { return edgeMap (processor, band, lfo).edge >= 0; }
bool LfoPage::hasBackdrop() const { return showsEdges() || getBackdrop() != TriadSettings::Backdrop::off; }
void LfoShapeHost::paintBackdrop (juce::Graphics& g, juce::Rectangle<float> plot) { page.paintBackdrop (g, plot); }

struct LfoPage::Backdrop
{
    std::array<float, triad::WaveTrace::bins> level {};
    std::array<int, triad::WaveTrace::bins> age {};
    triad::ui::SpectrumBackdrop spectrum;
};

TriadSettings::Backdrop LfoPage::getBackdrop() const
{
    // The setting (changed in any Triad's gear menu) is read twice a second, not every frame.
    const bool audio = processor.getTrigger (band).audio;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (audio != backdropAudio || now - backdropChecked > 500.0)
    {
        backdropAudio = audio;
        backdropChecked = now;
        backdropMode = settings->getLfoBackdrop (audio);
        fullSpectrum = settings->getLfoFullSpectrum();
    }
    return backdropMode;
}
bool LfoPage::spectrumShown() const { return showsEdges() ? edgeSpectrum : getBackdrop() == TriadSettings::Backdrop::spectrum; }
void LfoPage::setSpectrumShown (bool on)
{
    if (showsEdges()) edgeSpectrum = on;
    else
    {
        // The gear menu's choice for this band's TRIGGER: the spectrum, or back to what it was before.
        const bool audio = processor.getTrigger (band).audio;
        auto& before = beforeSpectrum[audio ? 1 : 0];
        const auto now = settings->getLfoBackdrop (audio);
        if (on && now != TriadSettings::Backdrop::spectrum) before = now;
        settings->setLfoBackdrop (audio, on ? TriadSettings::Backdrop::spectrum : before);
        backdropChecked = -1.0;
    }
    updateSpectrumButtons();
    shapes.getEditor().repaint();
}
void LfoPage::updateSpectrumButtons()
{
    const bool shown = spectrumShown();
    spectrumButton.setToggleState (shown, juce::dontSendNotification);
    fullSpectrumButton.setToggleState (fullSpectrum, juce::dontSendNotification);
    fullSpectrumButton.setAlpha (shown ? 1.0f : 0.6f);
}
std::pair<float, float> LfoPage::spectrumView (float lowU, float highU) const
{
    getBackdrop();   // the settings as they are
    if (fullSpectrum) return { 20.0f, 20000.0f };
    lowU = juce::jlimit (0.0f, 1.0f, lowU);
    highU = juce::jlimit (lowU, 1.0f, highU);
    constexpr float octave = 0.1f;   // the 10 octaves from 20 Hz to 20 kHz are 0..1
    if (highU - lowU < octave)
    {
        const float c = juce::jlimit (0.5f * octave, 1.0f - 0.5f * octave, 0.5f * (lowU + highU));
        lowU = c - 0.5f * octave;
        highU = c + 0.5f * octave;
    }
    return { hzOf (lowU), hzOf (highU) };
}
void LfoPage::visibilityChanged()
{
    if (! isVisible()) processor.setTracedLfo (-1, -1);   // nothing traced while the tab is closed
}
void LfoPage::paintBackdrop (juce::Graphics& g, juce::Rectangle<float> plot)
{
    // The band's slopes (0.20.0): the lit range fades as they do.
    const int lowOrder = ! TriadProcessor::isExtraChain (band) && band > 0 ? processor.getCrossoverSlope (band - 1) : 0;
    const int highOrder = ! TriadProcessor::isExtraChain (band) && band < processor.getBandCount() - 1 ? processor.getCrossoverSlope (band) : 0;
    if (backdrop == nullptr) backdrop = std::make_unique<Backdrop>();
    const auto colour = bandColour (band);
    if (const auto m = edgeMap (processor, band, lfo); m.edge >= 0)
    {
        // Design: what passes (lit in the band's colour) and what is cut (darker) across the cycle, as the line moves
        // the edges; then the spectrum, across or turned up the side.
        processor.setTracedLfo (-1, -1);
        const auto shape = processor.getLfoInfo (band, lfo).shape;
        const bool hzScale = m.edge != D::openEdges;
        const float bottomU = m.locked ? m.uL : m.edge == D::moveEdges ? 0.5f * m.w() : 0.0f, topU = m.locked ? m.uH : m.edge == D::moveEdges ? 1.0f - 0.5f * m.w() : 1.0f;
        const auto yForU = [&] (float u) { return plot.getBottom() - plot.getHeight() * (u - bottomU) / juce::jmax (0.001f, topU - bottomU); };
        juce::Path pass;
        std::vector<juce::Point<float>> lows, highs;
        float travelLow = 1.0f, travelHigh = 0.0f;   // where the band's edges go over the cycle
        for (float x = plot.getX(); x <= plot.getRight() + 0.5f; x += 3.0f)
        {
            const float v = triad::mod::evaluate (shape, (x - plot.getX()) / plot.getWidth());
            const auto [lo, hi] = m.at (v);
            travelLow = juce::jmin (travelLow, lo);
            travelHigh = juce::jmax (travelHigh, hi);
            if (hzScale) { lows.push_back ({ x, juce::jlimit (plot.getY(), plot.getBottom(), yForU (lo)) }); highs.push_back ({ x, juce::jlimit (plot.getY(), plot.getBottom(), yForU (hi)) }); }
            else
            {
                // Widen / narrow: the grid's height is how open it is; what passes drawn round the grid's middle, as
                // wide as the line is high (closed: nothing; open: all of it).
                const float mid = plot.getCentreY(), half = 0.5f * plot.getHeight() * juce::jlimit (0.0f, 1.0f, v);
                lows.push_back ({ x, mid + half });
                highs.push_back ({ x, mid - half });
            }
        }
        pass.startNewSubPath (highs.front());
        for (auto& p : highs) pass.lineTo (p);
        for (auto it = lows.rbegin(); it != lows.rend(); ++it) pass.lineTo (*it);
        pass.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.28f));   // cut
        g.fillRect (plot);
        g.setColour (colour.withAlpha (0.13f));                  // passes
        g.fillPath (pass);
        g.setColour (colour.withAlpha (0.55f));
        juce::Path lowLine, highLine;
        for (size_t i = 0; i < lows.size(); ++i)
        {
            if (i == 0) { lowLine.startNewSubPath (lows[i]); highLine.startNewSubPath (highs[i]); }
            else { lowLine.lineTo (lows[i]); highLine.lineTo (highs[i]); }
        }
        const float dash[] { 3.0f, 3.0f };
        juce::PathStrokeType stroke (1.0f);
        juce::Path dashedLow, dashedHigh;
        stroke.createDashedStroke (dashedLow, lowLine, dash, 2);
        stroke.createDashedStroke (dashedHigh, highLine, dash, 2);
        if (m.edge != D::lowEdge) g.fillPath (dashedHigh);
        if (m.edge != D::highEdge) g.fillPath (dashedLow);
        if (edgeSpectrum)
        {
            const auto heard = processor.heardCrossovers();
            const float lowHz = band > 0 && band - 1 < (int) heard.size() ? heard[(size_t) band - 1] : 20.0f;
            const float highHz = band < (int) heard.size() ? heard[(size_t) band] : 20000.0f;
            // Lit where the band is right now: it slides as the LFO moves the edges (Thomas: light up the part of the mix
            // being worked on).
            // Across: the band's range when locked (stretched so it fills the grid), else where the LFO takes its edges;
            // FULL SPECTRUM: 20 Hz to 20 kHz (0.21.0).
            const auto view = spectrumView (m.locked ? m.uL : travelLow, m.locked ? m.uH : travelHigh);
            backdrop->spectrum.paint (g, plot, processor.getSpectrumTap(), processor.getSampleRateForDisplay(), lowHz, highHz, colour, lowOrder, highOrder,
                                      view.first, view.second);
            paintSpectrumEnds (g, plot, view);
        }
        if (m.locked)
        {
            // A tag in the corner: what it is locked to.
            const auto text = juce::String::fromUTF8 ("LOCKED TO BAND ") + juce::String (band + 1) + juce::String::fromUTF8 ("  \xc2\xb7  ")
                            + triad::ui::formatHz (hzOf (m.uL)) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + triad::ui::formatHz (hzOf (m.uH));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
            const auto tag = juce::Rectangle<float> (plot.getX() + 8.0f, plot.getY() + 22.0f, tw, 17.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (colour); g.drawRoundedRectangle (tag.reduced (0.5f), 4.0f, 1.0f);
            g.drawText (text, tag, juce::Justification::centred);
        }
        return;
    }
    if (getBackdrop() != TriadSettings::Backdrop::spectrum)
    {
        processor.setTracedLfo (band, lfo);
        processor.readLfoTrace (backdrop->level, backdrop->age);
        const auto trigger = processor.getTrigger (band);
        triad::ui::drawWaveTrace (g, plot, backdrop->level, backdrop->age, colour, trigger.audio ? trigger.thresholdDb : std::numeric_limits<float>::quiet_NaN());
        return;
    }
    processor.setTracedLfo (-1, -1);
    // The spectrum (as the BANDS tab's): the band's range lit in its colour, the rest faint; across, the band as set
    // (0.21.0: as large as the grid, "you see the area you're affecting"), or 20 Hz to 20 kHz with FULL SPECTRUM.
    float low = 20.0f, high = 20000.0f, viewLow = 0.0f, viewHigh = 1.0f;
    if (! TriadProcessor::isExtraChain (band))
    {
        const auto cuts = processor.heardCrossovers();   // where the band is right now (with its edge LFOs)
        if (band > 0 && band - 1 < (int) cuts.size()) low = cuts[(size_t) band - 1];
        if (band < (int) cuts.size()) high = cuts[(size_t) band];
        const auto set = processor.effectiveCrossovers();
        if (band > 0 && band - 1 < (int) set.size()) viewLow = uOf (set[(size_t) band - 1]);
        if (band < (int) set.size()) viewHigh = uOf (set[(size_t) band]);
    }
    const auto view = spectrumView (viewLow, viewHigh);
    backdrop->spectrum.paint (g, plot, processor.getSpectrumTap(), processor.getSampleRateForDisplay(), low, high, colour, lowOrder, highOrder,
                              view.first, view.second);
    paintSpectrumEnds (g, plot, view);
}

// TRIGGER: SONG / AUDIO for the band's LFOs. With AUDIO: a level meter, the threshold on it, and a light that flashes
// when the LFOs start again (dim while they wait for sound).
class LfoPage::TriggerButton final : public juce::Button, private juce::Timer
{
public:
    explicit TriggerButton (LfoPage& p) : Button ("Trigger"), page (p)
    {
        setTooltip ("TRIGGER: SONG (the LFOs follow the song) or AUDIO (they start from their start when sound comes in, at the "
                    "song's tempo, for their length)");
        startTimerHz (30);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto settings = page.processor.getTrigger (page.band);
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (settings.audio ? currentPalette.controlsOn : over ? slotRowHover : slotRow);
        g.fillRoundedRectangle (r, 5.0f);
        auto area = getLocalBounds().reduced (10, 4);
        auto top = area.removeFromTop (14);
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("TRIGGER", top, juce::Justification::centredLeft);
        juce::Path arrow;
        const float ax = (float) getWidth() - 14.0f, ay = 11.0f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.fillPath (arrow);
        g.setColour (settings.audio ? accent : textColour);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        if (! settings.audio)
        {
            g.drawText ("SONG", area, juce::Justification::centredLeft);
            return;
        }
        g.drawText ("AUDIO", area.removeFromLeft (46), juce::Justification::centredLeft);
        // The light: flashes at every restart, dim while waiting for sound.
        const auto light = area.removeFromLeft (12).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        const float flash = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (juce::Time::getMillisecondCounterHiRes() - lastHit) / 250.0f);
        g.setColour (waiting ? currentPalette.ledOff : ledColour.withAlpha (0.45f + 0.55f * flash));
        g.fillEllipse (light.expanded (flash * 1.5f));
        area.removeFromLeft (6);
        // The level (-60 .. 0 dB) and the threshold on it.
        auto meter = area.withSizeKeepingCentre (area.getWidth(), 6).toFloat();
        g.setColour (currentPalette.meterBack);
        g.fillRoundedRectangle (meter, 3.0f);
        const auto toX = [&meter] (float db) { return meter.getX() + meter.getWidth() * juce::jlimit (0.0f, 1.0f, (db - TriadProcessor::minTriggerDb) / (TriadProcessor::maxTriggerDb - TriadProcessor::minTriggerDb)); };
        g.setColour (shownLevel >= settings.thresholdDb ? ledColour : dimTextColour);
        g.fillRoundedRectangle (meter.withRight (toX (shownLevel)), 3.0f);
        g.setColour (textColour);
        g.fillRect (juce::Rectangle<float> (toX (settings.thresholdDb) - 1.0f, meter.getY() - 3.0f, 2.0f, meter.getHeight() + 6.0f));
    }
private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        const auto status = page.processor.getTriggerStatus (page.band);
        shownLevel = juce::jmax (status.levelDb, shownLevel - 2.0f);   // falls back smoothly
        if (status.count != lastCount) { lastCount = status.count; lastHit = juce::Time::getMillisecondCounterHiRes(); }
        waiting = status.waiting;
        if (page.processor.getTrigger (page.band).audio) repaint();
    }
    LfoPage& page;
    float shownLevel = -100.0f;
    int lastCount = 0;
    double lastHit = 0.0;
    bool waiting = true;
};

// ---- LFO page -----------------------------------------------------------------------------------------
LfoPage::LfoPage (TriadProcessor& p) : processor (p)
{
    triggerButton = std::make_unique<TriggerButton> (*this);
    triggerButton->onClick = [this] { showTriggerMenu(); };
    addAndMakeVisible (*triggerButton);
    setTitle ("LFO");
    tabView.setViewedComponent (&tabHolder, false);
    tabView.setScrollBarsShown (false, true);
    tabView.setScrollBarThickness (5);
    addAndMakeVisible (tabView);
    addLfoButton.onClick = [this]
    {
        const int index = processor.addLfo (band);
        if (index >= 0) select (band, index);
    };
    addAndMakeVisible (addLfoButton);
    shapes.lengthTooltip = "LFO length: one cycle of the shape (synced to your song's tempo)";
    shapes.onChange = [this] { refresh(); };
    shapes.askName = [this] (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
    {
        if (askName != nullptr) askName (title, initial, std::move (done));
        else done (initial);
    };
    shapes.setLayout (ShapePanel::Layout::browser);   // 0.16.1: as Triad Time's (the browser under the grid)
    addAndMakeVisible (shapes);
    addAndMakeVisible (shapes.getBrowser());
    generator.setTarget (&shapes.getEditor());
    generator.setSources ({ triad::mod::Source::infiltrator, triad::mod::Source::shaperBox });   // each trained on its own set
    addAndMakeVisible (generator);
    addRouteButton.setTooltip ("Send this LFO to another knob in this band: a mix, the band's volume, or a knob inside one of its effects "
                               "(from the list, or LEARN: move the knob in the effect's window)");
    addRouteButton.onClick = [this]
    {
        if (processor.isLearningParameter()) { processor.cancelParameterLearn(); updateToolbar(); return; }
        enum { learnId = 100000 };   // + the slot whose window LEARN opens
        juce::PopupMenu menu;
        menu.addSectionHeader ("ROUTE " + processor.getLfoInfo (band, lfo).name.toUpperCase() + " TO");
        std::vector<TriadProcessor::Destination> list;
        addDestinationItems (menu, processor, band, nullptr, list);
        menu.addSeparator();
        // LEARN opens the effect's window if it is closed (an open one is left as it is). With several effects,
        // pick the one to open; a knob moved in any of the band's open windows is linked.
        std::vector<int> loaded;
        for (int s = 0; s < processor.getSlotCount (band); ++s)
            if (processor.getSlotInfo (band, s).loaded) loaded.push_back (s);
        if (loaded.size() == 1)
        {
            menu.addItem (learnId + loaded[0], "LEARN: move a knob in " + processor.getSlotInfo (band, loaded[0]).name + "'s window...");
        }
        else if (loaded.size() > 1)
        {
            juce::PopupMenu effects;
            effects.addSectionHeader ("OPEN ITS WINDOW, THEN MOVE A KNOB");
            for (int s : loaded)
                addItemWithRight (effects, learnId + s, juce::String (s + 1) + u8 (" \xc2\xb7 ") + processor.getSlotInfo (band, s).name,
                                  processor.isEffectWindowOpen (band, s) ? "open" : "");
            addSubMenu (menu, "LEARN: move a knob in an effect's window", std::move (effects));
        }
        else
        {
            menu.addItem (learnId, "LEARN: move a knob in an effect's window...", false);
        }
        juce::Component::SafePointer<LfoPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addRouteButton), [safe, list] (int choice)
        {
            if (safe == nullptr) return;
            if (choice >= learnId) { safe->startLearn (choice - learnId); return; }
            if (choice < 1 || choice > (int) list.size()) return;
            safe->processor.addRoute (safe->band, safe->lfo, list[(size_t) choice - 1]);
            safe->refresh();
        });
    };
    addAndMakeVisible (addRouteButton);
    learnButton.setTooltip ("LEARN: opens the effect's window (left as it is when it is open already); move a knob there and it is linked to this LFO");
    learnButton.onClick = [this] { showLearnMenu(); };
    addAndMakeVisible (learnButton);
    routeView.setViewedComponent (&routeHolder, false);
    routeView.setScrollBarsShown (true, false);
    routeView.setScrollBarThickness (5);
    addAndMakeVisible (routeView);
    spectrumButton.setClickingTogglesState (true);
    spectrumButton.setToggleState (edgeSpectrum, juce::dontSendNotification);
    spectrumButton.setTooltip ("The sound's spectrum behind the grid (on / off)");
    spectrumButton.onClick = [this] { setSpectrumShown (spectrumButton.getToggleState()); };
    spectrumButton.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
    spectrumButton.setColour (juce::TextButton::textColourOnId, inkColour);
    addAndMakeVisible (spectrumButton);
    fullSpectrumButton.setClickingTogglesState (true);
    fullSpectrumButton.setTooltip ("FULL SPECTRUM: 20 Hz to 20 kHz behind the grid. Off: just the range this LFO works in (its band, or where it takes "
                                   "the band's edges), as large as the grid");
    fullSpectrumButton.onClick = [this]
    {
        fullSpectrum = fullSpectrumButton.getToggleState();
        settings->setLfoFullSpectrum (fullSpectrum);
        if (! spectrumShown()) setSpectrumShown (true);
        updateSpectrumButtons();
        shapes.getEditor().repaint();
    };
    fullSpectrumButton.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
    fullSpectrumButton.setColour (juce::TextButton::textColourOnId, inkColour);
    addAndMakeVisible (fullSpectrumButton);
    fullSpectrum = settings->getLfoFullSpectrum();
    slopeButton.setTooltip ("How soft the edges this LFO moves are: 6 dB/oct blends the bands, 96 dB/oct is a hard wall (the slope of those dividers, as on the BANDS tab)");
    slopeButton.onClick = [this] { showSlopeMenu(); };
    addChildComponent (slopeButton);
    edgeNow = std::make_unique<EdgeNow> (processor);
    addChildComponent (*edgeNow);
    removeLfoButton.onClick = [this]
    {
        if (processor.removeLfo (band, lfo)) select (band, juce::jmax (0, lfo - 1));
    };
    addChildComponent (removeLfoButton);
    select (0, processor.getBandLfo (0));
}
LfoPage::~LfoPage() { processor.setTracedLfo (-1, -1); }
juce::Button& LfoPage::getTriggerButton() noexcept { return *triggerButton; }
void LfoPage::showTriggerMenu()
{
    enum { songId = 1, audioId, bandId, fullId, hitId, pauseId, thresholdBase = 100 };
    const auto s = processor.getTrigger (band);
    const auto name = TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "BAND " + juce::String (band + 1);
    juce::PopupMenu menu;
    menu.addSectionHeader ("TRIGGER  " + name + u8 (" \xc2\xb7 ALL ITS LFOS"));
    menu.addItem (songId, "SONG: follow the song (as before)", true, ! s.audio);
    menu.addItem (audioId, "AUDIO: start when sound comes in", true, s.audio);
    menu.addSectionHeader ("LISTEN TO");
    menu.addItem (bandId, TriadProcessor::isExtraChain (band) ? name + "'s own sound" : juce::String ("This band's sound"), s.audio, ! s.listenFull);
    menu.addItem (fullId, "The full sound (all bands)", s.audio, s.listenFull);
    menu.addSectionHeader ("A NEW SOUND RESTARTS THEM");
    menu.addItem (hitId, "On every hit", s.audio, ! s.afterPause);
    menu.addItem (pauseId, "After a pause (a beat of quiet)", s.audio, s.afterPause);
    juce::PopupMenu thresholds;
    for (int db = -60; db <= -6; db += 6)
        thresholds.addItem (thresholdBase - db, juce::String (db) + " dB", true, std::abs (s.thresholdDb - (float) db) < 0.5f);
    thresholds.addItem (thresholdBase + 3, "-3 dB", true, std::abs (s.thresholdDb + 3.0f) < 0.5f);
    addSubMenu (menu, "Threshold", std::move (thresholds), juce::String (juce::roundToInt (s.thresholdDb)) + " dB", s.audio);
    juce::Component::SafePointer<LfoPage> safe (this);
    const int forBand = band;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (triggerButton.get()), [safe, forBand] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto t = safe->processor.getTrigger (forBand);
        if (choice == songId) t.audio = false;
        else if (choice == audioId) t.audio = true;
        else if (choice == bandId) t.listenFull = false;
        else if (choice == fullId) t.listenFull = true;
        else if (choice == hitId) t.afterPause = false;
        else if (choice == pauseId) t.afterPause = true;
        else if (choice >= thresholdBase) t.thresholdDb = (float) (thresholdBase - choice);   // ids 103 .. 160: -3 .. -60 dB
        safe->processor.setTrigger (forBand, t);
        safe->processor.setStatus ((TriadProcessor::isExtraChain (forBand) ? TriadProcessor::chainName (forBand) : "Band " + juce::String (forBand + 1))
                                   + (t.audio ? "'s LFOs start when sound comes in (" + juce::String (juce::roundToInt (t.thresholdDb)) + " dB, "
                                                    + (t.listenFull ? "the full sound" : "its own sound") + ", " + (t.afterPause ? "after a pause" : "every hit") + ")"
                                              : juce::String ("'s LFOs follow the song")));
        safe->triggerButton->repaint();
    });
}

juce::String LfoPage::signature() const
{
    juce::String s;
    s << processor.getBandCount() << ":" << processor.getStructureVersion() << ":";
    for (int i = 0; i < processor.getLfoCount (band); ++i)
    {
        const auto l = processor.getLfoInfo (band, i);
        s << l.id << l.target << (l.active ? "+" : "-") << (processor.isEdgeLfoLocked (band, i) ? "L" : "") << ";";
    }
    s << "|" << processor.getRouteCount (band);
    for (int r : processor.getRoutesFrom (band, lfo)) s << "," << r;
    return s;
}
void LfoPage::select (int b, int l)
{
    band = TriadProcessor::isExtraChain (b) ? b : juce::jlimit (0, juce::jmax (0, processor.getBandCount() - 1), b);
    lfo = juce::jlimit (0, juce::jmax (0, processor.getLfoCount (band) - 1), l);
    host.band = band; host.lfo = lfo;
    shapes.setHost (&host);
    builtSignature.clear();
    refresh();
}
void LfoPage::startLearn (int slot)
{
    if (! processor.beginParameterLearn (band, lfo))
    {
        processor.setStatus (TriadProcessor::isExtraChain (band) ? "LEARN: " + TriadProcessor::chainName (band) + " has no effect loaded"
                                                                 : juce::String ("LEARN: this band has no effect loaded"));
        return;
    }
    // Open the effect's window to move a knob in. If it is open already it is left exactly as it is: the page
    // it is on and its settings stay (it may have taken a few clicks to get there).
    processor.showEffectIfClosed (band, slot);
    updateToolbar();
}
void LfoPage::refresh()
{
    if (processor.isLearningParameter())
    {
        if (processor.pollParameterLearn() >= 0) rebuild();
        updateToolbar();
    }
    if (! processor.isValidBand (band) || lfo >= processor.getLfoCount (band))
    {
        if (! processor.isValidBand (band)) band = juce::jmin (band, processor.getBandCount() - 1);
        lfo = juce::jlimit (0, juce::jmax (0, processor.getLfoCount (band) - 1), lfo);
        host.band = band; host.lfo = lfo;
        shapes.setHost (&host);
    }
    if (signature() != builtSignature) rebuild();
    for (auto& row : routeRows) row->update();
    updateToolbar();
    updateSlopeButton();
    updateSpectrumButtons();
    // The wave changed elsewhere (0.21.0: an end of its range dragged on the BANDS tab): shown as it is now.
    if (auto now = host.getShape(); now != seenShape)
    {
        if (now.size() >= 2 && now != shapes.getEditor().getShape()) shapes.getEditor().reload();
        seenShape = std::move (now);
    }
}
std::vector<int> LfoPage::movedDividers() const
{
    // The dividers this LFO's band edges are: the low cut is the one below the band, the high cut the one above.
    std::vector<int> out;
    if (TriadProcessor::isExtraChain (band) || lfo >= processor.getLfoCount (band)) return out;
    const int edge = processor.getLfoInfo (band, lfo).edge;
    if (edge < 0) return out;
    if (edge != D::highEdge && band > 0) out.push_back (band - 1);
    if (edge != D::lowEdge && band < processor.getBandCount() - 1) out.push_back (band);
    return out;
}
void LfoPage::updateSlopeButton()
{
    const auto dividers = movedDividers();
    if (slopeButton.isVisible() != ! dividers.empty()) slopeButton.setVisible (! dividers.empty());
    if (dividers.empty()) return;
    std::set<int> orders;
    for (int j : dividers) orders.insert (processor.getCrossoverSlope (j));
    const auto text = orders.size() == 1 ? "SLOPE " + juce::String (triad::CrossoverFilter::dbPerOctave (*orders.begin())) + " dB" : juce::String ("SLOPE MIXED");
    if (slopeButton.getButtonText() != text) slopeButton.setButtonText (text);
}
void LfoPage::showSlopeMenu()
{
    const auto dividers = movedDividers();
    if (dividers.empty()) return;
    juce::PopupMenu menu;
    menu.addSectionHeader ("HOW SOFT THE EDGES IT MOVES ARE");
    const auto cuts = processor.effectiveCrossovers();
    const int first = dividers.front();
    CrossoverDisplay::addSlopeItems (menu, processor.getCrossoverSlope (first), 0, first < (int) cuts.size() ? cuts[(size_t) first] : 0.0f,
                                     processor.getSampleRateForDisplay());
    juce::Component::SafePointer<LfoPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slopeButton), [safe, dividers] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        for (int j : dividers) safe->processor.setCrossoverSlope (j, choice);
        safe->updateSlopeButton();
        safe->getEditor().repaint();
    });
}
void LfoPage::showLfoMenu (int i)
{
    enum { onId = 1, copyId, pasteId, resetId, removeId, bandCopyId, bandPasteId, bandResetId };
    if (i < 0 || i >= processor.getLfoCount (band)) return;
    const auto info = processor.getLfoInfo (band, i);
    juce::PopupMenu menu;
    menu.addSectionHeader (info.name + juce::String::fromUTF8 ("  \xc2\xb7  ") + info.target);
    const bool routed = ! processor.getRoutesFrom (band, i).empty();
    menu.addItem (onId, routed ? juce::String ("On") : juce::String ("On (goes nowhere yet: + ROUTE TO...)"), routed, processor.isLfoOn (band, i));
    menu.addSeparator();
    menu.addItem (copyId, "Copy wave");
    menu.addItem (pasteId, "Paste wave", ShapeEditor::clipboard.size() >= 2 || juce::SystemClipboard::getTextFromClipboard().contains ("TriadLfoShape"));
    menu.addItem (resetId, "Reset wave");
    menu.addItem (removeId, "Remove LFO", ! info.bandLfo && info.slot < 0);
    if (! TriadProcessor::isExtraChain (band))
    {
        menu.addSeparator();
        menu.addSectionHeader ("BAND " + juce::String (band + 1));
        menu.addItem (bandCopyId, "Copy band state");
        menu.addItem (bandPasteId, TriadProcessor::hasCopiedBand() ? "Paste band state (" + TriadProcessor::copiedBandDescription() + ")"
                                                                   : juce::String ("Paste band state"), TriadProcessor::hasCopiedBand());
        menu.addItem (bandResetId, "Reset band state");
    }
    juce::Component::SafePointer<LfoPage> safe (this);
    const int b = band;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, b, i] (int choice)
    {
        if (safe == nullptr || choice == 0 || safe->band != b) return;
        auto& p = safe->processor;
        switch (choice)
        {
            case onId: p.setLfoOn (b, i, ! p.isLfoOn (b, i)); break;
            case copyId: safe->getEditor().copyShape(); break;
            case pasteId: safe->getEditor().pasteShape(); break;
            case resetId: p.resetLfoShape (b, i); safe->getEditor().reload(); break;
            case removeId: if (p.removeLfo (b, i)) safe->select (b, juce::jmax (0, i - 1)); break;
            case bandCopyId: p.copyBandState (b); break;
            case bandPasteId: p.pasteBandState (b); break;
            case bandResetId: p.resetBandState (b); break;
            default: break;
        }
        safe->refresh();
    });
}
void LfoPage::colourBandButtons()
{
    for (size_t i = 0; i < bandButtons.size() && i < bandButtonIndex.size(); ++i)
    {
        const int index = bandButtonIndex[i];
        bandButtons[i]->setColour (juce::TextButton::buttonColourId, index == band ? bandColour (index).withAlpha (0.85f) : slotRow);
        bandButtons[i]->setColour (juce::TextButton::textColourOffId, index == band ? inkColour : bandColour (index));
    }
}
void LfoPage::rebuild()
{
    builtSignature = signature();
    host.band = band; host.lfo = lfo;
    shapes.setHost (&host);
    bandButtons.clear();
    bandButtonIndex.clear();
    // PRE first, then the bands, then POST: each has its own LFOs, like a band.
    const int count = processor.getBandCount();
    for (int b = -1; b <= count; ++b)
    {
        const int index = b < 0 ? TriadProcessor::preBand : b == count ? TriadProcessor::postBand : b;
        auto button = std::make_unique<juce::TextButton> (TriadProcessor::isExtraChain (index) ? TriadProcessor::chainName (index)
                                                                                              : count > 8 ? "B" + juce::String (b + 1) : "BAND " + juce::String (b + 1));
        button->onClick = [this, index] { select (index, juce::jmax (0, processor.getBandLfo (index))); };
        addAndMakeVisible (*button);
        bandButtons.push_back (std::move (button));
        bandButtonIndex.push_back (index);
    }
    colourBandButtons();
    tabs.clear();
    for (int i = 0; i < processor.getLfoCount (band); ++i)
    {
        const auto info = processor.getLfoInfo (band, i);
        const auto top = info.name + (info.bandLfo ? u8 (" \xc2\xb7 BAND") : info.slot >= 0 ? u8 (" \xc2\xb7 SLOT ") + juce::String (info.slot + 1)
                                      : info.edge >= 0 ? (processor.isEdgeLfoLocked (band, i) ? u8 (" \xc2\xb7 LOCKED RANGE") : u8 (" \xc2\xb7 BAND RANGE")) : juce::String());
        auto tab = std::make_unique<LfoTab> (top, info.target, info.active, bandColour (band));
        tab->setToggleState (i == lfo, juce::dontSendNotification);
        tab->onClick = [this, i] { select (band, i); };
        juce::Component::SafePointer<LfoPage> safe (this);
        const int b = band;
        tab->onLight = [safe, b, i]
        {
            if (safe == nullptr || safe->band != b) return;
            safe->processor.setLfoOn (b, i, ! safe->processor.isLfoOn (b, i));
            safe->refresh();
        };
        tab->onMenu = [safe, b, i]
        {
            if (safe == nullptr || safe->band != b || i >= safe->processor.getLfoCount (b)) return;
            safe->select (b, i);
            safe->showLfoMenu (i);
        };
        tabHolder.addAndMakeVisible (*tab);
        tabs.push_back (std::move (tab));
    }
    routeRows.clear();
    for (int r : processor.getRoutesFrom (band, lfo))
    {
        auto row = std::make_unique<RouteRow> (processor, band, r, false, bandColour (band));
        routeHolder.addAndMakeVisible (*row);
        routeRows.push_back (std::move (row));
    }
    const auto info = processor.getLfoInfo (band, lfo);
    removeLfoButton.setVisible (! info.bandLfo && info.slot < 0);
    updateSpectrumButtons();
    updateSlopeButton();
    edgeNow->show (band, lfo);
    resized();
    repaint();
}
void LfoPage::updateToolbar()
{
    const bool learning = processor.isLearningParameter();
    addRouteButton.setButtonText (learning ? "MOVE A KNOB..." : "+ ROUTE TO...");
    learnButton.setButtonText (learning ? "STOP" : "LEARN");
    learnButton.setColour (juce::TextButton::buttonColourId, learning ? accent.withAlpha (0.35f) : addRouteButton.findColour (juce::TextButton::buttonColourId));
    learnButton.setColour (juce::TextButton::textColourOffId, accent);
    shapes.updateToolbar();
}
void LfoPage::showLearnMenu()
{
    if (processor.isLearningParameter()) { processor.cancelParameterLearn(); updateToolbar(); return; }
    std::vector<int> loaded;
    for (int s = 0; s < processor.getSlotCount (band); ++s)
        if (processor.getSlotInfo (band, s).loaded) loaded.push_back (s);
    if (loaded.empty())
    {
        processor.setStatus (TriadProcessor::isExtraChain (band) ? "LEARN: " + TriadProcessor::chainName (band) + " has no effect loaded"
                                                                 : juce::String ("LEARN: this band has no effect loaded"));
        return;
    }
    if (loaded.size() == 1) { startLearn (loaded.front()); return; }
    juce::PopupMenu menu;
    menu.addSectionHeader ("OPEN ITS WINDOW, THEN MOVE A KNOB");
    for (int s : loaded)
        addItemWithRight (menu, s + 1, juce::String (s + 1) + u8 (" \xc2\xb7 ") + processor.getSlotInfo (band, s).name,
                          processor.isEffectWindowOpen (band, s) ? "open" : "");
    juce::Component::SafePointer<LfoPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&learnButton), [safe] (int choice)
    {
        if (safe != nullptr && choice > 0) safe->startLearn (choice - 1);
    });
}
void LfoPage::resized()
{
    // 0.16.1, Thomas's layout (as Triad Time's): the bands and the tips, the LFOs and TRIGGER, the grid with ROUTING
    // beside it, one toolbar, then the shapes browser and GENERATE.
    auto r = getLocalBounds();
    auto bandRow = r.removeFromTop (32);
    for (auto& b : bandButtons) { b->setBounds (bandRow.removeFromLeft (chipWidth()).withTrimmedRight (4)); }
    hintArea = bandRow.withTrimmedLeft (12);
    r.removeFromTop (6);
    auto tabRow = r.removeFromTop (44);
    triggerButton->setBounds (tabRow.removeFromRight (routingWidth).withSizeKeepingCentre (routingWidth, 38));
    tabRow.removeFromRight (8);
    tabView.setBounds (tabRow.withTrimmedRight (40));
    addLfoButton.setBounds (juce::Rectangle<int> (tabRow.getX() + juce::jmin (tabRow.getWidth() - 34, (int) tabs.size() * 160), tabRow.getY(), 30, tabRow.getHeight())
                                .withSizeKeepingCentre (28, 28));
    int x = 0;
    for (auto& t : tabs) { t->setBounds (x, 0, 156, 38); x += 160; }
    tabHolder.setSize (juce::jmax (x, tabView.getWidth()), 38);
    r.removeFromTop (8);
    generatorArea = r.removeFromBottom (GeneratorPanel::preferredHeight);
    r.removeFromBottom (12);
    generator.setBounds (generatorArea.removeFromRight (270));
    generatorArea.removeFromRight (12);
    shapes.getBrowser().setBounds (generatorArea);
    shapes.setGridInsetRight (routingWidth);
    shapes.setBounds (r);
    routingArea = juce::Rectangle<int> (r.getRight() - routingWidth, r.getY(), routingWidth, r.getHeight() - ShapePanel::browserToolbarHeight - 8);
    // The header (ROUTING, where from), the cards, then + ROUTE TO and LEARN side by side (0.17.0: room for two cards
    // or more in the default window).
    auto panel = routingArea.reduced (10, 0).withTrimmedTop (48).withTrimmedBottom (10);
    auto buttons = panel.removeFromBottom (28);
    learnButton.setBounds (buttons.removeFromRight (juce::jmax (70, buttons.getWidth() * 2 / 5)));
    buttons.removeFromRight (6);
    addRouteButton.setBounds (buttons);
    panel.removeFromBottom (8);
    constexpr int cardHeight = 52, cardGap = 5;
    // A band-edge LFO: BAND NOW under its card, when there is room for it.
    {
        const bool edges = showsEdges() && ! TriadProcessor::isExtraChain (band);
        const int nowHeight = juce::jmin (96, panel.getHeight() - cardHeight - cardGap - 8);
        if (edges && nowHeight >= 56)
        {
            edgeNow->setBounds (panel.removeFromBottom (nowHeight));
            panel.removeFromBottom (8);
        }
        edgeNow->setVisible (edges && nowHeight >= 56);
    }
    routeView.setBounds (panel);
    const int width = panel.getWidth() - ((int) routeRows.size() * (cardHeight + cardGap) - cardGap > panel.getHeight() ? 7 : 0);
    int y = 0;
    for (auto& row : routeRows) { row->setBounds (0, y, width, cardHeight); y += cardHeight + cardGap; }
    routeHolder.setSize (width, juce::jmax (y, 1));
    removeLfoButton.setBounds (juce::Rectangle<int> (routingArea.getRight() - 96, routingArea.getY() + 10, 86, 22));
    // Over the grid's top right corner: SLOPE (band-edge LFOs), SPECTRUM and FULL SPECTRUM.
    {
        const auto grid = shapes.getBounds().withWidth (shapes.getWidth() - routingWidth - 8);
        fullSpectrumButton.setBounds (grid.getRight() - 132, grid.getY() + 3, 120, 18);
        spectrumButton.setBounds (fullSpectrumButton.getX() - 84, grid.getY() + 3, 76, 18);
        slopeButton.setBounds (spectrumButton.getX() - 104, grid.getY() + 3, 96, 18);
    }
}
void LfoPage::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (routingArea.toFloat(), 12.0f);
    const auto info = processor.getLfoInfo (band, lfo);
    auto header = routingArea.reduced (14, 10);
    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold).withKerningFactor (0.05f));
    g.drawText ("ROUTING", header.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ((TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "Band " + juce::String (band + 1)) + u8 (" \xc2\xb7 ") + info.name + u8 (" \xe2\x86\x92"),
                header.removeFromTop (18), juce::Justification::centredLeft);
    if (routeRows.empty())
    {
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Not routed yet: + ROUTE TO picks a knob, LEARN links the next one you move in an effect.",
                          routeView.getBounds().reduced (4, 0).removeFromTop (60), juce::Justification::topLeft, 4);
    }
    // How to edit, top right (two lines, as Triad Time's).
    // With many bands the room left is narrow: shorter tips, or none (the status line explains each part too).
    g.setColour (dimTextColour);
    const juce::Font tipFont (juce::FontOptions (10.5f));
    g.setFont (tipFont);
    auto tips = hintArea;
    const auto fits = [&] (const juce::String& a, const juce::String& b)
    {
        return juce::GlyphArrangement::getStringWidth (tipFont, a) <= (float) tips.getWidth() && juce::GlyphArrangement::getStringWidth (tipFont, b) <= (float) tips.getWidth();
    };
    juce::String first = u8 ("Double-click: add point \xc2\xb7 Drag: move \xc2\xb7 Double-click a point: delete"),
                 second = u8 ("Drag the small dot: curve \xc2\xb7 Drag across points: select \xc2\xb7 Right-click: more");
    if (! fits (first, second))
    {
        first = u8 ("Double-click: add / delete \xc2\xb7 Drag: move");
        second = u8 ("Drag across: select \xc2\xb7 Right-click: more");
    }
    if (fits (first, second))
    {
        g.drawText (first, tips.removeFromTop (tips.getHeight() / 2), juce::Justification::bottomRight);
        g.drawText (second, tips, juce::Justification::topRight);
    }
}
