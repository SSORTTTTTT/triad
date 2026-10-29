#include "PluginEditor.h"
#include "TriadTime.h"

using namespace triad::ui;

namespace
{
constexpr int margin = 24, headerHeight = 52, stripGap = 10, minStripWidth = 196, maxStripWidth = 420;
// 0.18.0 (Thomas): the frequency panel 1.5 times as tall (was 210); the band footers take half the room they did.
constexpr int displayHeight = 315;
enum MenuIds { scanningId = 1000001, noneId, browseId, refreshId, rescanId, emptySlotId, searchId };
juce::Array<juce::File> filesIn (const juce::File& folder, const juce::String& pattern)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, pattern);
    files.sort();
    return files;
}
}

// ---- Triad's bands, for the frequency panel -------------------------------------------------------
namespace
{
class TriadCrossoverHost final : public CrossoverHost
{
public:
    explicit TriadCrossoverHost (TriadProcessor& p) : processor (p) {}
    juce::String getProductName() const override { return "Triad"; }
    int getBandCount() const override { return processor.getBandCount(); }
    std::vector<float> effectiveCrossovers() const override { return processor.effectiveCrossovers(); }   // as set (0.19.0)
    std::vector<float> heardCrossovers() const override { return processor.heardCrossovers(); }   // with their LFOs
    int getCrossoverSlope (int index) const override { return processor.getCrossoverSlope (index); }
    juce::RangedAudioParameter* crossoverParameter (int index) const override { return processor.crossoverParameter (index); }
    juce::RangedAudioParameter* slopeParameter (int index) const override { return processor.slopeParameter (index); }
    double getSampleRateForDisplay() const override { return processor.getSampleRateForDisplay(); }
    void setAllCrossoverSlopes (int order) override { processor.setAllCrossoverSlopes (order); }
    bool addBand (float hz) override { return processor.addBand (hz); }
    bool removeBand (int band) override { return processor.removeBand (band); }
    triad::SpectrumTap& getSpectrum() override { return processor.spectrum; }
    bool hasChains() const override { return true; }
    bool hasLiveMode() const override { return true; }
    bool isLiveMode() const override { return processor.isLiveMode(); }
    void setLiveMode (bool on) override { processor.setLiveMode (on); }
    bool hasBandState() const override { return true; }
    void copyBandState (int band) override { processor.copyBandState (band); }
    void pasteBandState (int band) override { processor.pasteBandState (band); }
    bool canPasteBandState() const override { return TriadProcessor::hasCopiedBand(); }
    juce::String pasteBandDescription() const override { return TriadProcessor::copiedBandDescription(); }
    bool canLinkToLfo() const override { return true; }
    bool isBandHeard (int band) const override
    {
        return ! processor.isBandMuted (band) && (! processor.isAnyBandSoloed() || processor.isBandSoloed (band));
    }
    std::pair<float, float> crossoverSweep (int index) const override { return processor.crossoverSweep (index); }
    int edgeLfo (int band, int edge) const override { return processor.findEdgeLfo (band, edge); }
private:
    TriadProcessor& processor;
};
}
std::unique_ptr<CrossoverHost> makeCrossoverHost (TriadProcessor& p) { return std::make_unique<TriadCrossoverHost> (p); }

// ---- Name prompt ----------------------------------------------------------------------------------
NamePrompt::NamePrompt()
{
    setInterceptsMouseClicks (true, true);
    text.setFont (juce::FontOptions (15.0f));
    text.setJustification (juce::Justification::centredLeft);
    text.onReturnKey = [this] { finish (true); };
    text.onEscapeKey = [this] { finish (false); };
    addAndMakeVisible (text);
    saveButton.onClick = [this] { finish (true); };
    cancelButton.onClick = [this] { finish (false); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (cancelButton);
}
void NamePrompt::show (const juce::String& t, const juce::String& initial, std::function<void (const juce::String&)> onSave)
{
    title = t;
    callback = std::move (onSave);
    text.setText (initial, false);
    text.selectAll();
    setVisible (true);
    toFront (true);
    if (isShowing()) text.grabKeyboardFocus();
    repaint();
}
void NamePrompt::finish (bool save)
{
    const auto name = juce::File::createLegalFileName (text.getText().trim());
    setVisible (false);
    if (save && name.isNotEmpty() && callback) callback (name);
}
void NamePrompt::paint (juce::Graphics& g)
{
    // In the menus' look (0.16.0): the title in the accent colour, a dark field, SAVE filled.
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto m = triad::ui::menuColours();
    const auto box = getLocalBounds().withSizeKeepingCentre (400, 154).toFloat();
    g.setColour (m.back); g.fillRoundedRectangle (box, 12.0f);
    g.setColour (m.edge); g.drawRoundedRectangle (box.reduced (0.5f), 12.0f, 1.0f);
    g.setColour (m.header);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.06f));
    g.drawText (title.toUpperCase(), box.reduced (20.0f, 16.0f).removeFromTop (18.0f), juce::Justification::centredLeft);
}
void NamePrompt::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (400, 154).reduced (20, 16);
    box.removeFromTop (28);
    text.setBounds (box.removeFromTop (36));
    box.removeFromTop (16);
    auto buttons = box.removeFromTop (32);
    saveButton.setBounds (buttons.removeFromRight (92));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (92));
}
void NamePrompt::lookAndFeelChanged()
{
    const auto m = triad::ui::menuColours();
    saveButton.setColour (juce::TextButton::buttonColourId, m.chosen);
    saveButton.setColour (juce::TextButton::textColourOffId, m.chosenInk);
    text.setColour (juce::TextEditor::focusedOutlineColourId, m.chosen);
    text.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    text.setIndents (10, 0);
}

// ---- Header buttons ---------------------------------------------------------------------------------
void TriadEditor::TabButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    g.setColour (on ? currentPalette.tabOn : over ? currentPalette.tabOver : juce::Colours::transparentBlack);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f);
    g.setColour (on ? accent : over ? textColour : dimTextColour);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (getName(), getLocalBounds(), juce::Justification::centred);
    if (on) { g.setColour (accent); g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromBottom (3.0f).reduced (10.0f, 0.0f), 1.5f); }
}
void TriadEditor::PresetButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (over ? currentPalette.paletteButton.interpolatedWith (currentPalette.text, 0.02f) : currentPalette.paletteButton);
    g.fillRoundedRectangle (r, 5.0f);
    auto area = getLocalBounds().reduced (10, 0);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("PRESET", area.removeFromLeft (52), juce::Justification::centredLeft);
    juce::Path arrow;
    const float ax = (float) getWidth() - 18.0f, ay = (float) getHeight() * 0.5f;
    arrow.addTriangle (ax - 5.0f, ay - 2.5f, ax + 5.0f, ay - 2.5f, ax, ay + 3.5f);
    g.fillPath (arrow);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (presetName.isNotEmpty() ? presetName : "Init", area.withTrimmedRight (20), juce::Justification::centredLeft, 1, 0.8f);
}

// ---- Editor -----------------------------------------------------------------------------------------
TriadEditor::TriadEditor (TriadProcessor& p)
    : AudioProcessorEditor (p), processor (p), frequencyDisplay (makeCrossoverHost (p)), lfoPage (p), matrixPage (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    for (int i = 0; i < 3; ++i)
    {
        tabButtons[(size_t) i]->onClick = [this, i] { setTab ((Tab) i); };
        content.addAndMakeVisible (*tabButtons[(size_t) i]);
    }
    tabButtons[0]->setTooltip ("Bands: crossovers, effects, mixes and volumes");
    tabButtons[1]->setTooltip ("LFO: draw how each mix knob moves over time");
    tabButtons[2]->setTooltip ("Matrix: which LFO moves which knob, and how much");
    presetButton.onClick = [this] { showPresetMenu(); };
    content.addAndMakeVisible (presetButton);
    gearButton.onClick = [this] { showGearMenu(); };
    content.addAndMakeVisible (gearButton);
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    content.addAndMakeVisible (undoButton);
    content.addAndMakeVisible (redoButton);
    content.addAndMakeVisible (bandsPage);
    bandsPage.addAndMakeVisible (frequencyDisplay);
    frequencyDisplay.onEditBandLfo = [this] (int band) { showLfo (band, -1); };
    frequencyDisplay.onLinkEdges = [this] (int band, int edge) { linkEdgesToLfo (band, edge); };
    frequencyDisplay.onTogglePre = [this] { togglePre(); };
    frequencyDisplay.onTogglePost = [this] { togglePost(); };
    lfoPage.askName = [this] (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
    {
        prompt.show (title, initial, std::move (done));
    };
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    bandsPage.addAndMakeVisible (viewport);
    content.addChildComponent (lfoPage);
    content.addChildComponent (matrixPage);
    matrixPage.onEditLfo = [this] (int band, int lfo) { setTab (Tab::lfo); lfoPage.select (band, lfo); };
    status.setFont (juce::FontOptions (12.0f));
    content.addAndMakeVisible (status);
    content.addChildComponent (prompt);
    content.addChildComponent (themePanel);
    content.addChildComponent (pluginSearch);
    catalog->addChangeListener (this);
    rebuildStrips();
    setTab (Tab::bands);
    setScale (settings->getScale());
    CrossoverDisplay::ghostMovement = settings->getGhostMovement();
    timerCallback();
    startTimerHz (15);   // labels, status, structure; the meters and dots move with the screen (animate)
}
TriadEditor::~TriadEditor()
{
    stopTimer();
    catalog->removeChangeListener (this);
    processor.closeWindows();
    setLookAndFeel (nullptr);
}
void TriadEditor::setScale (double s)
{
    scale = juce::jlimit (0.75, 1.5, s);
    content.setTransform (juce::AffineTransform::scale ((float) scale));
    setSize (juce::roundToInt (logicalWidth * scale), juce::roundToInt (logicalHeight * scale));
}
void TriadEditor::paint (juce::Graphics& g) { g.fillAll (background); }
void TriadEditor::themeChanged()
{
    // A new theme (chosen here, in another Triad, Triad Time or Triad Split): re-colour and redraw everything.
    look.applyTheme();
    sendLookAndFeelChange();
    repaint();
}
void TriadEditor::resized()
{
    content.setBounds (0, 0, logicalWidth, logicalHeight);
    layoutContent();
}
void TriadEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (headerColour); g.fillRect (0, 0, logicalWidth, headerHeight);
    g.setColour (accent); g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("TRIAD", margin, 7, 110, 38, juce::Justification::centredLeft);
    g.setColour (currentPalette.softText); g.setFont (juce::FontOptions (11.5f));
    g.drawText (juce::String ("VST3 + AU  /  ") + juce::String (TRIAD_PRODUCT_NAME).fromLastOccurrenceOf (" ", false, false).toUpperCase() + "  " + TRIAD_VERSION, logicalWidth - margin - 170, 13, 170, 26, juce::Justification::centredRight);
}
void TriadEditor::layoutContent()
{
    int x = 140;
    for (auto* t : tabButtons) { t->setBounds (x, 11, 84, 30); x += 88; }
    presetButton.setBounds (420, 11, 260, 30);
    gearButton.setBounds (logicalWidth - margin - 170 - 40, 12, 30, 28);
    redoButton.setBounds (gearButton.getX() - 38, 12, 30, 28);
    undoButton.setBounds (redoButton.getX() - 34, 12, 30, 28);
    const auto page = juce::Rectangle<int> (margin, headerHeight + 8, logicalWidth - 2 * margin, logicalHeight - headerHeight - 8 - 30);
    bandsPage.setBounds (page);
    lfoPage.setBounds (page);
    matrixPage.setBounds (page);
    frequencyDisplay.setBounds (0, 0, page.getWidth(), displayHeight);
    viewport.setBounds (0, displayHeight + 8, page.getWidth(), page.getHeight() - displayHeight - 8);
    status.setBounds (margin, logicalHeight - 28, logicalWidth - 2 * margin, 24);
    prompt.setBounds (0, 0, logicalWidth, logicalHeight);
    themePanel.setBounds (0, 0, logicalWidth, logicalHeight);
    pluginSearch.setBounds (0, 0, logicalWidth, logicalHeight);
    layoutStrips();
}
void TriadEditor::setTab (Tab t)
{
    tab = t;
    for (int i = 0; i < 3; ++i) tabButtons[(size_t) i]->setToggleState ((int) t == i, juce::dontSendNotification);
    bandsPage.setVisible (t == Tab::bands);
    lfoPage.setVisible (t == Tab::lfo);
    matrixPage.setVisible (t == Tab::matrix);
    if (t == Tab::lfo) lfoPage.refresh();
    if (t == Tab::matrix) matrixPage.refresh();
}
void TriadEditor::rebuildStrips()
{
    strips.clear();
    builtCount = processor.getBandCount();
    for (int b = 0; b < builtCount; ++b)
    {
        strips.push_back (std::make_unique<BandStrip> (processor, *this, b));
        stripHolder.addAndMakeVisible (*strips.back());
    }
    if (preStrip == nullptr)
    {
        preStrip = std::make_unique<BandStrip> (processor, *this, TriadProcessor::preBand);
        stripHolder.addChildComponent (*preStrip);
        postStrip = std::make_unique<BandStrip> (processor, *this, TriadProcessor::postBand);
        stripHolder.addChildComponent (*postStrip);
    }
    updateChains();
    layoutStrips();
    frequencyDisplay.refresh();
}
void TriadEditor::toggleChain (int band)
{
    const bool pre = band == TriadProcessor::preBand;
    if ((pre ? isPreShown() : isPostShown()) && processor.hasChainEffects (band))
    {
        processor.setStatus (TriadProcessor::chainName (band) + " has effects, so it stays shown (remove them, or use its ... menu, to hide it)");
        return;
    }
    (pre ? preOpened : postOpened) = ! (pre ? isPreShown() : isPostShown());
    updateChains();
    layoutStrips();
}
void TriadEditor::updateChains()
{
    bool changed = false;
    for (int band : { TriadProcessor::preBand, TriadProcessor::postBand })
    {
        int effects = 0;
        for (int s = 0; s < processor.getSlotCount (band); ++s) effects += processor.getSlotInfo (band, s).hasContent ? 1 : 0;
        const bool pre = band == TriadProcessor::preBand;
        const bool show = (pre ? preOpened : postOpened) || effects > 0;
        if (pre) frequencyDisplay.setPreState (show, effects);
        else frequencyDisplay.setPostState (show, effects);
        auto* strip = pre ? preStrip.get() : postStrip.get();
        if (strip != nullptr && strip->isVisible() != show) { strip->setVisible (show); changed = true; }
    }
    if (changed) layoutStrips();
}
int TriadEditor::stripOrigin() const
{
    return isPreShown() ? preStrip->getWidth() + stripGap : 0;
}
void TriadEditor::layoutStrips()
{
    const int available = viewport.getWidth();
    const bool pre = isPreShown(), post = isPostShown();
    const int n = juce::jmax (1, (int) strips.size() + (pre ? 1 : 0) + (post ? 1 : 0));
    const int width = juce::jlimit (minStripWidth, maxStripWidth, (available - stripGap * (n - 1)) / n);
    const int total = n * width + stripGap * (n - 1);
    const int height = juce::jmax (100, viewport.getHeight() - 8);
    stripHolder.setSize (juce::jmax (total, available), height);
    if (pre) preStrip->setBounds (0, 0, width, height);   // PRE first: it runs before the bands
    const int origin = pre ? width + stripGap : 0;
    // While a band is dragged, the others make room where it would land.
    int position = 0;
    for (int i = 0; i < (int) strips.size(); ++i)
    {
        if (drag.active && i == drag.band) continue;
        if (drag.active && position == drag.target) ++position;
        strips[(size_t) i]->setBounds (origin + position * (width + stripGap), 0, width, height);
        ++position;
    }
    if (drag.active && drag.band >= 0 && drag.band < (int) strips.size())
        strips[(size_t) drag.band]->setSize (width, height);
    if (post) postStrip->setBounds (origin + (int) strips.size() * (width + stripGap), 0, width, height);   // POST last: it runs after them
}
void TriadEditor::timerCallback()
{
    if (processor.getBandCount() != builtCount) rebuildStrips();
    {
        // The latency meter's tooltip: each band's latency and the effects in it, and all of them added up.
        // Bands play side by side, so the slowest band sets Triad's delay (the others wait for it).
        juce::StringArray lines;
        const double sr = juce::jmax (1.0, processor.getSampleRateForDisplay());
        auto ms = [sr] (int samples) { return triad::ui::formatMs (1000.0 * samples / sr); };
        int all = 0;
        for (int b = -1; b <= builtCount; ++b)
        {
            const int index = b < 0 ? TriadProcessor::preBand : b == builtCount ? TriadProcessor::postBand : b;
            juce::StringArray effects;
            int chain = 0;
            for (int s = 0; s < processor.getSlotCount (index); ++s)
                if (const auto info = processor.getSlotInfo (index, s); info.latency > 0) { effects.add (info.name + " " + ms (info.latency)); chain += info.latency; }
            all += chain;
            if (chain > 0)
                lines.add ((TriadProcessor::isExtraChain (index) ? TriadProcessor::chainName (index) + " (adds to every band)" : "Band " + juce::String (b + 1)) + ": " + ms (chain)
                           + " (" + effects.joinIntoString (" + ") + ")");
        }
        if (! lines.isEmpty())
            lines.add ("All effects added up: " + ms (all) + ". Bands play side by side, so the slowest band sets the delay you hear"
                       + (processor.getPreLatencySamples() + processor.getPostLatencySamples() > 0 ? juce::String (", plus PRE before them and POST after.")
                                                                                                    : juce::String (".")));
        frequencyDisplay.setLatency (processor.getLatencySamples(), sr, lines.joinIntoString ("\n"));
    }
    if (tab == Tab::bands)
    {
        const auto cuts = processor.effectiveCrossovers();
        for (auto& strip : strips) strip->update (cuts, builtCount, processor.nyquistFrequency());
        updateChains();
        if (isPreShown()) preStrip->update (cuts, builtCount, processor.nyquistFrequency());
        if (isPostShown()) postStrip->update (cuts, builtCount, processor.nyquistFrequency());
        frequencyDisplay.refreshIfChanged();
    }
    else if (tab == Tab::lfo) lfoPage.refresh();
    else matrixPage.refresh();
    if (const auto name = processor.getPresetName(); name != presetButton.presetName) { presetButton.presetName = name; presetButton.repaint(); }
    {
        const int back = processor.getUndoSteps(), forward = processor.getRedoSteps();
        undoButton.setEnabled (processor.canUndo());
        redoButton.setEnabled (processor.canRedo());
        undoButton.setTooltip (back > 0 ? "Undo the last change (" + juce::String (back) + " step" + (back == 1 ? "" : "s") + " back)"
                                        : juce::String ("Undo: nothing to undo yet"));
        redoButton.setTooltip (forward > 0 ? "Redo (" + juce::String (forward) + " step" + (forward == 1 ? "" : "s") + " forward)"
                                           : juce::String ("Redo: nothing to redo"));
    }
    juce::String text;
    for (int b = -1; b <= builtCount && text.isEmpty(); ++b)
    {
        const int index = b < 0 ? TriadProcessor::preBand : b == builtCount ? TriadProcessor::postBand : b;
        for (int s = 0; s < processor.getSlotCount (index); ++s)
            if (auto info = processor.getSlotInfo (index, s); info.status.isNotEmpty())
            {
                text = (TriadProcessor::isExtraChain (index) ? TriadProcessor::chainName (index) : "Band " + juce::String (b + 1)) + " / Slot "
                     + juce::String (s + 1) + ": " + info.status;
                break;
            }
    }
    if (text.isEmpty() && catalog->isScanning()) text = catalog->getProgress();
    if (text.isEmpty()) text = processor.getStatus();
    if (text.isEmpty())
        text = tab == Tab::lfo ? "Each knob has its own LFO: draw on the grid and the knob follows it in time with your song."
             : tab == Tab::matrix ? "Each row: an LFO (source), how much it moves its destination (amount), and the knob it moves."
             : "Click a slot to load an effect, + to add a slot, ~ for its LFO. Drag an effect up or down to reorder it, or a band's header to move the band.";
    status.setText (text, juce::dontSendNotification);
}
void TriadEditor::animate (double frameTime)
{
    // Every frame of the screen: 120 a second on a ProMotion screen while the computer keeps up, else 60 (FramePacer).
    // The meters' ballistics follow the clock. Whatever the DAW automates (dividers, slopes, band edges) moves here too.
    const double dt = lastFrame > 0.0 ? juce::jlimit (0.0, 0.1, frameTime - lastFrame) : 1.0 / 60.0;
    if (! pacer->due (lastFrame, frameTime)) return;
    const FramePacer::Timed timed (*pacer);
    const auto meters = processor.takeMeters();
    pacer->setAudioLoad (meters.cpu);
    frequencyDisplay.setMeters (meters.input, meters.output, meters.cpu, dt);
    if (tab == Tab::bands)
    {
        if (processor.getBandCount() == builtCount)
        {
            frequencyDisplay.refreshIfChanged();
            const auto cuts = processor.effectiveCrossovers();   // as set: they stay put while LFOs move the edges (0.19.0)
            for (auto& strip : strips) strip->updateEdges (cuts, builtCount, processor.nyquistFrequency());
        }
        for (auto& strip : strips) strip->animate (meters.bands[(size_t) strip->getBand()], dt);
        if (isPreShown()) preStrip->animate (0.0f, dt);
        if (isPostShown()) postStrip->animate (0.0f, dt);
    }
}
void TriadEditor::changeListenerCallback (juce::ChangeBroadcaster*) { timerCallback(); }
void TriadEditor::setCrossoverFrequency (int index, float hz) { frequencyDisplay.setCrossoverValue (index, hz); }

void TriadEditor::undo()
{
    processor.undo();
    timerCallback();   // the bands, slots and pages as they are now
}
void TriadEditor::redo()
{
    processor.redo();
    timerCallback();
}
void TriadEditor::linkEdgesToLfo (int band, int edge)
{
    // "~ Link to LFO" (0.18.0): the band's edge to an LFO of the band (made the first time), shown in the LFO tab.
    const int lfo = processor.linkEdgesToLfo (band, edge);
    if (lfo < 0) return;
    setTab (Tab::lfo);
    lfoPage.select (band, lfo);
}
void TriadEditor::showLfo (int band, int slot)
{
    setTab (Tab::lfo);
    lfoPage.select (band, slot < 0 ? juce::jmax (0, processor.getBandLfo (band)) : processor.getSlotLfo (band, slot));
}

// ---- Moving bands -------------------------------------------------------------------------------------
void TriadEditor::bandDrag (int band, const juce::MouseEvent& e, int phase)
{
    auto* strip = getStrip (band);
    if (strip == nullptr) return;
    const auto position = e.getEventRelativeTo (&stripHolder).position;
    if (phase == 0)
    {
        drag = Drag { band, position.x, strip->getX(), false, band };
        return;
    }
    if (drag.band != band) return;
    if (phase == 1)
    {
        if (! drag.active && std::abs (position.x - drag.startMouse) < 6.0f) return;
        if (! drag.active) { drag.active = true; strip->setLifted (true); strip->toFront (false); }
        const int origin = stripOrigin();
        const int x = juce::jlimit (origin, juce::jmax (origin, stripHolder.getWidth() - strip->getWidth()),
                                    drag.startX + juce::roundToInt (position.x - drag.startMouse));
        strip->setTopLeftPosition (x, 0);
        drag.target = juce::jlimit (0, (int) strips.size() - 1, juce::roundToInt ((float) (x - origin) / (float) (strip->getWidth() + stripGap)));
        layoutStrips();
        const auto inView = e.getEventRelativeTo (&viewport).getPosition();
        viewport.autoScroll (inView.x, inView.y, 40, 14);
        return;
    }
    const bool moved = drag.active && drag.target != band && drag.target >= 0;
    const int target = drag.target;
    strip->setLifted (false);
    drag = {};
    if (moved) processor.moveBand (band, target);
    layoutStrips();
    timerCallback();
}

// ---- Effect list --------------------------------------------------------------------------------------
bool TriadEditor::isFavourite (const juce::String& format, const juce::String& name)
{
    return settings->isFavourite (TriadSettings::favouriteKey (format, name));
}
void TriadEditor::setFavourite (const juce::String& format, const juce::String& name, bool on)
{
    settings->setFavourite (TriadSettings::favouriteKey (format, name), on);
    processor.setStatus ((on ? "Added to FAVORITES: " : "Removed from FAVORITES: ") + name);
}
juce::String TriadEditor::favouriteTypeOf (const EffectEntry& e, const std::vector<EffectEntry>& all) const
{
    if (const auto set = settings->getFavouriteType (TriadSettings::favouriteKey (e.format, e.name)); triad::effectTypes().contains (set)) return set;
    auto category = e.description.category;
    if (e.format == "AU")
        for (const auto& other : all)
            if (other.format == "VST3" && other.name.equalsIgnoreCase (e.name)) { category = other.description.category; break; }
    return triad::effectType (category, e.name);
}
juce::String TriadEditor::getFavouriteType (const juce::String& format, const juce::String& name)
{
    const auto list = effectList();
    for (const auto& e : *list)
        if (e.format == format && e.name == name) return favouriteTypeOf (e, *list);
    const auto set = settings->getFavouriteType (TriadSettings::favouriteKey (format, name));
    return set.isNotEmpty() ? set : triad::effectType ({}, name);
}
void TriadEditor::setFavouriteType (const juce::String& format, const juce::String& name, const juce::String& type)
{
    const auto key = TriadSettings::favouriteKey (format, name);
    settings->setFavouriteType (key, {});
    if (getFavouriteType (format, name) != type) settings->setFavouriteType (key, type);   // only when it differs from the plug-in's own
    processor.setStatus (name + " is listed under " + type + " in FAVORITES");
}
std::shared_ptr<std::vector<EffectEntry>> TriadEditor::effectList (int* builtIns) const
{
    // Triad's built-in effect first (Triad Time), then everything installed (an installed Triad Time is the same
    // effect: only the built-in one is listed).
    auto list = std::make_shared<std::vector<EffectEntry>>();
    for (const auto& d : TriadBuiltInFormat::effects())
    {
        EffectEntry e;
        e.description = d;
        e.name = d.name;
        e.manufacturer = d.manufacturerName;
        e.format = TriadTime::formatName;
        list->push_back (e);
    }
    if (builtIns != nullptr) *builtIns = (int) list->size();
    for (auto& e : catalog->getEffects())
        if (! (e.name == "Triad Time" && e.manufacturer == "Thomas Ross")) list->push_back (std::move (e));
    return list;
}
void TriadEditor::loadEntry (int band, int slot, const EffectEntry& e)
{
    // Loading opens the effect's window straight away: one less click.
    if (e.loadByFile) processor.loadFile (band, slot, juce::File (e.description.fileOrIdentifier), true, e.name);
    else processor.loadEffect (band, slot, e.description, true);
    timerCallback();
}
void TriadEditor::openPluginSearch (int band, int slot, const juce::String& typed)
{
    if (! TriadProcessor::isExtraChain (band)) band = juce::jlimit (0, juce::jmax (0, processor.getBandCount() - 1), band);
    const bool newSlot = slot >= processor.getSlotCount (band);
    const auto target = (TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "Band " + juce::String (band + 1)) + "  /  "
                        + (newSlot ? juce::String ("new slot") : "slot " + juce::String (slot + 1));
    auto list = effectList();
    juce::Component::SafePointer<TriadEditor> safe (this);
    pluginSearch.open (list, target, typed,
                       [safe] (const EffectEntry& e) { return safe != nullptr && safe->isFavourite (e.format, e.name); },
                       [safe, list, band, slot] (int index)
                       {
                           if (safe == nullptr || index < 0 || index >= (int) list->size()) return;
                           safe->loadEntry (band, slot, (*list)[(size_t) index]);
                       },
                       [safe] (const EffectEntry& e, bool on) { if (safe != nullptr) safe->setFavourite (e.format, e.name, on); });
}
void TriadEditor::chooseEffect (int band, int slot, juce::Component& target)
{
    int builtIns = 0;
    auto list = effectList (&builtIns);
    const bool newSlot = slot >= processor.getSlotCount (band);
    const bool alphabetical = settings->getSort() == TriadSettings::Sort::alphabetical;
    const auto tagFor = [] (const EffectEntry& e) { return triad::ui::menuTag (e.format == "AU" ? juce::String ("AU") : e.format == "VST3" ? e.format : juce::String ("TRIAD")); };
    juce::PopupMenu menu;
    menu.addSectionHeader ((TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "Band " + juce::String (band + 1)) + "  /  "
                           + (newSlot ? juce::String ("New slot") : "Slot " + juce::String (slot + 1)));
    // The search first (the magnifying glass; or just start typing while the menu is open).
    {
        juce::Component::SafePointer<TriadEditor> safe (this);
        menu.addCustomItem (searchId, std::make_unique<triad::search::MenuItem> ([safe, band, slot] (const juce::String& typed)
        {
            if (safe != nullptr) safe->openPluginSearch (band, slot, typed);
        }), nullptr, "Search plug-ins");
    }
    // Favourites first: one click away (0.16.0: all of them A-Z, and by type, each opening to the side).
    const auto favourites = settings->getFavourites();
    std::vector<int> favouriteItems;
    for (int i = 0; i < (int) list->size(); ++i)
        if (const auto& e = (*list)[(size_t) i]; e.verified && favourites.contains (TriadSettings::favouriteKey (e.format, e.name)))
            favouriteItems.push_back (i);
    std::sort (favouriteItems.begin(), favouriteItems.end(),
               [&list] (int a, int b) { return (*list)[(size_t) a].name.compareIgnoreCase ((*list)[(size_t) b].name) < 0; });
    const auto heart = u8 ("\xe2\x99\xa5 ");
    if (! favouriteItems.empty())
    {
        menu.addSectionHeader (u8 ("\xe2\x99\xa5 FAVORITES"));
        if (settings->getFavouritesByType())
        {
            juce::PopupMenu all;
            for (int i : favouriteItems) triad::ui::addItemWithRight (all, i + 1, heart + (*list)[(size_t) i].name, tagFor ((*list)[(size_t) i]));
            triad::ui::addSubMenu (menu, heart + "FAVORITES", std::move (all), juce::String ((int) favouriteItems.size()));
            for (const auto& type : triad::effectTypes())
            {
                juce::PopupMenu ofType;
                int count = 0;
                ofType.addSectionHeader (type);
                for (int i : favouriteItems)
                    if (favouriteTypeOf ((*list)[(size_t) i], *list) == type)
                    {
                        triad::ui::addItemWithRight (ofType, i + 1, heart + (*list)[(size_t) i].name, tagFor ((*list)[(size_t) i]));
                        ++count;
                    }
                if (count > 0) triad::ui::addSubMenu (menu, type, std::move (ofType), juce::String (count));
            }
        }
        else
            for (int i : favouriteItems) triad::ui::addItemWithRight (menu, i + 1, heart + (*list)[(size_t) i].name, tagFor ((*list)[(size_t) i]));
        menu.addSeparator();
    }
    menu.addSectionHeader ("BUILT IN");
    for (int i = 0; i < builtIns; ++i)
        triad::ui::addItemWithRight (menu, i + 1, (*list)[(size_t) i].name, tagFor ((*list)[(size_t) i]));
    menu.addSeparator();
    menu.addSectionHeader ("ALL PLUG-INS");
    for (const juce::String format : { "VST3", "AU" })
    {
        juce::PopupMenu formatMenu;
        std::vector<int> items;
        for (int i = 0; i < (int) list->size(); ++i)
            if (const auto& e = (*list)[(size_t) i]; e.format == format && e.verified) items.push_back (i);
        if (items.empty()) continue;
        if (alphabetical)
        {
            // A to Z by plug-in name; long lists are split by first letter.
            formatMenu.addSectionHeader (format == "AU" ? juce::String ("AUDIO UNITS  A-Z") : juce::String ("VST3  A-Z"));
            std::sort (items.begin(), items.end(), [&list] (int a, int b) { return (*list)[(size_t) a].name.compareIgnoreCase ((*list)[(size_t) b].name) < 0; });
            const bool byLetter = items.size() > 40;
            juce::PopupMenu letterMenu;
            juce::String letter;
            int inLetter = 0;
            auto flush = [&]
            {
                if (letterMenu.getNumItems() > 0) triad::ui::addSubMenu (formatMenu, letter, std::move (letterMenu), juce::String (inLetter));
                letterMenu = juce::PopupMenu();
                inLetter = 0;
            };
            for (int i : items)
            {
                const auto& e = (*list)[(size_t) i];
                if (! byLetter) { triad::ui::addItemWithRight (formatMenu, i + 1, e.name, e.manufacturer); continue; }
                auto first = e.name.substring (0, 1).toUpperCase();
                if (! juce::CharacterFunctions::isLetter (first[0])) first = "#";
                if (first != letter) { flush(); letter = first; }
                triad::ui::addItemWithRight (letterMenu, i + 1, e.name, e.manufacturer);
                ++inLetter;
            }
            if (byLetter) flush();
        }
        else
        {
            formatMenu.addSectionHeader (format == "AU" ? juce::String ("AUDIO UNITS  BY COMPANY") : juce::String ("VST3  BY COMPANY"));
            juce::PopupMenu makerMenu;
            juce::String maker;
            bool any = false;
            int inMaker = 0;
            auto flush = [&]
            {
                if (makerMenu.containsAnyActiveItems())
                    triad::ui::addSubMenu (formatMenu, maker.isNotEmpty() ? maker : "Other", std::move (makerMenu), juce::String (inMaker));
                makerMenu = juce::PopupMenu();
                inMaker = 0;
            };
            for (int i : items)
            {
                const auto& e = (*list)[(size_t) i];
                if (! any || e.manufacturer != maker) { flush(); maker = e.manufacturer; makerMenu.addSectionHeader (maker.isNotEmpty() ? maker.toUpperCase() : "OTHER"); }
                makerMenu.addItem (i + 1, e.name);
                ++inMaker;
                any = true;
            }
            flush();
        }
        triad::ui::addSubMenu (menu, format == "AU" ? "Audio Units" : "VST3", std::move (formatMenu), juce::String ((int) items.size()));
    }
    {
        // Plug-ins Triad could not check (no time, or they crashed its scanner) are kept apart.
        juce::PopupMenu unchecked;
        int count = 0;
        for (int i = 0; i < (int) list->size(); ++i)
            if (const auto& e = (*list)[(size_t) i]; ! e.verified)
            {
                unchecked.addItem (i + 1, e.name + (e.crashed ? juce::String (" (crashed when checked)") : juce::String()), ! e.crashed);
                ++count;
            }
        if (count > 0) triad::ui::addSubMenu (menu, "Not checked", std::move (unchecked), juce::String (count));
    }
    if (catalog->isScanning()) menu.addItem (scanningId, catalog->getProgress(), false);
    else if (list->empty()) menu.addItem (noneId, "No effects found", false);
    menu.addSeparator();
    if (newSlot) menu.addItem (emptySlotId, "Add an empty slot");
    menu.addItem (browseId, "Browse for a plug-in file...");
    menu.addItem (refreshId, "Refresh plug-in list");
    menu.addItem (rescanId, "Rescan all plug-ins");
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, list, band, slot] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto& self = *safe;
        if (choice == searchId) { self.openPluginSearch (band, slot); return; }
        if (choice == browseId) { self.browse (band, slot); return; }
        if (choice == emptySlotId) { self.processor.addSlot (band); self.timerCallback(); return; }
        if (choice == refreshId) { self.catalog->refresh(); return; }
        if (choice == rescanId) { self.catalog->rescanAll(); return; }
        if (choice < 1 || choice > (int) list->size()) return;
        self.loadEntry (band, slot, (*list)[(size_t) choice - 1]);
    });
}
void TriadEditor::browse (int band, int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Select a VST3 or AU effect", juce::File(), "*.vst3;*.component");
    juce::Component::SafePointer<TriadEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
        [safe, band, slot] (const juce::FileChooser& c)
        {
            if (safe == nullptr || c.getResult() == juce::File()) return;
            safe->processor.loadFile (band, slot, c.getResult(), true);
            safe->timerCallback();
        });
}

// ---- Presets ------------------------------------------------------------------------------------------
bool TriadEditor::savePreset (const juce::String& name)
{
    const auto folder = TriadSettings::presetFolder();
    folder.createDirectory();
    processor.setPresetName (name);
    const bool ok = processor.createPresetXml()->writeTo (folder.getChildFile (name + ".triadpreset"));
    processor.setStatus (ok ? "Saved preset " + name : "Could not save the preset in " + folder.getFullPathName());
    return ok;
}
void TriadEditor::setStartupPreset (const juce::String& choice)
{
    settings->setStartupPreset (choice);
    processor.setStatus ("New Triads will open with "
                         + (choice.isEmpty() ? juce::String ("Init (an empty Triad)")
                            : choice == TriadSettings::currentSettings ? juce::String ("your startup settings") : "the preset \"" + choice + "\""));
}
bool TriadEditor::useCurrentSettingsAtStartup()
{
    const auto file = TriadSettings::startupSettingsFile();
    file.getParentDirectory().createDirectory();
    auto xml = processor.createPresetXml();
    xml->setAttribute ("preset", "Startup settings");
    if (! xml->writeTo (file)) { processor.setStatus ("Could not save the startup settings in " + file.getFullPathName()); return false; }
    setStartupPreset (TriadSettings::currentSettings);
    return true;
}
bool TriadEditor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("TriadState")) { processor.setStatus ("This is not a Triad preset."); return false; }
    xml->setAttribute ("preset", file.getFileNameWithoutExtension());
    processor.loadPresetXml (*xml);
    rebuildStrips();
    return true;
}
bool TriadEditor::saveChain (int band, const juce::String& name)
{
    auto xml = processor.createChainXml (band);
    if (xml == nullptr) return false;
    xml->setAttribute ("name", name);
    const auto folder = TriadSettings::chainFolder();
    folder.createDirectory();
    const bool ok = xml->writeTo (folder.getChildFile (name + ".triadchain"));
    processor.setStatus (ok ? "Saved band " + juce::String (band + 1) + " chain as " + name : "Could not save the chain");
    return ok;
}
bool TriadEditor::loadChain (int band, const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    const auto error = xml != nullptr ? processor.loadChainXml (band, *xml) : juce::String ("This is not a Triad chain preset.");
    processor.setStatus (error.isEmpty() ? "Loaded chain " + file.getFileNameWithoutExtension() + " into band " + juce::String (band + 1) : error);
    return error.isEmpty();
}
void TriadEditor::showPresetMenu()
{
    enum { saveId = 1, saveAsId, folderId, initId, firstPreset = 100 };
    const auto files = filesIn (TriadSettings::presetFolder(), "*.triadpreset");
    const auto current = processor.getPresetName();
    juce::PopupMenu menu;
    menu.addSectionHeader ("TRIAD PRESETS");
    if (current.isNotEmpty() && current != "Init") menu.addItem (saveId, "Save \"" + current + "\"");
    menu.addItem (saveAsId, "Save as new preset...");
    menu.addItem (initId, "Initialize (an empty Triad)");
    menu.addSeparator();
    for (int i = 0; i < files.size(); ++i) menu.addItem (firstPreset + i, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == current);
    if (files.isEmpty()) menu.addItem (-1, "No presets saved yet", false);
    menu.addSeparator();
    menu.addItem (folderId, "Show presets folder");
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [safe, files, current] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto& self = *safe;
        if (choice == saveId) self.savePreset (current);
        else if (choice == saveAsId)
            self.prompt.show ("Save Triad preset", current.isNotEmpty() ? current : "My preset",
                              [safe] (const juce::String& name) { if (safe != nullptr) safe->savePreset (name); });
        else if (choice == folderId) { TriadSettings::presetFolder().createDirectory(); TriadSettings::presetFolder().revealToUser(); }
        else if (choice == initId) { self.processor.initialize(); self.rebuildStrips(); }
        else if (choice >= firstPreset && choice - firstPreset < files.size()) self.loadPreset (files[choice - firstPreset]);
    });
}
void TriadEditor::showChainMenu (int band, juce::Component& target, bool atMouse)
{
    enum { pastePluginId = 1, lfoId, clearId, hideId };
    const auto name = TriadProcessor::chainName (band);
    juce::PopupMenu menu;
    menu.addSectionHeader (name + (band == TriadProcessor::preBand ? "  (FULL RANGE, BEFORE THE BANDS)" : "  (FULL RANGE, AFTER THE BANDS)"));
    menu.addItem (pastePluginId, TriadProcessor::hasCopiedEffect() ? "Paste plugin (" + TriadProcessor::copiedEffectName() + ") into a new slot"
                                                                   : juce::String ("Paste plugin into a new slot"),
                  TriadProcessor::hasCopiedEffect());
    menu.addItem (lfoId, "Edit LFOs for " + name + "...", processor.getLfoCount (band) > 0);
    menu.addSeparator();
    menu.addItem (clearId, "Remove all " + name + " effects", processor.hasChainEffects (band));
    menu.addItem (hideId, "Hide " + name, ! processor.hasChainEffects (band));
    auto options = juce::PopupMenu::Options().withTargetComponent (&target);
    if (atMouse) options = options.withMousePosition();
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (options, [safe, band] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto& self = *safe;
        if (choice == pastePluginId) self.processor.pasteEffectState (band, self.processor.getSlotCount (band));
        else if (choice == lfoId) self.showLfo (band, 0);
        else if (choice == clearId)
            for (int s = self.processor.getSlotCount (band); --s >= 0;) self.processor.removeSlot (band, s);
        else if (choice == hideId) { (band == TriadProcessor::preBand ? self.preOpened : self.postOpened) = false; self.updateChains(); }
        self.timerCallback();
    });
}
void TriadEditor::showBandMenu (int band, juce::Component& target, bool atMouse)
{
    if (TriadProcessor::isExtraChain (band)) { showChainMenu (band, target, atMouse); return; }
    enum { saveId = 1, lfoId, leftId, rightId, removeId, folderId, copyId, pasteId, pastePluginId, firstChain = 100 };
    const auto files = filesIn (TriadSettings::chainFolder(), "*.triadchain");
    const int count = processor.getBandCount();
    juce::PopupMenu menu, chains;
    menu.addSectionHeader ("BAND " + juce::String (band + 1));
    menu.addItem (copyId, "Copy band state");
    menu.addItem (pasteId, TriadProcessor::hasCopiedBand() ? "Paste band state (" + TriadProcessor::copiedBandDescription() + ")"
                                                           : juce::String ("Paste band state"),
                  TriadProcessor::hasCopiedBand());
    if (TriadProcessor::hasCopiedEffect())
        menu.addItem (pastePluginId, "Paste plugin (" + TriadProcessor::copiedEffectName() + ") into a new slot");
    menu.addSeparator();
    menu.addItem (saveId, "Save chain as preset...");
    for (int i = 0; i < files.size(); ++i) chains.addItem (firstChain + i, files[i].getFileNameWithoutExtension());
    if (files.isEmpty()) chains.addItem (-1, "No chains saved yet", false);
    chains.addSeparator();
    chains.addItem (folderId, "Show chains folder");
    menu.addSubMenu ("Load chain preset (replaces this band's effects)", chains);
    menu.addSeparator();
    menu.addItem (lfoId, "Edit LFOs for this band...");
    if (band > 0) menu.addItem (leftId, "Move effects to band " + juce::String (band));            // only the moves there are
    if (band < count - 1) menu.addItem (rightId, "Move effects to band " + juce::String (band + 2));
    menu.addSeparator();
    menu.addItem (removeId, "Remove band " + juce::String (band + 1), count > 1);
    auto options = juce::PopupMenu::Options().withTargetComponent (&target);
    if (atMouse) options = options.withMousePosition();
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (options, [safe, band, files] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->bandMenuChoice (band, choice, files);
    });
}
void TriadEditor::bandMenuChoice (int band, int choice, const juce::Array<juce::File>& files)
{
    enum { saveId = 1, lfoId, leftId, rightId, removeId, folderId, copyId, pasteId, pastePluginId, firstChain = 100 };
    juce::Component::SafePointer<TriadEditor> safe (this);
    if (choice == copyId) processor.copyBandState (band);
    else if (choice == pasteId) processor.pasteBandState (band);
    else if (choice == pastePluginId) processor.pasteEffectState (band, processor.getSlotCount (band));
    else if (choice == saveId)
        prompt.show ("Save band " + juce::String (band + 1) + " chain", "Band " + juce::String (band + 1) + " chain",
                     [safe, band] (const juce::String& name) { if (safe != nullptr) safe->saveChain (band, name); });
    else if (choice == lfoId) showLfo (band, -1);
    else if (choice == leftId) processor.moveBand (band, band - 1);
    else if (choice == rightId) processor.moveBand (band, band + 1);
    else if (choice == removeId) processor.removeBand (band);
    else if (choice == folderId) { TriadSettings::chainFolder().createDirectory(); TriadSettings::chainFolder().revealToUser(); }
    else if (choice >= firstChain && choice - firstChain < files.size()) loadChain (band, files[choice - firstChain]);
    timerCallback();
}

// ---- Settings -----------------------------------------------------------------------------------------
void TriadEditor::addPluginFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Add a folder with VST3 plug-ins", juce::File());
    juce::Component::SafePointer<TriadEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& c)
    {
        if (safe == nullptr || ! c.getResult().isDirectory()) return;
        auto folders = safe->settings->getFolders();
        folders.addIfNotAlreadyThere (c.getResult().getFullPathName());
        safe->settings->setFolders (folders);
        safe->catalog->configure (folders, safe->settings->getAutoCheck());
    });
}
void TriadEditor::showGearMenu()
{
    enum { sortCompany = 1, sortAZ, autoCheck, refreshList, rescanAll, addFolder, presetsFolder, chainsFolder,
           startupUseCurrent, startupInit, startupCurrentSettings, favouritesByType, windowSize, scaleBase = 100, removeFolderBase = 200,
           startupBase = 1000, pitchLabelsBase = 900000, themeBase = 800000, backdropSongBase = 700000, backdropAudioBase = 700010,
           timeBackdropBase = 700020, ghostMovementId = 700030 };
    const auto folders = settings->getFolders();
    // 0.16.0: grouped as in Thomas's picture, each group's details opening to the side.
    juce::PopupMenu menu;
    menu.addSectionHeader ("EFFECT LIST");
    {
        juce::PopupMenu list, removeMenu;
        list.addSectionHeader ("EFFECT LIST");
        list.addItem (sortCompany, "Sort by company", true, settings->getSort() == TriadSettings::Sort::company);
        list.addItem (sortAZ, "Sort A-Z by name", true, settings->getSort() == TriadSettings::Sort::alphabetical);
        list.addItem (autoCheck, "Look for new plug-ins each time Triad opens", true, settings->getAutoCheck());
        list.addSeparator();
        list.addItem (refreshList, "Rescan for new plug-ins");
        list.addItem (rescanAll, "Rescan everything");
        list.addSeparator();
        list.addItem (addFolder, "Add a plug-in folder...");
        for (int i = 0; i < folders.size(); ++i) removeMenu.addItem (removeFolderBase + i, folders[i]);
        triad::ui::addSubMenu (list, "Remove a plug-in folder", std::move (removeMenu), {}, ! folders.isEmpty());
        list.addSeparator();
        list.addItem (-1, catalog->getProgress(), false);
        triad::ui::addSubMenu (menu, "Sorting, folders and rescans", std::move (list));
    }
    menu.addSeparator();
    menu.addSectionHeader ("FAVORITES");
    menu.addItem (favouritesByType, "Favorites by type (DELAY, DISTORTION ...)", true, settings->getFavouritesByType());
    menu.addSeparator();
    menu.addSectionHeader ("WINDOW SIZE");
    const juce::StringArray sizes { "75%", "90%", "100%", "125%", "150%" };
    const int percents[] { 75, 90, 100, 125, 150 };
    auto pickedSize = std::make_shared<int> (-1);
    {
        int now = -1;
        for (int i = 0; i < 5; ++i) if (juce::roundToInt (scale * 100.0) == percents[i]) now = i;
        menu.addCustomItem (windowSize, std::make_unique<triad::ui::MenuChips> (sizes, now, pickedSize), nullptr, "Window size");
    }
    menu.addSeparator();
    menu.addSectionHeader ("COLOURS");
    {
        juce::PopupMenu themes;
        triad::ui::addThemeItems (themes, themeBase, false);
        triad::ui::addSubMenu (menu, "Theme", std::move (themes), triad::ui::themeDisplayName (triad::ui::currentTheme()));
        menu.addItem (themeBase + 201, "Edit the colours...");
    }
    menu.addSeparator();
    const auto backdropName = [] (TriadSettings::Backdrop b)
    {
        return b == TriadSettings::Backdrop::waveform ? juce::String ("Waveform") : b == TriadSettings::Backdrop::spectrum ? juce::String ("Spectrum") : juce::String ("Nothing");
    };
    menu.addSectionHeader ("BANDS TAB");
    triad::ui::addItemWithRight (menu, ghostMovementId, triad::ui::menuLfo ("Ghost movement"), "LFOs on the band edges", true, settings->getGhostMovement());
    menu.addSeparator();
    menu.addSectionHeader ("LFO TAB: BEHIND THE GRID");
    {
        // What is drawn behind each LFO's grid: the sound coming into its band (0.15.0).
        const auto addChoices = [&] (juce::PopupMenu& m, const juce::String& title, int base, TriadSettings::Backdrop now)
        {
            m.addSectionHeader (title);
            m.addItem (base + 0, "Nothing", true, now == TriadSettings::Backdrop::off);
            m.addItem (base + 1, "The waveform (the sound across the LFO's cycle)", true, now == TriadSettings::Backdrop::waveform);
            m.addItem (base + 2, "The spectrum (as on the BANDS tab, the band's range lit)", true, now == TriadSettings::Backdrop::spectrum);
        };
        juce::PopupMenu song, audio;
        addChoices (song, "BANDS ON TRIGGER: SONG", backdropSongBase, settings->getLfoBackdrop (false));
        addChoices (audio, "BANDS ON TRIGGER: AUDIO", backdropAudioBase, settings->getLfoBackdrop (true));
        triad::ui::addSubMenu (menu, "Bands on TRIGGER: SONG", std::move (song), backdropName (settings->getLfoBackdrop (false)));
        triad::ui::addSubMenu (menu, "Bands on TRIGGER: AUDIO", std::move (audio), backdropName (settings->getLfoBackdrop (true)));
    }
    menu.addSeparator();
    menu.addSectionHeader ("TRIAD TIME");
    {
        const auto now = settings->getTimeBackdrop();
        juce::PopupMenu sound;
        sound.addSectionHeader ("BEHIND THE GRID");
        sound.addItem (timeBackdropBase + 0, "Nothing", true, now == TriadSettings::Backdrop::off);
        sound.addItem (timeBackdropBase + 1, "The waveform (the sound coming in, across the pattern)", true, now == TriadSettings::Backdrop::waveform);
        sound.addItem (timeBackdropBase + 2, "The spectrum (as Triad's, Pro-Q style)", true, now == TriadSettings::Backdrop::spectrum);
        triad::ui::addSubMenu (menu, "Behind the grid", std::move (sound), backdropName (now));
    }
    {
        juce::PopupMenu labels;
        const auto mode = settings->getPitchLabels();
        labels.addSectionHeader ("PITCH LABELS (FWD / REV, SEMITONES)");
        labels.addItem (pitchLabelsBase + 0, "Off", true, mode == TriadSettings::PitchLabels::off);
        labels.addItem (pitchLabelsBase + 1, "While dragging a point", true, mode == TriadSettings::PitchLabels::dragging);
        labels.addItem (pitchLabelsBase + 2, "Always, on every line", true, mode == TriadSettings::PitchLabels::always);
        triad::ui::addSubMenu (menu, "Pitch labels on the TIME lines", std::move (labels),
                               mode == TriadSettings::PitchLabels::off ? "Off" : mode == TriadSettings::PitchLabels::always ? "Always" : "Dragging");
    }
    menu.addSeparator();
    menu.addSectionHeader ("PRESETS");
    const auto presetFiles = filesIn (TriadSettings::presetFolder(), "*.triadpreset");
    const auto startup = settings->getStartupPreset();
    {
        juce::PopupMenu startupMenu;
        startupMenu.addSectionHeader ("STARTUP PRESET");
        startupMenu.addItem (startupInit, "Init (an empty Triad)", true, startup.isEmpty());
        if (TriadSettings::startupSettingsFile().existsAsFile())
            startupMenu.addItem (startupCurrentSettings, "My startup settings (saved with \"Use the current settings\")", true,
                                 startup == TriadSettings::currentSettings);
        if (! presetFiles.isEmpty()) startupMenu.addSectionHeader ("SAVED PRESETS");
        for (int i = 0; i < presetFiles.size(); ++i)
            startupMenu.addItem (startupBase + i, presetFiles[i].getFileNameWithoutExtension(), true, presetFiles[i].getFileNameWithoutExtension() == startup);
        const auto label = startup.isEmpty() ? juce::String ("Init") : startup == TriadSettings::currentSettings ? juce::String ("Mine") : startup;
        triad::ui::addSubMenu (menu, "Startup preset", std::move (startupMenu), label);
    }
    menu.addItem (startupUseCurrent, "Use the current settings as the startup preset");
    menu.addItem (presetsFolder, "Show presets folder");
    menu.addItem (chainsFolder, "Show chain presets folder");
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearButton), [safe, folders, presetFiles, pickedSize] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto& self = *safe;
        auto& s = *self.settings;
        if (choice == windowSize)
        {
            const int percent[] { 75, 90, 100, 125, 150 };
            if (*pickedSize >= 0 && *pickedSize < 5) { s.setScale (percent[*pickedSize] / 100.0); self.setScale (s.getScale()); }
            return;
        }
        if (choice == favouritesByType) { s.setFavouritesByType (! s.getFavouritesByType()); return; }
        if (choice == ghostMovementId)
        {
            s.setGhostMovement (! s.getGhostMovement());
            CrossoverDisplay::ghostMovement = s.getGhostMovement();
            self.frequencyDisplay.repaint();
            return;
        }
        if (choice == startupInit || choice == startupCurrentSettings || (choice >= startupBase && choice - startupBase < presetFiles.size()))
        {
            self.setStartupPreset (choice == startupInit ? juce::String() : choice == startupCurrentSettings ? juce::String (TriadSettings::currentSettings)
                                                                         : presetFiles[choice - startupBase].getFileNameWithoutExtension());
            return;
        }
        if (choice == startupUseCurrent) { self.useCurrentSettingsAtStartup(); return; }
        if (triad::ui::handleThemeChoice (choice, themeBase, [&self] { self.openThemePanel(); })) return;
        if (choice >= pitchLabelsBase && choice <= pitchLabelsBase + 2) { s.setPitchLabels ((TriadSettings::PitchLabels) (choice - pitchLabelsBase)); return; }
        if (choice >= backdropSongBase && choice <= backdropSongBase + 2) { s.setLfoBackdrop (false, (TriadSettings::Backdrop) (choice - backdropSongBase)); return; }
        if (choice >= backdropAudioBase && choice <= backdropAudioBase + 2) { s.setLfoBackdrop (true, (TriadSettings::Backdrop) (choice - backdropAudioBase)); return; }
        if (choice >= timeBackdropBase && choice <= timeBackdropBase + 2) { s.setTimeBackdrop ((TriadSettings::Backdrop) (choice - timeBackdropBase)); return; }
        if (choice == sortCompany) s.setSort (TriadSettings::Sort::company);
        else if (choice == sortAZ) s.setSort (TriadSettings::Sort::alphabetical);
        else if (choice == autoCheck)
        {
            s.setAutoCheck (! s.getAutoCheck());
            if (s.getAutoCheck()) self.catalog->configure (s.getFolders(), true);
        }
        else if (choice == refreshList) self.catalog->refresh();
        else if (choice == rescanAll) self.catalog->rescanAll();
        else if (choice == addFolder) self.addPluginFolder();
        else if (choice == presetsFolder) { TriadSettings::presetFolder().createDirectory(); TriadSettings::presetFolder().revealToUser(); }
        else if (choice == chainsFolder) { TriadSettings::chainFolder().createDirectory(); TriadSettings::chainFolder().revealToUser(); }
        else if (choice > scaleBase && choice < removeFolderBase) { s.setScale ((choice - scaleBase) / 100.0); self.setScale (s.getScale()); }
        else if (choice >= removeFolderBase && choice - removeFolderBase < folders.size())
        {
            auto next = folders;
            next.remove (choice - removeFolderBase);
            s.setFolders (next);
            self.catalog->configure (next, s.getAutoCheck());
        }
    });
}
