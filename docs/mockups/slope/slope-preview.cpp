// Mock-up (design, not in the plug-in yet): an LFO on a divider's slope, from hard (96 dB/oct) to soft (6 dB/oct).
// Rendered from Triad's own code: the band menu with the new items, the slope list, the LFO tab (the real grid editor
// with a dB/oct scale) and the frequency panel showing the slope's range.
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"
#include "LfoPage.h"
#include "Look.h"
#include "ShapePanel.h"
#include "CrossoverDisplay.h"
#include "UiCommon.h"
#include <iostream>
namespace mod = triad::mod;
using namespace triad::ui;
using D = TriadProcessor::Destination;
using CF = triad::CrossoverFilter;

static void save (juce::Component& c, const juce::File& file, juce::Rectangle<int> area = {})
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (area.isEmpty() ? c.getLocalBounds() : area, true, 2.0f), stream);
    std::cout << "wrote " << file.getFullPathName() << std::endl;
}
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static double t = 0.0;
static void play (TriadProcessor& p, int blocks)
{
    juce::MidiBuffer midi;
    juce::Random r (3);
    for (int i = 0; i < blocks; ++i)
    {
        juce::AudioBuffer<float> audio (2, 512);
        for (int s = 0; s < 512; ++s, t += 1.0 / 48000.0)
        {
            const double beat = std::fmod (t * 2.0, 1.0);
            float v = 0.18f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * t)
                    + 0.05f * (float) (std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) + std::sin (juce::MathConstants<double>::twoPi * 277.2 * t)
                                       + std::sin (juce::MathConstants<double>::twoPi * 329.6 * t) + std::sin (juce::MathConstants<double>::twoPi * 880.0 * t)
                                       + std::sin (juce::MathConstants<double>::twoPi * 3520.0 * t))
                    + (beat < 0.08 ? 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (60.0 + 120.0 * (0.08 - beat)) * t) : 0.0f)
                    + (std::fmod (beat, 0.5) < 0.03 ? 0.12f * (r.nextFloat() - 0.5f) : 0.02f * (r.nextFloat() - 0.5f));
            audio.setSample (0, s, v);
            audio.setSample (1, s, v);
        }
        p.processBlock (audio, midi);
    }
}

static float vOf (int order) { return (float) (order - CF::minOrder) / (float) (CF::maxOrder - CF::minOrder); }
static int orderAt (float v) { return CF::minOrder + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) (CF::maxOrder - CF::minOrder)); }
static juce::String dbText (int order) { return juce::String (CF::dbPerOctave (order)) + " dB/oct"; }
static constexpr float fc = 2500.0f;
static constexpr int softest = 2, hardest = 16;   // the LFO's range in the mock-up: 12 to 96 dB/oct

// A little picture of a slope: the real response of a high-pass of that order, two octaves either side of its cutoff.
static void drawSlopeGlyph (juce::Graphics& g, juce::Rectangle<float> r, int order, juce::Colour colour)
{
    juce::Path p;
    for (int i = 0; i <= 40; ++i)
    {
        const float x = (float) i / 40.0f;
        const float f = 1000.0f * std::exp2 ((x - 0.5f) * 4.0f);
        const float db = juce::jlimit (-30.0f, 0.0f, juce::Decibels::gainToDecibels (CF::highMagnitude (order, f, 1000.0f, 48000.0), -100.0f));
        const auto pt = juce::Point<float> (r.getX() + r.getWidth() * x, r.getY() + r.getHeight() * (-db / 30.0f));
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

// The grid's host for a slope LFO: a dB/oct scale, each point's slope, "harder +36 dB" on the lines, and behind it the
// three kinds of slope the list already names (soft blend, classic, near brickwall).
struct SlopeHost final : ShapeHost
{
    TriadProcessor& processor;
    int band = 1, lfo = 0;
    mod::Shape shape;
    juce::Colour colour;
    float phase = 0.6f;
    explicit SlopeHost (TriadProcessor& p) : processor (p) {}
    mod::Shape getShape() const override { return shape; }
    void setShape (const mod::Shape& s, bool) override { shape = s; }
    int getLength() const override { return processor.getLfoInfo (band, lfo).length; }
    void setLength (int) override {}
    float getPhase() const override { return phase; }
    juce::Colour getColour() const override { return colour; }
    std::vector<std::pair<float, juce::String>> valueMarks() const override
    {
        std::vector<std::pair<float, juce::String>> marks;
        for (int o = CF::minOrder; o <= CF::maxOrder; ++o)
        {
            const bool labelled = o == 1 || o == 2 || o == 4 || o == 6 || o == 8 || o == 12 || o == 16;
            marks.push_back ({ vOf (o), labelled ? juce::String (CF::dbPerOctave (o)) : juce::String() });
        }
        return marks;
    }
    juce::String pointLabel (const mod::Point& p) const override { return dbText (orderAt (p.y)); }
    juce::String segmentLabel (const mod::Point& from, const mod::Point& to) const override
    {
        const int d = CF::dbPerOctave (orderAt (to.y)) - CF::dbPerOctave (orderAt (from.y));
        if (d == 0) return {};
        return d > 0 ? "harder +" + juce::String (d) + " dB" : juce::String::fromUTF8 ("softer \xe2\x88\x92") + juce::String (-d) + " dB";
    }
    int segmentLabelMode() const override { return 2; }
    bool fillsUnderLine() const override { return false; }
    bool hasBackdrop() const override { return true; }
    void paintBackdrop (juce::Graphics& g, juce::Rectangle<float> plot) override
    {
        const float half = 0.5f / (float) (CF::maxOrder - CF::minOrder);
        const auto yOf = [&] (float v) { return juce::jlimit (plot.getY(), plot.getBottom(), plot.getBottom() - plot.getHeight() * v); };
        struct Zone { int lo, hi; const char* name; int glyph; float alpha; };
        const Zone zones[] { { 1, 3, "SOFT BLEND", 2, 0.0f }, { 4, CF::steepestClassic, "CLASSIC", 6, 0.035f }, { CF::steepestClassic + 1, 16, "NEAR BRICKWALL", 16, 0.075f } };
        for (const auto& z : zones)
        {
            const float top = yOf (vOf (z.hi) + half), bottom = yOf (vOf (z.lo) - half);
            g.setColour (colour.withAlpha (z.alpha));
            g.fillRect (juce::Rectangle<float> (plot.getX(), top, plot.getWidth(), bottom - top));
            if (z.lo > 1)
            {
                const float dash[] { 3.0f, 4.0f };
                g.setColour (colour.withAlpha (0.35f));
                g.drawDashedLine ({ plot.getX(), bottom, plot.getRight(), bottom }, dash, 2, 1.0f);
            }
            // Its name and a picture of its slope, bottom right of the zone.
            const float left = plot.getX() + plot.getWidth() * 0.29f;
            drawSlopeGlyph (g, { left, bottom - 19.0f, 26.0f, 13.0f }, z.glyph, colour.withAlpha (0.8f));
            g.setColour (colour.withAlpha (0.75f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (z.name, juce::Rectangle<float> (left + 34.0f, bottom - 20.0f, 160.0f, 16.0f), juce::Justification::centredLeft);
        }
    }
};

// SLOPE NOW, in the ROUTING card's free space: the divider's two bands at the slope playing now (filled), and as dashed
// lines where the LFO takes it (its softest and its hardest).
struct SlopeNow final : juce::Component
{
    int now = 8;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (currentPalette.controls.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 6.0f);
        r.reduce (10.0f, 7.0f);
        auto head = r.removeFromTop (16.0f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (dimTextColour);
        g.drawText (juce::String::fromUTF8 ("SLOPE NOW  \xc2\xb7  2.5 kHz"), head, juce::Justification::centredLeft);
        g.setColour (bandColour (1));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (dbText (now), head, juce::Justification::centredRight);
        const auto plot = r.withTrimmedTop (4.0f);
        g.setColour (currentPalette.gridLines.withAlpha (0.12f));
        for (int k = -3; k <= 3; ++k) { const float x = plot.getX() + plot.getWidth() * (0.5f + (float) k / 6.0f); g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom()); }
        const auto curve = [&] (int order, bool high)
        {
            juce::Path p;
            for (int i = 0; i <= 120; ++i)
            {
                const float x = (float) i / 120.0f;
                const float f = fc * std::exp2 ((x - 0.5f) * 6.0f);
                const float m = high ? CF::highMagnitude (order, f, fc, 48000.0) : CF::lowMagnitude (order, f, fc, 48000.0);
                const float db = juce::jlimit (-36.0f, 0.0f, juce::Decibels::gainToDecibels (m, -100.0f));
                const auto pt = juce::Point<float> (plot.getX() + plot.getWidth() * x, plot.getY() + plot.getHeight() * (-db / 36.0f));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            return p;
        };
        for (int b : { 0, 1 })
        {
            const auto colour = bandColour (1 + b);
            auto line = curve (now, b == 1);
            juce::Path fill (line);
            fill.lineTo (plot.getRight(), plot.getBottom()); fill.lineTo (plot.getX(), plot.getBottom()); fill.closeSubPath();
            g.setColour (colour.withAlpha (0.22f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (line, juce::PathStrokeType (1.6f));
            for (int o : { softest, hardest })
            {
                juce::Path dashed;
                const float dash[] { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, curve (o, b == 1), dash, 2);
                g.setColour (colour.withAlpha (0.6f));
                g.fillPath (dashed);
            }
        }
        g.setColour (currentPalette.text.withAlpha (0.5f));
        g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());
    }
};

// The LFO tab's label, as the real tab draws it, with the slope's words.
struct TabPatch final : juce::Component
{
    juce::String line1, line2;
    juce::Colour colour;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colour.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colour); g.fillRoundedRectangle (r.removeFromBottom (3.0f), 1.5f);
        auto area = getLocalBounds().reduced (10, 4).withTrimmedRight (16);
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (line1, area.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (line2, area, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (ledColour);
        g.fillEllipse (juce::Rectangle<float> ((float) getWidth() - 16.0f, 7.0f, 8.0f, 8.0f));
    }
};

// Over the frequency panel: the slope dot's range on its divider (soft at the top, hard at the bottom, as the dot moves)
// and the softest and hardest curves the LFO reaches, dashed.
struct SlopeRange final : juce::Component
{
    CrossoverDisplay& display;
    int now = 8;
    explicit SlopeRange (CrossoverDisplay& d) : display (d) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        const auto plot = display.plotBounds();
        const float x = display.xForFrequency (fc);
        const float top = plot.getY() + 30.0f, bottom = plot.getBottom() - 3.0f;
        constexpr float rangeDb = 30.0f;
        const auto curve = [&] (int order, bool high)
        {
            juce::Path p;
            bool started = false;
            for (float px = plot.getX(); px <= plot.getRight(); px += 2.0f)
            {
                const float f = display.frequencyForX (px);
                if (high ? (f < fc / 16.0f || f > fc * 1.5f) : (f < fc / 1.5f || f > fc * 16.0f)) { started = false; continue; }
                const float m = high ? CF::highMagnitude (order, f, fc, 48000.0) : CF::lowMagnitude (order, f, fc, 48000.0)
                                       * CF::highMagnitude (4, f, 250.0f, 48000.0);
                const float db = juce::jlimit (-rangeDb, 0.0f, juce::Decibels::gainToDecibels (m, -100.0f));
                if (db <= -rangeDb + 0.01f) { started = false; continue; }
                const auto pt = juce::Point<float> (px, top + (bottom - top) * (-db / rangeDb));
                if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
            }
            return p;
        };
        for (int o : { softest, hardest })
            for (int b : { 0, 1 })
            {
                juce::Path dashed;
                const float dash[] { 4.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, curve (o, b == 1), dash, 2);
                g.setColour (bandColour (1 + b).withAlpha (0.7f));
                g.fillPath (dashed);
            }
        // The labels on the curves, where they cross 18 dB down.
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int o : { softest, hardest })
        {
            float hz = fc;
            for (float f = fc; f > 200.0f; f *= 0.99f)
                if (juce::Decibels::gainToDecibels (CF::highMagnitude (o, f, fc, 48000.0), -100.0f) < -18.0f) { hz = f; break; }
            const auto pt = juce::Point<float> (display.xForFrequency (hz), top + (bottom - top) * 0.6f);
            const auto text = juce::String (CF::dbPerOctave (o)) + " dB";
            const auto box = juce::Rectangle<float> (44.0f, 15.0f).withCentre (pt.translated (o == softest ? -26.0f : 24.0f, 0.0f));
            g.setColour (currentPalette.background.withAlpha (0.85f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (bandColour (2));
            g.drawText (text, box, juce::Justification::centred);
        }
        // The dot's track on the divider: soft (top) to hard (bottom), in the colour of the band whose LFO moves it.
        const float y0 = display.slopeY (softest), y1 = display.slopeY (hardest);
        const auto colour = bandColour (1);
        g.setColour (colour.withAlpha (0.9f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 1.5f, y0, 3.0f, y1 - y0), 1.5f);
        for (float y : { y0, y1 }) g.fillRoundedRectangle (juce::Rectangle<float> (x - 6.0f, y - 1.0f, 12.0f, 2.0f), 1.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("SOFT " + juce::String (CF::dbPerOctave (softest)), juce::Rectangle<float> (x - 78.0f, y0 - 8.0f, 70.0f, 16.0f), juce::Justification::centredRight);
        g.drawText ("HARD " + juce::String (CF::dbPerOctave (hardest)), juce::Rectangle<float> (x - 78.0f, y1 - 8.0f, 70.0f, 16.0f), juce::Justification::centredRight);
    }
};

template <typename T> static T* find (juce::Component& root, const std::function<bool (T&)>& match)
{
    for (auto* child : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (child); t != nullptr && match (*t)) return t;
        if (auto* deeper = find<T> (*child, match)) return deeper;
    }
    return nullptr;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File folder (argc > 1 ? argv[1] : "/tmp");
    applyTheme (themeNamed ("Plum"), true);
    TriadProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.addSlot (1);
    std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
    editor->setVisible (true);
    pump (300);
    auto host = makeCrossoverHost (p);
    auto& display = editor->getDisplay();
    menusInsideWindow = true;
    // 1. The band menu: under the four range moves, the two slopes.
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (juce::String::fromUTF8 ("Band 2  \xc2\xb7  250 Hz \xe2\x80\x93 2.5 kHz"));
        CrossoverDisplay::addLinkItems (menu, *host, 1, 200);
        menu.addSectionHeader (menuLfo (juce::String::fromUTF8 ("AN LFO ON A SLOPE  \xc2\xb7  HARD \xe2\x86\x94 SOFT")));
        addItemWithRight (menu, 210, menuFilter ('k', "Low cut slope"), "now " + dbText (p.getCrossoverSlope (0)));
        addItemWithRight (menu, 211, menuFilter ('j', "High cut slope"), "now " + dbText (p.getCrossoverSlope (1)));
        menu.addSeparator();
        menu.addItem (1, "Remove band 2");
        menu.addItem (2, "Add a band here (300 Hz)");
        menu.addSeparator();
        CrossoverDisplay::addAllDividerItems (menu, CrossoverDisplay::allDividersBase);
        const auto at = editor->getLocalPoint (&display, juce::Point<int> ((int) display.xForFrequency (300.0f), (int) display.plotBounds().getY() + 30));
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (editor->localPointToGlobal (at).toFloat());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&display).withMousePosition());
        save (*editor, folder.getChildFile ("slope-1-band-menu.png"));
        juce::PopupMenu::dismissAllActiveMenus();
        pump (200);
    }
    // 2. The slope list (click a divider's "24 dB/oct"): Link to LFO first.
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("CROSSOVER 2 SLOPE");
        addItemWithRight (menu, 50, menuLfo (juce::String::fromUTF8 ("Link to LFO: hard \xe2\x86\x94 soft")), "band 2's high cut");
        menu.addSeparator();
        CrossoverDisplay::addSlopeItems (menu, p.getCrossoverSlope (1), 0, fc, 48000.0);
        CrossoverDisplay::addAllDividerItems (menu, CrossoverDisplay::allDividersBase);
        const auto readout = display.slopeReadoutBounds (1);
        const auto at = editor->getLocalPoint (&display, readout.getCentre().toInt());
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (editor->localPointToGlobal (at).toFloat());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&display).withMousePosition());
        save (*editor, folder.getChildFile ("slope-2-slope-list.png"));
        juce::PopupMenu::dismissAllActiveMenus();
        pump (200);
    }
    menusInsideWindow = false;
    // The LFO: soft, a quick rise to the hardest, held, then easing back down through the classic slopes.
    const mod::Shape shape { { 0.0f, vOf (2), 0.0f }, { 0.22f, vOf (16), -2.0f }, { 0.5f, vOf (16), 0.0f }, { 0.78f, vOf (6), 0.0f }, { 1.0f, vOf (2), 0.0f } };
    SlopeHost slopeHost (p);
    slopeHost.shape = shape;
    slopeHost.colour = bandColour (1);
    slopeHost.phase = 0.67f;
    const int now = orderAt (mod::evaluate (shape, slopeHost.phase));
    p.setCrossoverSlope (1, now);
    const int lfo = p.linkEdgesToLfo (1, D::highEdge);   // stands in for the slope LFO (flat: nothing moves)
    slopeHost.lfo = lfo;
    p.setStatus ("Linked crossover 2's slope (2.5 kHz) to band 2's LFO " + juce::String (lfo + 1));
    // 3. The LFO tab.
    {
        editor->setTab (TriadEditor::Tab::lfo);
        auto& page = editor->getLfoPage();
        page.select (1, lfo);
        pump (300);
        page.spectrumButton.setVisible (false);
        auto& real = page.getEditor();
        ShapeEditor overlay;
        real.getParentComponent()->addAndMakeVisible (overlay);
        overlay.setBounds (real.getBounds());
        overlay.setHost (&slopeHost);
        overlay.reload();
        TabPatch tab;
        if (auto* button = find<juce::Button> (*editor, [] (juce::Button& b) { return dynamic_cast<juce::TextButton*> (&b) == nullptr && b.getButtonText().startsWith ("High cut"); }))
        {
            tab.line1 = juce::String::fromUTF8 ("LFO ") + juce::String (lfo + 1) + juce::String::fromUTF8 (" \xc2\xb7 SLOPE");
            tab.line2 = "High cut slope 2.5 kHz";
            tab.colour = bandColour (1);
            button->getParentComponent()->addAndMakeVisible (tab);
            tab.setBounds (button->getBounds());
            button->setVisible (false);
        }
        else std::cout << "no tab found" << std::endl;
        if (auto* destination = find<juce::TextButton> (*editor, [] (juce::TextButton& b) { return b.getButtonText().startsWith ("High cut"); }))
            destination->setButtonText ("High cut slope 2.5 kHz");
        else std::cout << "no destination found" << std::endl;
        SlopeNow inset;
        inset.now = now;
        if (auto* row = find<RouteRow> (*editor, [] (RouteRow&) { return true; }))
        {
            const auto rowArea = editor->getLocalArea (row, row->getLocalBounds());
            const auto add = editor->getLocalArea (&page.getAddRouteButton(), page.getAddRouteButton().getLocalBounds());
            editor->addAndMakeVisible (inset);
            inset.setBounds (rowArea.getX(), rowArea.getBottom() + 6, rowArea.getWidth(), add.getY() - rowArea.getBottom() - 12);
        }
        else std::cout << "no route row found" << std::endl;
        play (p, 20);
        pump (200);
        overlay.frame (juce::Time::getMillisecondCounterHiRes() / 1000.0);
        pump (100);
        save (*editor, folder.getChildFile ("slope-3-lfo-tab.png"));
        editor->removeChildComponent (&inset);
    }
    // 4. The frequency panel: the dot's track and the softest and hardest curves.
    {
        editor->setTab (TriadEditor::Tab::bands);
        pump (200);
        SlopeRange range (display);
        range.now = now;
        display.addAndMakeVisible (range);
        range.setBounds (display.getLocalBounds());
        editor->createComponentSnapshot (editor->getLocalBounds());
        play (p, 60);
        pump (200);
        display.updateSpectrum (0.05);
        display.refreshIfChanged();
        save (*editor, folder.getChildFile ("slope-4-bands.png"));
        display.removeChildComponent (&range);
    }
    return 0;
}
