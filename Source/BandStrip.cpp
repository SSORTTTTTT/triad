#include "BandStrip.h"
#include "Settings.h"
#include "PluginCatalog.h"

using namespace triad::ui;

namespace
{
constexpr int rowHeight = 30, rowGap = 2, addHeight = 24, footerHeight = 92;
}

// ---- Slot row -------------------------------------------------------------------------------
SlotRow::SlotRow (TriadProcessor& p, StripHost& h, int b, int s, juce::Colour c)
    : processor (p), host (h), band (b), slot (s), colour (c)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRange (0.0, 100.0, 0.1);
    knob.setValue (p.getSlotMix (b, s), juce::dontSendNotification);
    knob.setDoubleClickReturnValue (true, 100.0);
    knob.setColour (juce::Slider::rotarySliderFillColourId, colour);
    knob.setTitle ("Band " + juce::String (band + 1) + " slot " + juce::String (slot + 1) + " mix");
    knob.setPopupDisplayEnabled (false, false, nullptr);
    // The slot mix is saved with the project (it is not a DAW automation parameter).
    knob.onValueChange = [this] { processor.setSlotMix (band, slot, (float) knob.getValue()); };
    addAndMakeVisible (knob);
    setTitle ("Band " + juce::String (band + 1) + " slot " + juce::String (slot + 1));
}
double SlotRow::latencyMs (int samples) const { return 1000.0 * samples / juce::jmax (1.0, processor.getSampleRateForDisplay()); }
juce::String SlotRow::latencyText (int samples) const
{
    const double ms = latencyMs (samples);
    return formatMs (ms);
}
void SlotRow::update (const TriadProcessor::SlotInfo& next, int slotCount)
{
    if (! knob.isMouseButtonDown() && std::abs (knob.getValue() - next.mix) > 0.001)
        knob.setValue (next.mix, juce::dontSendNotification);
    const bool liveChanged = std::abs (next.liveMix - info.liveMix) > 0.2f || next.modulated != info.modulated;
    if (! first && slotCount == count && next.name == info.name && next.status == info.status && next.loaded == info.loaded
        && next.enabled == info.enabled && next.pending == info.pending && next.hasContent == info.hasContent && next.format == info.format
        && next.reportedLatency == info.reportedLatency && next.liveBypassed == info.liveBypassed && next.latency == info.latency)
    {
        if (liveChanged) { info.liveMix = next.liveMix; info.modulated = next.modulated; }   // the dot itself moves in animate()
        return;
    }
    first = false;
    count = slotCount;
    info = next;
    const auto label = info.name.isNotEmpty() ? info.name : "Slot " + juce::String (slot + 1);
    const auto latencyNote = info.reportedLatency <= 0 ? juce::String()
                           : info.liveBypassed ? "\nLIVE is on: passed by, because it adds " + latencyText (info.reportedLatency) + " of latency"
                                               : "\nAdds " + latencyText (info.reportedLatency) + " of latency (" + juce::String (info.reportedLatency) + " samples)";
    setTooltip (info.loaded ? label + " (" + info.format + ")  -  click to open, arrow for options, drag to move" + latencyNote
                            : info.hasContent ? label + "  -  " + (info.pending ? juce::String ("loading...") : info.status)
                                              : "Slot " + juce::String (slot + 1) + "  -  click to load an effect");
    knob.setTooltip (label + " mix (dry/wet). Double-click for 100%.");
    knob.setAlpha (info.loaded && info.enabled ? 1.0f : 0.55f);
    repaint();
}
void SlotRow::resized()
{
    // The mix as a box: the value over a line meter (0.16.0).
    knob.setBounds (getWidth() - 22 - 50, 3, 50, getHeight() - 6);
}
void SlotRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (hover ? slotRowHover : slotRow);
    g.fillRoundedRectangle (r, 3.0f);
    // Arrow, like FL's slot menu triangle.
    juce::Path arrow;
    const auto cy = r.getCentreY();
    arrow.addTriangle (8.0f, cy - 4.5f, 8.0f, cy + 4.5f, 13.0f, cy);
    g.setColour (info.hasContent ? textColour.withAlpha (0.8f) : dimTextColour.withAlpha (0.6f));
    g.fillPath (arrow);
    // Name, or "Slot n" in italics when empty.
    auto nameArea = juce::Rectangle<float> (20.0f, 0.0f, (float) lfoArea().getX() - 22.0f, r.getHeight());
    // Latency badge: what this effect adds (amber; red from 10 ms, too late to play through), or LIVE OFF.
    if (info.loaded && info.reportedLatency > 0)
    {
        const auto text = info.liveBypassed ? juce::String ("LIVE OFF") : latencyText (info.reportedLatency).removeCharacters (" ");
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 6.0f;
        const auto badge = juce::Rectangle<float> (nameArea.getRight() - w, cy - 6.5f, w, 13.0f);
        const auto tint = info.liveBypassed ? dimTextColour : latencyMs (info.reportedLatency) >= 10.0 ? juce::Colour (0xffe8705c) : juce::Colour (0xffe8b35c);
        g.setColour (tint.withAlpha (0.18f)); g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (tint); g.drawText (text, badge, juce::Justification::centred);
        nameArea.setRight (badge.getX() - 3.0f);
    }
    if (info.name.isNotEmpty())
    {
        auto nameColour = info.status.isNotEmpty() ? juce::Colour (0xffe8a15c) : textColour;
        if (! info.enabled || info.liveBypassed) nameColour = nameColour.withAlpha (0.45f);
        g.setColour (nameColour);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (info.name + (info.pending ? juce::String (" ...") : juce::String()), nameArea.toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.85f);
    }
    else
    {
        g.setColour (dimTextColour.withAlpha (0.75f));
        g.setFont (juce::FontOptions (13.5f, juce::Font::italic));
        g.drawText ("Slot " + juce::String (slot + 1), nameArea, juce::Justification::centredLeft);
    }
    // LFO button: lit in the band colour while an LFO moves this knob.
    IconButton::drawIcon (g, IconButton::Icon::lfo, lfoArea().toFloat().withSizeKeepingCentre (14.0f, 9.0f),
                          info.modulated ? colour : dimTextColour.withAlpha (hover ? 0.7f : 0.35f));
    // Green on/off light.
    const auto light = ledArea().toFloat().withSizeKeepingCentre (9.0f, 9.0f);
    if (info.enabled)
    {
        g.setColour (ledColour.withAlpha (0.25f));
        g.fillEllipse (light.expanded (2.5f));
        g.setColour (ledColour);
    }
    else g.setColour (currentPalette.ledOff);
    g.fillEllipse (light);
}
void SlotRow::animate (double dt)
{
    bool modulated = false;
    const float live = processor.getSlotLiveMix (band, slot, modulated);
    // The audio updates the value once a block (about 90 times a second); the dot glides to it (a few ms behind)
    // so it moves on every frame of the screen instead of in steps.
    const float before = shownMix;
    if (! modulated || shownMix < 0.0f) shownMix = live;
    else shownMix += (live - shownMix) * (float) (1.0 - std::exp (-dt / 0.008));
    const bool changed = modulated != info.modulated || std::abs (shownMix - before) >= 0.02f;
    info.modulated = modulated;
    info.liveMix = live;
    if (changed) repaint (knob.getBounds().expanded (6));
}
void SlotRow::paintOverChildren (juce::Graphics& g)
{
    if (lifted)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.75f), 3.0f, 1.5f);
    }
    if (! info.modulated) return;
    // Where the LFO has the mix right now: a dot riding along the box's meter line.
    const auto meter = valueBoxMeter (knob.getBounds().toFloat(), false);
    const float value = juce::jlimit (0.0f, 1.0f, (shownMix < 0.0f ? info.liveMix : shownMix) / 100.0f);
    const auto dot = juce::Point<float> (meter.getX() + value * meter.getWidth(), meter.getCentreY());
    g.setColour (gridBackground);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot));
    g.setColour (currentPalette.marker);
    g.fillEllipse (juce::Rectangle<float> (5.5f, 5.5f).withCentre (dot));
}
void SlotRow::mouseDown (const juce::MouseEvent& e)
{
    if (auto* strip = findParentComponentOfClass<BandStrip>()) strip->slotDrag (slot, e, 0);
}
void SlotRow::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* strip = findParentComponentOfClass<BandStrip>()) strip->slotDrag (slot, e, 1);
}
void SlotRow::mouseUp (const juce::MouseEvent& e)
{
    if (auto* strip = findParentComponentOfClass<BandStrip>(); strip != nullptr && strip->slotDrag (slot, e, 2)) return;   // dropped
    if (! e.mouseWasClicked()) return;
    if (e.mods.isPopupMenu() || arrowArea().contains (e.getPosition())) { showMenu(); return; }
    if (ledArea().contains (e.getPosition())) { processor.setSlotEnabled (band, slot, ! info.enabled); return; }
    if (lfoArea().contains (e.getPosition())) { host.showLfo (band, slot); return; }
    if (info.loaded) processor.showEffect (band, slot);   // one click opens the effect
    else host.chooseEffect (band, slot, *this);
}
void SlotRow::showMenu()
{
    // A band's only, empty slot has nothing to manage: go straight to the effect list (unless a plug-in was
    // copied, which can be pasted into it).
    const auto copied = TriadProcessor::copiedEffectName();
    if (! info.hasContent && count <= 1 && copied.isEmpty()) { host.chooseEffect (band, slot, *this); return; }
    enum { menuOpen = 1, menuReplace, menuUp, menuDown, menuToggle, menuRemove, menuLfo, menuFavourite, menuCopyEffect, menuPasteEffect,
           menuTypeBase = 100 };
    juce::PopupMenu menu;
    menu.addSectionHeader (info.name.isNotEmpty() ? info.name : "Slot " + juce::String (slot + 1));
    if (info.hasContent) menu.addItem (menuOpen, "Open window", info.loaded);
    menu.addItem (menuReplace, info.hasContent ? "Replace..." : "Load effect...");
    menu.addItem (menuLfo, "Edit LFO for this mix knob...");
    const bool favourite = info.loaded && host.isFavourite (info.format, info.name);
    if (info.loaded) menu.addItem (menuFavourite, favourite ? u8 ("\xe2\x99\xa5 Remove from FAVORITES") : u8 ("\xe2\x99\xa1 Add to FAVORITES"));
    // A favorite can be listed under another type in FAVORITES (the plug-in says DELAY; you hear it as RHYTHM + TIME).
    const auto type = favourite ? host.getFavouriteType (info.format, info.name) : juce::String();
    if (type.isNotEmpty())
    {
        juce::PopupMenu types;
        types.addSectionHeader ("LIST IT IN FAVORITES UNDER");
        for (int i = 0; i < triad::effectTypes().size(); ++i)
            types.addItem (menuTypeBase + i, triad::effectTypes()[i], true, triad::effectTypes()[i] == type);
        addSubMenu (menu, "Favorite type", std::move (types), type);
    }
    menu.addSeparator();
    // Copy plugin state: the effect with every knob as it is now, to paste into a slot of any band.
    menu.addItem (menuCopyEffect, "Copy plugin state", info.loaded && ! info.pending);
    menu.addItem (menuPasteEffect, copied.isEmpty() ? juce::String ("Paste plugin state")
                                                    : (info.hasContent ? "Paste plugin state (" : "Paste plugin (") + copied + ")",
                  copied.isNotEmpty());
    menu.addSeparator();
    menu.addItem (menuUp, "Move up", slot > 0);
    menu.addItem (menuDown, "Move down", slot < count - 1);
    if (info.hasContent) menu.addItem (menuToggle, info.enabled ? "Bypass" : "Enable");
    menu.addSeparator();
    menu.addItem (menuRemove, count > 1 ? "Remove slot" : "Remove effect");
    juce::Component::SafePointer<SlotRow> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, favourite] (int choice)
    {
        if (safe == nullptr) return;
        auto& self = *safe;
        // Rows are rebuilt from the editor's timer, never inside these calls.
        switch (choice)
        {
            case menuOpen:      self.processor.showEffect (self.band, self.slot); break;
            case menuReplace:   self.host.chooseEffect (self.band, self.slot, self); break;
            case menuUp:        self.processor.moveSlot (self.band, self.slot, -1); break;
            case menuDown:      self.processor.moveSlot (self.band, self.slot, 1); break;
            case menuToggle:    self.processor.setSlotEnabled (self.band, self.slot, ! self.info.enabled); break;
            case menuRemove:    self.processor.removeSlot (self.band, self.slot); break;
            case menuLfo:       self.host.showLfo (self.band, self.slot); break;
            case menuFavourite: self.host.setFavourite (self.info.format, self.info.name, ! favourite); break;
            case menuCopyEffect: self.processor.copyEffectState (self.band, self.slot); break;
            case menuPasteEffect: self.processor.pasteEffectState (self.band, self.slot); break;
            default:
                if (choice >= menuTypeBase && choice < menuTypeBase + triad::effectTypes().size())
                    self.host.setFavouriteType (self.info.format, self.info.name, triad::effectTypes()[choice - menuTypeBase]);
                break;
        }
    });
}

// ---- "+" (add slot) -----------------------------------------------------------------------------
AddSlotButton::AddSlotButton (StripHost& h, int b, juce::Colour c) : host (h), band (b), colour (c)
{
    setTitle ("Add a slot to band " + juce::String (band + 1));
    setTooltip ("Add an effect slot to this band");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void AddSlotButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (hover ? slotRowHover : slotRow.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colour.withAlpha (hover ? 0.9f : 0.45f));
    const float dash[] = { 3.0f, 3.0f };
    juce::Path outline;
    outline.addRoundedRectangle (r, 3.0f);
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
    g.fillPath (dashed);
    g.setColour (hover ? textColour : dimTextColour);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("+", getLocalBounds().withWidth (getWidth() / 2 - 22), juce::Justification::centredRight);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("ADD SLOT", getLocalBounds().withTrimmedLeft (getWidth() / 2 - 16), juce::Justification::centredLeft);
}
void AddSlotButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
        host.chooseEffect (band, count, *this);
}

// ---- Band strip -----------------------------------------------------------------------------
BandStrip::BandStrip (TriadProcessor& p, StripHost& h, int b)
    : processor (p), host (h), band (b), colour (bandColour (b)), addButton (h, b, bandColour (b))
{
    setTitle (isPre() ? juce::String ("PRE") : isPost() ? juce::String ("POST") : "Band " + juce::String (band + 1));
    removeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    removeButton.setTooltip ("Remove this band (its range joins the neighbouring band)");
    // The editor rebuilds strips from its timer, so this strip is not deleted inside its own callback.
    removeButton.onClick = [this] { processor.removeBand (band); };
    if (! isExtra()) addAndMakeVisible (removeButton);
    menuButton.setTooltip (isExtra() ? (isPre() ? "PRE" : "POST") + juce::String (": paste a plug-in, remove its effects, hide it")
                                   : "Copy / paste band state, chain presets, LFOs, move or remove this band (or right-click the band)");
    menuButton.onClick = [this] { host.showBandMenu (band, menuButton); };
    addAndMakeVisible (menuButton);
    for (auto* label : { &startLabel, &endLabel })
    {
        label->setFont (juce::FontOptions (13.0f));
        if (! isExtra()) addAndMakeVisible (*label);
    }
    lookAndFeelChanged();
    startLabel.setJustificationType (juce::Justification::centredLeft);
    endLabel.setJustificationType (juce::Justification::centredRight);
    startLabel.onTextChange = [this] { if (auto hz = parseHz (startLabel.getText()); hz > 0.0f && band > 0) host.setCrossoverFrequency (band - 1, hz); };
    endLabel.onTextChange = [this] { if (auto hz = parseHz (endLabel.getText()); hz > 0.0f) host.setCrossoverFrequency (band, hz); };
    slotView.setViewedComponent (&slotHolder, false);
    slotView.setScrollBarsShown (true, false);
    slotView.setScrollBarThickness (6);
    addAndMakeVisible (slotView);
    slotHolder.addAndMakeVisible (addButton);
    slotHolder.addMouseListener (this, false);   // right-click the empty space under the slots: the band menu
    rebuildRows (processor.getSlotCount (band));
    if (isExtra()) return;   // PRE and POST have no band mix or volume: each slot has its own mix
    mix.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mix.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mix.setDoubleClickReturnValue (true, 100.0);
    mix.setColour (juce::Slider::rotarySliderFillColourId, colour);
    mix.setTitle ("Band " + juce::String (band + 1) + " mix");
    mix.getProperties().set ("triadLabel", "BAND MIX");   // drawn in its box
    mix.setTooltip ("Band mix: 0% is the unprocessed band, 100% is the band's full effect chain.");
    addAndMakeVisible (mix);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, TriadProcessor::bandMixID (band), mix);
    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setColour (juce::Slider::trackColourId, colour);
    volume.setTitle ("Band " + juce::String (band + 1) + " volume");
    volume.setTooltip ("Band volume. Double-click for 0 dB.");
    addAndMakeVisible (volume);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, TriadProcessor::bandGainID (band), volume);
    if (! isExtra())
    {
        // MUTE: hear only the bands you want (the band's effects keep playing, so unmuting is instant).
        muteButton.setClickingTogglesState (true);
        muteButton.setTooltip ("Mute this band: you hear only the other bands. Its effects keep running, so unmuting is instant.");
        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8705c));
        muteButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111312));
        muteButton.onStateChange = [this] { repaint(); };
        addAndMakeVisible (muteButton);
        muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, TriadProcessor::bandMuteID (band), muteButton);
        // SOLO (0.18.0, Thomas): hear only this band (and any other soloed one).
        soloButton.setClickingTogglesState (true);
        soloButton.setTooltip ("Solo this band: you hear only the soloed bands. The others keep running, so it is instant both ways.");
        soloButton.setColour (juce::TextButton::buttonOnColourId, soloColour);
        soloButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111312));
        soloButton.onStateChange = [this] { repaint(); };
        addAndMakeVisible (soloButton);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, TriadProcessor::bandSoloID (band), soloButton);
    }
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.onValueChange = [this] { repaint (0, getHeight() - footerHeight, getWidth(), footerHeight); };
}
void BandStrip::rebuildRows (int count)
{
    const bool grew = count > (int) rows.size() && ! rows.empty();
    rows.clear();
    for (int s = 0; s < count; ++s)
    {
        rows.push_back (std::make_unique<SlotRow> (processor, host, band, s, colour));
        slotHolder.addAndMakeVisible (*rows.back());
    }
    addButton.setSlotCount (count);
    layoutRows();
    repaint();
    if (grew)   // show the slot that was just added
        slotView.setViewPosition (0, juce::jmax (0, slotHolder.getHeight() - slotView.getHeight()));
}
void BandStrip::layoutRows()
{
    const int width = slotView.getWidth() - (slotView.isVerticalScrollBarShown() ? 8 : 0);
    const int step = rowHeight + rowGap;
    // While a slot is dragged, the others make room where it would land.
    const bool dragging = slotDragState.active && slotDragState.slot >= 0 && slotDragState.slot < (int) rows.size();
    int position = 0;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        if (dragging && i == slotDragState.slot) continue;
        if (dragging && position == slotDragState.target) ++position;
        rows[(size_t) i]->setBounds (0, position * step, juce::jmax (40, width), rowHeight);
        ++position;
    }
    if (dragging) rows[(size_t) slotDragState.slot]->setSize (juce::jmax (40, width), rowHeight);
    const int y = (int) rows.size() * step;
    addButton.setBounds (0, y, juce::jmax (40, width), addHeight);
    slotHolder.setSize (juce::jmax (40, width), y + addHeight + 2);
}
void BandStrip::refreshRows()
{
    const int count = processor.getSlotCount (band);
    for (int s = 0; s < juce::jmin (count, (int) rows.size()); ++s) rows[(size_t) s]->update (processor.getSlotInfo (band, s), count);
}
bool BandStrip::slotDrag (int slot, const juce::MouseEvent& e, int phase)
{
    if (slot < 0 || slot >= (int) rows.size()) return false;
    auto& row = *rows[(size_t) slot];
    const float y = e.getEventRelativeTo (&slotHolder).position.y;
    if (phase == 0)
    {
        slotDragState = {};
        if (! e.mods.isPopupMenu() && e.mods.isLeftButtonDown() && rows.size() > 1)
            slotDragState = { slot, y, row.getY(), false, slot };
        return false;
    }
    if (slotDragState.slot != slot) return false;
    if (phase == 1)
    {
        if (! slotDragState.active && std::abs (y - slotDragState.startY) < 5.0f) return false;
        if (! slotDragState.active) { slotDragState.active = true; row.setLifted (true); row.toFront (false); }
        const int step = rowHeight + rowGap;
        const int top = juce::jlimit (0, ((int) rows.size() - 1) * step, slotDragState.startTop + juce::roundToInt (y - slotDragState.startY));
        row.setTopLeftPosition (0, top);
        slotDragState.target = juce::jlimit (0, (int) rows.size() - 1, juce::roundToInt ((float) top / (float) step));
        layoutRows();
        const auto inView = e.getEventRelativeTo (&slotView).getPosition();
        slotView.autoScroll (inView.x, inView.y, 24, 8);
        return true;
    }
    const bool dropped = slotDragState.active;
    const int from = slotDragState.slot, to = slotDragState.target;
    if (dropped) row.setLifted (false);
    slotDragState = {};
    if (dropped && to != from && to >= 0) processor.moveSlotTo (band, from, to);
    layoutRows();
    if (dropped) refreshRows();   // the rows show the slots in their new order right away
    return dropped;
}
void BandStrip::update (const std::vector<float>& crossovers, int bandCount, float nyquist)
{
    updateEdges (crossovers, bandCount, nyquist);
    removeButton.setVisible (bandCount > 1 && ! isExtra());
    if (slotDragState.active) return;   // the rows stay as they are until the dragged slot is dropped
    const int count = processor.getSlotCount (band);
    if (count != (int) rows.size())
    {
        // Keep the scroll position unless rows were added (then the new slot is scrolled into view).
        const auto position = slotView.getViewPosition();
        const bool grew = count > (int) rows.size();
        rebuildRows (count);
        if (! grew) slotView.setViewPosition (position);
    }
    int chain = 0;
    for (int s = 0; s < (int) rows.size(); ++s)
    {
        const auto info = processor.getSlotInfo (band, s);
        chain += info.latency;
        rows[(size_t) s]->update (info, count);
    }
    if (chain != chainLatency) { chainLatency = chain; repaint(); }
    const bool silent = ! isExtra() && processor.isAnyBandSoloed() && ! processor.isBandSoloed (band);
    if (silent != shownSilent) { shownSilent = silent; repaint(); }
}
// The band's edges (they move with the dividers: every frame, so automation moves them smoothly).
void BandStrip::updateEdges (const std::vector<float>& crossovers, int bandCount, float nyquist)
{
    if (! isExtra() && band < (int) crossovers.size() + 1)
    {
        startHz = band == 0 ? 0.0f : crossovers[(size_t) band - 1];
        endHz = band < (int) crossovers.size() ? crossovers[(size_t) band] : nyquist;
    }
    // Only band edges that are real crossovers can be typed in (0 Hz and Nyquist are fixed).
    startLabel.setEditable (band > 0, band > 0, false);
    endLabel.setEditable (band < bandCount - 1, band < bandCount - 1, false);
    if (! startLabel.isBeingEdited()) startLabel.setText (formatHz (startHz), juce::dontSendNotification);
    if (! endLabel.isBeingEdited()) endLabel.setText (formatHz (endHz), juce::dontSendNotification);
    startLabel.setTooltip (band > 0 ? "Click to type the band's lower edge (e.g. 250 or 2.5k)" : "The lowest band starts at 0 Hz");
    endLabel.setTooltip (band < bandCount - 1 ? "Click to type the band's upper edge" : "The highest band ends at Nyquist (half the sample rate)");
}
// Every frame (up to 120 a second): the level meter and the LFO dots on the slot knobs.
void BandStrip::animate (float newLevel, double dt)
{
    // Fast attack, gentle fall (about -30 dB a second), peak hold for 0.75 s, timed by the frame clock.
    if (isExtra()) { for (auto& row : rows) row->animate (dt); return; }   // no band meter
    const float before = level, holdBefore = peakHold;
    level = juce::jmax (newLevel, level * (float) std::pow (0.7, dt * 20.0));
    if (newLevel >= peakHold) { peakHold = newLevel; holdTime = 0.75; }
    else if ((holdTime -= dt) < 0.0) peakHold *= (float) std::pow (0.85, dt * 20.0);
    if (level < 1.0e-4f) level = 0.0f;
    if (peakHold < 1.0e-4f) peakHold = 0.0f;
    if (std::abs (level - before) > 1.0e-5f || std::abs (peakHold - holdBefore) > 1.0e-5f)
        repaint (meterArea().expanded (4).getUnion (peakTextArea()));
    for (auto& row : rows) row->animate (dt);
}
juce::Rectangle<int> BandStrip::meterArea() const
{
    // Under the volume slider, as long as its track.
    const int footerTop = getHeight() - footerHeight;
    return { volume.getX() + 7, footerTop + 50, juce::jmax (10, volume.getWidth() - 14), 6 };
}
juce::Rectangle<int> BandStrip::peakTextArea() const
{
    const auto m = meterArea();
    return { m.getRight() - 60, m.getBottom() + 6, 60, 16 };
}
void BandStrip::resized()
{
    const int w = getWidth();
    removeButton.setBounds (w - 32, 8, 22, 22);
    menuButton.setBounds (w - 60, 8, 26, 22);
    startLabel.setBounds (8, 31, w / 2 - 12, 22);
    endLabel.setBounds (w / 2 + 4, 31, w / 2 - 12, 22);
    slotView.setBounds (6, headerHeight, w - 12, getHeight() - headerHeight - footerHeight);
    layoutRows();
    layoutRows();   // again, now that the viewport knows whether it needs a scroll bar
    const int footerTop = getHeight() - footerHeight;
    mix.setBounds (w - 94, footerTop + 8, 84, 76);
    volume.setBounds (10, footerTop + 26, w - 114, 18);
    // MUTE and SOLO side by side (M and S in a narrow strip), the slot count after them.
    const int buttonWidth = w < 250 ? 26 : 50;
    muteButton.setButtonText (w < 250 ? "M" : "MUTE");
    soloButton.setButtonText (w < 250 ? "S" : "SOLO");
    muteButton.setBounds (12, footerTop + 62, buttonWidth, 22);
    soloButton.setBounds (muteButton.getRight() + 4, footerTop + 62, buttonWidth, 22);
}
void BandStrip::lookAndFeelChanged()
{
    // The frequency labels' colours follow the theme.
    for (auto* label : { &startLabel, &endLabel })
    {
        label->setColour (juce::Label::textColourId, currentPalette.softText);
        label->setColour (juce::Label::textWhenEditingColourId, textColour);
        label->setColour (juce::Label::backgroundWhenEditingColourId, currentPalette.field);
        label->setColour (juce::Label::outlineWhenEditingColourId, colour);
    }
}
void BandStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (panel); g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (colour.withAlpha (0.75f)); g.fillRoundedRectangle (12.0f, 11.0f, 4.0f, 17.0f, 2.0f);
    g.setColour (colour); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (isPre() ? juce::String ("PRE") : isPost() ? juce::String ("POST") : "BAND " + juce::String (band + 1), 23, 8, getWidth() - 90, 24,
                juce::Justification::centredLeft);
    g.setColour (currentPalette.faintText); g.setFont (juce::FontOptions (12.0f));
    if (isExtra())
    {
        g.setColour (currentPalette.softText); g.setFont (juce::FontOptions (12.0f));
        g.drawText (isPre() ? "Full range, before the bands" : "Full range, after the bands", 10, 31, getWidth() - 20, 22, juce::Justification::centredLeft);
    }
    else g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), getWidth() / 2 - 10, 31, 20, 22, juce::Justification::centred);
    g.setColour (colour.withAlpha (0.45f));
    g.drawHorizontalLine (headerHeight - 4, 10.0f, (float) getWidth() - 10.0f);
    const int footerTop = getHeight() - footerHeight;
    g.drawHorizontalLine (footerTop + 2, 10.0f, (float) getWidth() - 10.0f);
    const int left = 12, right = getWidth() - 106;   // the volume's value ends before the BAND MIX box
    if (isExtra())
    {
        // PRE / POST: what it is for, how many slots, and its latency (it adds to every band's).
        g.setColour (dimTextColour); g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (isPre() ? "Runs on the whole sound first: pitch correction, a de-esser, a gate..."
                                  : "Runs on the whole sound last: a limiter, glue compression, a final EQ...",
                          left, footerTop + 8, getWidth() - 2 * left, 32, juce::Justification::topLeft, 2);
        const int n = (int) rows.size();
        g.drawText (juce::String (n) + (n == 1 ? " slot" : " slots"), left, footerTop + 62, 80, 16, juce::Justification::centredLeft);
        if (chainLatency > 0)
        {
            const double ms = 1000.0 * chainLatency / juce::jmax (1.0, processor.getSampleRateForDisplay());
            g.setColour (ms >= 10.0 ? juce::Colour (0xffe8705c) : juce::Colour (0xffe8b35c));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (formatMs (ms) + " latency (adds to every band)", left, footerTop + 74, getWidth() - 2 * left, 14, juce::Justification::centredLeft);
        }
        return;
    }
    // Volume: label and value, slider (a component), then the level meter.
    g.setColour (dimTextColour); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("VOLUME", left, footerTop + 8, 80, 16, juce::Justification::centredLeft);
    const auto db = (float) volume.getValue();
    g.setColour (textColour); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ((db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB", right - 80, footerTop + 8, 80, 16, juce::Justification::centredRight);
    const auto meter = meterArea().toFloat();
    g.setColour (gridBackground); g.fillRoundedRectangle (meter, 2.0f);
    auto position = [&meter] (float gain)
    {
        const float dbValue = juce::jlimit (-60.0f, 6.0f, juce::Decibels::gainToDecibels (gain, -60.0f));
        return meter.getX() + meter.getWidth() * (dbValue + 60.0f) / 66.0f;
    };
    if (level > 0.001f)
    {
        g.setColour (level > 1.0f ? juce::Colour (0xffe8705c) : colour.withAlpha (0.85f));
        g.fillRoundedRectangle (meter.withRight (position (level)), 2.0f);
    }
    if (peakHold > 0.001f)
    {
        g.setColour (peakHold > 1.0f ? juce::Colour (0xffe8705c) : textColour);
        g.fillRect (position (peakHold) - 1.0f, meter.getY() - 1.0f, 2.0f, meter.getHeight() + 2.0f);
    }
    g.setColour (dimTextColour); g.setFont (juce::FontOptions (10.0f));
    g.drawText (formatDb (peakHold), peakTextArea(), juce::Justification::centredRight);
    // Under the meter: MUTE and SOLO (buttons), then the slot count and the band's latency beside them (where there
    // is room before the peak level).
    const int n = (int) rows.size();
    const int info = soloButton.getRight() + 8;
    const auto count = juce::String (n) + (n == 1 ? " slot" : " slots");
    const float peakLeft = (float) peakTextArea().getRight() - juce::GlyphArrangement::getStringWidth (juce::FontOptions (10.0f), formatDb (peakHold)) - 6.0f;
    if ((float) info + juce::GlyphArrangement::getStringWidth (juce::FontOptions (10.0f), count) < peakLeft)
        g.drawText (count, info, footerTop + 62, 80, 16, juce::Justification::centredLeft);
    if (chainLatency > 0)
    {
        // The band's latency (its effects added up); the slowest band sets Triad's total.
        const double ms = 1000.0 * chainLatency / juce::jmax (1.0, processor.getSampleRateForDisplay());
        g.setColour (ms >= 10.0 ? juce::Colour (0xffe8705c) : juce::Colour (0xffe8b35c));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (formatMs (ms) + " latency", info, footerTop + 74, 90, 14, juce::Justification::centredLeft);
    }
    // A muted, soloed or (another band soloed) silent band: its title says so.
    const auto state = muteButton.getToggleState() ? juce::String ("MUTED") : soloButton.getToggleState() ? juce::String ("SOLO")
                     : shownSilent ? juce::String ("SILENT") : juce::String();
    if (state.isNotEmpty())
    {
        g.setColour (muteButton.getToggleState() ? juce::Colour (0xffe8705c) : soloButton.getToggleState() ? soloColour : dimTextColour);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (state, getWidth() - 110, 8, 46, 24, juce::Justification::centredRight);
    }
}
void BandStrip::paintOverChildren (juce::Graphics& g)
{
    if (! lifted) return;
    g.setColour (colour);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 7.0f, 2.0f);
}
void BandStrip::setLifted (bool on)
{
    lifted = on;
    setAlpha (on ? 0.9f : 1.0f);
    repaint();
}
// Press and drag the header to move this band's chain to another band.
// Right-click anywhere on the band (header, footer or the space under the slots): the band menu.
void BandStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { host.showBandMenu (band, *this, true); return; }
    if (! isExtra() && e.eventComponent == this && e.y < headerHeight && e.mods.isLeftButtonDown()) host.bandDrag (band, e, 0);
}
void BandStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (! isExtra() && e.eventComponent == this && e.mouseDownPosition.y < headerHeight && ! e.mods.isPopupMenu()) host.bandDrag (band, e, 1);
}
void BandStrip::mouseUp (const juce::MouseEvent& e)
{
    if (! isExtra() && e.eventComponent == this && e.mouseDownPosition.y < headerHeight && ! e.mods.isPopupMenu()) host.bandDrag (band, e, 2);
}
