#include "LfoPage.h"

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
        setTooltip (line1 + ": " + line2 + (active ? " (on)" : " (off until you draw on it)"));
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
};

}

// ---- Behind the grid: the sound coming in --------------------------------------------------------------------------
bool LfoShapeHost::hasBackdrop() const { return page.hasBackdrop(); }
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
    }
    return backdropMode;
}
void LfoPage::visibilityChanged()
{
    if (! isVisible()) processor.setTracedLfo (-1, -1);   // nothing traced while the tab is closed
}
void LfoPage::paintBackdrop (juce::Graphics& g, juce::Rectangle<float> plot)
{
    if (backdrop == nullptr) backdrop = std::make_unique<Backdrop>();
    const auto colour = bandColour (band);
    if (getBackdrop() == TriadSettings::Backdrop::waveform)
    {
        processor.setTracedLfo (band, lfo);
        processor.readLfoTrace (backdrop->level, backdrop->age);
        const auto trigger = processor.getTrigger (band);
        triad::ui::drawWaveTrace (g, plot, backdrop->level, backdrop->age, colour, trigger.audio ? trigger.thresholdDb : std::numeric_limits<float>::quiet_NaN());
        return;
    }
    processor.setTracedLfo (-1, -1);
    // The spectrum (as the BANDS tab's): the band's range lit in its colour, the rest faint.
    float low = 20.0f, high = 20000.0f;
    if (! TriadProcessor::isExtraChain (band))
    {
        const auto cuts = processor.effectiveCrossovers();
        if (band > 0 && band - 1 < (int) cuts.size()) low = cuts[(size_t) band - 1];
        if (band < (int) cuts.size()) high = cuts[(size_t) band];
    }
    backdrop->spectrum.paint (g, plot, processor.getSpectrumTap(), processor.getSampleRateForDisplay(), low, high, colour);
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
        s << l.id << l.target << (l.active ? "+" : "-") << ";";
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
        const auto top = info.name + (info.bandLfo ? u8 (" \xc2\xb7 BAND") : info.slot >= 0 ? u8 (" \xc2\xb7 SLOT ") + juce::String (info.slot + 1) : juce::String());
        auto tab = std::make_unique<LfoTab> (top, info.target, info.active, bandColour (band));
        tab->setToggleState (i == lfo, juce::dontSendNotification);
        tab->onClick = [this, i] { select (band, i); };
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
    resized();
    repaint();
}
void LfoPage::updateToolbar()
{
    const bool learning = processor.isLearningParameter();
    addRouteButton.setButtonText (learning ? "LEARNING... (CLICK TO STOP)" : "+ ROUTE TO...");
    learnButton.setButtonText (learning ? "MOVE A KNOB... (STOP)" : "LEARN");
    learnButton.setColour (juce::TextButton::buttonColourId, learning ? accent.withAlpha (0.35f) : currentPalette.panel);
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
    auto panel = routingArea.reduced (10, 0).withTrimmedTop (50).withTrimmedBottom (10);
    learnButton.setBounds (panel.removeFromBottom (32));
    panel.removeFromBottom (6);
    addRouteButton.setBounds (panel.removeFromBottom (32));
    panel.removeFromBottom (8);
    routeView.setBounds (panel);
    const int width = panel.getWidth() - (routeRows.size() * 70 > (size_t) panel.getHeight() ? 7 : 0);
    int y = 0;
    for (auto& row : routeRows) { row->setBounds (0, y, width, 64); y += 70; }
    routeHolder.setSize (width, juce::jmax (y, 1));
    removeLfoButton.setBounds (juce::Rectangle<int> (routingArea.getRight() - 96, routingArea.getY() + 10, 86, 22));
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
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (10.5f));
    auto tips = hintArea;
    g.drawText (u8 ("Double-click: add point \xc2\xb7 Drag: move \xc2\xb7 Double-click a point: delete"), tips.removeFromTop (tips.getHeight() / 2),
                juce::Justification::bottomRight);
    g.drawText (u8 ("Drag the small dot: curve \xc2\xb7 Drag across points: select \xc2\xb7 Right-click: more"), tips, juce::Justification::topRight);
}
