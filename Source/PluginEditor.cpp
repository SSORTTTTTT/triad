#include "PluginEditor.h"

using namespace triad::ui;

namespace
{
constexpr int margin = 24, headerHeight = 52, stripGap = 10, minStripWidth = 196, maxStripWidth = 420;
constexpr int displayHeight = 210;
enum MenuIds { scanningId = 1000001, noneId, browseId, refreshId, rescanId, emptySlotId };
juce::Array<juce::File> filesIn (const juce::File& folder, const juce::String& pattern)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, pattern);
    files.sort();
    return files;
}
}

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
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto box = getLocalBounds().withSizeKeepingCentre (380, 150).toFloat();
    g.setColour (panel); g.fillRoundedRectangle (box, 8.0f);
    g.setColour (accent); g.drawRoundedRectangle (box, 8.0f, 1.2f);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, box.reduced (18.0f, 14.0f).removeFromTop (20.0f), juce::Justification::centredLeft);
}
void NamePrompt::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (380, 150).reduced (18, 14);
    box.removeFromTop (28);
    text.setBounds (box.removeFromTop (34));
    box.removeFromTop (14);
    auto buttons = box.removeFromTop (30);
    saveButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

// ---- Header buttons ---------------------------------------------------------------------------------
void TriadEditor::TabButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    g.setColour (on ? juce::Colour (0xff242826) : over ? juce::Colour (0xff383c3a) : juce::Colours::transparentBlack);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f);
    g.setColour (on ? accent : over ? textColour : dimTextColour);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (getName(), getLocalBounds(), juce::Justification::centred);
    if (on) { g.setColour (accent); g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromBottom (3.0f).reduced (10.0f, 0.0f), 1.5f); }
}
void TriadEditor::PresetButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (over ? juce::Colour (0xff1f2321) : juce::Colour (0xff1a1d1c));
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
    : AudioProcessorEditor (p), processor (p), frequencyDisplay (p), lfoPage (p), matrixPage (p)
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
    catalog->addChangeListener (this);
    rebuildStrips();
    setTab (Tab::bands);
    setScale (settings->getScale());
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
    g.setColour (juce::Colour (0xffaeb6ab)); g.setFont (juce::FontOptions (11.5f));
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
    // At most 60 frames a second (a 120 Hz screen gets every other one); the meters' ballistics follow the clock.
    const double dt = lastFrame > 0.0 ? juce::jlimit (0.0, 0.1, frameTime - lastFrame) : 1.0 / 60.0;
    if (lastFrame > 0.0 && dt < 1.0 / 65.0) return;
    lastFrame = frameTime;
    const auto meters = processor.takeMeters();
    frequencyDisplay.setMeters (meters.input, meters.output, meters.cpu, dt);
    if (tab == Tab::bands)
    {
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
    processor.setStatus ((on ? "Added to favourites: " : "Removed from favourites: ") + name);
}
void TriadEditor::chooseEffect (int band, int slot, juce::Component& target)
{
    auto list = std::make_shared<std::vector<EffectEntry>> (catalog->getEffects());
    const bool newSlot = slot >= processor.getSlotCount (band);
    const bool alphabetical = settings->getSort() == TriadSettings::Sort::alphabetical;
    juce::PopupMenu menu;
    menu.addSectionHeader ((TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "Band " + juce::String (band + 1)) + "  /  "
                           + (newSlot ? juce::String ("New slot") : "Slot " + juce::String (slot + 1)));
    // Favourites first: one click away.
    const auto favourites = settings->getFavourites();
    std::vector<int> favouriteItems;
    for (int i = 0; i < (int) list->size(); ++i)
        if (const auto& e = (*list)[(size_t) i]; e.verified && favourites.contains (TriadSettings::favouriteKey (e.format, e.name)))
            favouriteItems.push_back (i);
    if (! favouriteItems.empty())
    {
        menu.addSectionHeader ("FAVOURITES");
        for (int i : favouriteItems) menu.addItem (i + 1, (*list)[(size_t) i].name + "  (" + (*list)[(size_t) i].format + ")");
        menu.addSeparator();
    }
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
            std::sort (items.begin(), items.end(), [&list] (int a, int b) { return (*list)[(size_t) a].name.compareIgnoreCase ((*list)[(size_t) b].name) < 0; });
            const bool byLetter = items.size() > 40;
            juce::PopupMenu letterMenu;
            juce::String letter;
            auto flush = [&] { if (letterMenu.getNumItems() > 0) formatMenu.addSubMenu (letter, letterMenu); letterMenu = juce::PopupMenu(); };
            for (int i : items)
            {
                const auto& e = (*list)[(size_t) i];
                const auto label = e.name + (e.manufacturer.isNotEmpty() ? "  -  " + e.manufacturer : juce::String());
                if (! byLetter) { formatMenu.addItem (i + 1, label); continue; }
                auto first = e.name.substring (0, 1).toUpperCase();
                if (! juce::CharacterFunctions::isLetter (first[0])) first = "#";
                if (first != letter) { flush(); letter = first; }
                letterMenu.addItem (i + 1, label);
            }
            if (byLetter) flush();
        }
        else
        {
            juce::PopupMenu makerMenu;
            juce::String maker;
            bool any = false;
            auto flush = [&]
            {
                if (makerMenu.containsAnyActiveItems()) formatMenu.addSubMenu (maker.isNotEmpty() ? maker : "Other", makerMenu);
                makerMenu = juce::PopupMenu();
            };
            for (int i : items)
            {
                const auto& e = (*list)[(size_t) i];
                if (! any || e.manufacturer != maker) { flush(); maker = e.manufacturer; }
                makerMenu.addItem (i + 1, e.name);
                any = true;
            }
            flush();
        }
        menu.addSubMenu (format == "AU" ? "Audio Units" : "VST3", formatMenu);
    }
    {
        // Plug-ins Triad could not check (no time, or they crashed its scanner) are kept apart.
        juce::PopupMenu unchecked;
        for (int i = 0; i < (int) list->size(); ++i)
            if (const auto& e = (*list)[(size_t) i]; ! e.verified)
                unchecked.addItem (i + 1, e.name + (e.crashed ? juce::String (" (crashed when checked)") : juce::String()), ! e.crashed);
        if (unchecked.getNumItems() > 0) menu.addSubMenu ("Not checked", unchecked);
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
        if (choice == browseId) { self.browse (band, slot); return; }
        if (choice == emptySlotId) { self.processor.addSlot (band); self.timerCallback(); return; }
        if (choice == refreshId) { self.catalog->refresh(); return; }
        if (choice == rescanId) { self.catalog->rescanAll(); return; }
        if (choice < 1 || choice > (int) list->size()) return;
        const auto& e = (*list)[(size_t) choice - 1];
        // Loading opens the effect's window straight away: one less click.
        if (e.loadByFile) self.processor.loadFile (band, slot, juce::File (e.description.fileOrIdentifier), true, e.name);
        else self.processor.loadEffect (band, slot, e.description, true);
        self.timerCallback();
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
    menu.addItem (leftId, "Move effects to band " + juce::String (band), band > 0);
    menu.addItem (rightId, "Move effects to band " + juce::String (band + 2), band < count - 1);
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
           startupUseCurrent, startupInit, startupCurrentSettings, scaleBase = 100, removeFolderBase = 200, startupBase = 1000 };
    const auto folders = settings->getFolders();
    juce::PopupMenu menu, removeMenu;
    menu.addSectionHeader ("EFFECT LIST");
    menu.addItem (sortCompany, "Sort by company", true, settings->getSort() == TriadSettings::Sort::company);
    menu.addItem (sortAZ, "Sort A-Z by name", true, settings->getSort() == TriadSettings::Sort::alphabetical);
    menu.addItem (autoCheck, "Look for new plug-ins each time Triad opens", true, settings->getAutoCheck());
    menu.addItem (refreshList, "Rescan for new plug-ins");
    menu.addItem (rescanAll, "Rescan everything");
    menu.addItem (addFolder, "Add a plug-in folder...");
    for (int i = 0; i < folders.size(); ++i) removeMenu.addItem (removeFolderBase + i, folders[i]);
    if (! folders.isEmpty()) menu.addSubMenu ("Remove a plug-in folder", removeMenu);
    menu.addItem (-1, catalog->getProgress(), false);
    menu.addSectionHeader ("WINDOW SIZE");
    for (int percent : { 75, 90, 100, 125, 150 })
        menu.addItem (scaleBase + percent, juce::String (percent) + "%", true, juce::roundToInt (scale * 100.0) == percent);
    menu.addSectionHeader ("PRESETS");
    const auto presetFiles = filesIn (TriadSettings::presetFolder(), "*.triadpreset");
    const auto startup = settings->getStartupPreset();
    {
        juce::PopupMenu startupMenu;
        startupMenu.addItem (startupInit, "Init (an empty Triad)", true, startup.isEmpty());
        if (TriadSettings::startupSettingsFile().existsAsFile())
            startupMenu.addItem (startupCurrentSettings, "My startup settings (saved with \"Use the current settings\")", true,
                                 startup == TriadSettings::currentSettings);
        if (! presetFiles.isEmpty()) startupMenu.addSectionHeader ("SAVED PRESETS");
        for (int i = 0; i < presetFiles.size(); ++i)
            startupMenu.addItem (startupBase + i, presetFiles[i].getFileNameWithoutExtension(), true, presetFiles[i].getFileNameWithoutExtension() == startup);
        const auto label = startup.isEmpty() ? juce::String ("Init") : startup == TriadSettings::currentSettings ? juce::String ("my startup settings") : startup;
        menu.addSubMenu ("Change startup preset (now: " + label + ")", startupMenu);
    }
    menu.addItem (startupUseCurrent, "Use the current settings as the startup preset");
    menu.addItem (presetsFolder, "Show presets folder");
    menu.addItem (chainsFolder, "Show chain presets folder");
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearButton), [safe, folders, presetFiles] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto& self = *safe;
        auto& s = *self.settings;
        if (choice == startupInit || choice == startupCurrentSettings || (choice >= startupBase && choice - startupBase < presetFiles.size()))
        {
            self.setStartupPreset (choice == startupInit ? juce::String() : choice == startupCurrentSettings ? juce::String (TriadSettings::currentSettings)
                                                                         : presetFiles[choice - startupBase].getFileNameWithoutExtension());
            return;
        }
        if (choice == startupUseCurrent) { self.useCurrentSettingsAtStartup(); return; }
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
