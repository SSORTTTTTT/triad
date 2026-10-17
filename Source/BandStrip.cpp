#include "BandStrip.h"
#include "Settings.h"

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
void SlotRow::update (const TriadProcessor::SlotInfo& next, int slotCount)
{
    if (! knob.isMouseButtonDown() && std::abs (knob.getValue() - next.mix) > 0.001)
        knob.setValue (next.mix, juce::dontSendNotification);
    const bool liveChanged = std::abs (next.liveMix - info.liveMix) > 0.2f || next.modulated != info.modulated;
    if (! first && slotCount == count && next.name == info.name && next.status == info.status && next.loaded == info.loaded
        && next.enabled == info.enabled && next.pending == info.pending && next.hasContent == info.hasContent && next.format == info.format)
    {
        if (liveChanged) { info.liveMix = next.liveMix; info.modulated = next.modulated; repaint(); }
        return;
    }
    first = false;
    count = slotCount;
    info = next;
    const auto label = info.name.isNotEmpty() ? info.name : "Slot " + juce::String (slot + 1);
    setTooltip (info.loaded ? label + " (" + info.format + ")  -  click to open, arrow for options"
                            : info.hasContent ? label + "  -  " + (info.pending ? juce::String ("loading...") : info.status)
                                              : "Slot " + juce::String (slot + 1) + "  -  click to load an effect");
    knob.setTooltip (label + " mix (dry/wet). Double-click for 100%.");
    knob.setAlpha (info.loaded && info.enabled ? 1.0f : 0.55f);
    repaint();
}
void SlotRow::resized()
{
    const int size = getHeight() - 4;
    knob.setBounds (getWidth() - 22 - size, 2, size, size);
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
    const auto nameArea = juce::Rectangle<float> (20.0f, 0.0f, (float) lfoArea().getX() - 22.0f, r.getHeight());
    if (info.name.isNotEmpty())
    {
        auto nameColour = info.status.isNotEmpty() ? juce::Colour (0xffe8a15c) : textColour;
        if (! info.enabled) nameColour = nameColour.withAlpha (0.45f);
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
    else g.setColour (juce::Colour (0xff4a504c));
    g.fillEllipse (light);
}
void SlotRow::paintOverChildren (juce::Graphics& g)
{
    if (! info.modulated) return;
    // Where the LFO has the mix right now: a dot riding round the knob.
    const auto k = knob.getBounds().toFloat();
    const auto params = knob.getRotaryParameters();
    const float angle = params.startAngleRadians + (info.liveMix / 100.0f) * (params.endAngleRadians - params.startAngleRadians);
    const float radius = k.getWidth() * 0.5f + 1.0f;
    const auto centre = k.getCentre();
    const auto dot = centre.getPointOnCircumference (radius, angle);
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (dot));
}
void SlotRow::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    if (e.mods.isPopupMenu() || arrowArea().contains (e.getPosition())) { showMenu(); return; }
    if (ledArea().contains (e.getPosition())) { processor.setSlotEnabled (band, slot, ! info.enabled); return; }
    if (lfoArea().contains (e.getPosition())) { host.showLfo (band, slot); return; }
    if (info.loaded) processor.showEffect (band, slot);   // one click opens the effect
    else host.chooseEffect (band, slot, *this);
}
void SlotRow::showMenu()
{
    // A band's only, empty slot has nothing to manage: go straight to the effect list.
    if (! info.hasContent && count <= 1) { host.chooseEffect (band, slot, *this); return; }
    enum { menuOpen = 1, menuReplace, menuUp, menuDown, menuToggle, menuRemove, menuLfo, menuFavourite };
    juce::PopupMenu menu;
    menu.addSectionHeader (info.name.isNotEmpty() ? info.name : "Slot " + juce::String (slot + 1));
    if (info.hasContent) menu.addItem (menuOpen, "Open window", info.loaded);
    menu.addItem (menuReplace, info.hasContent ? "Replace..." : "Load effect...");
    menu.addItem (menuLfo, "Edit LFO for this mix knob...");
    const bool favourite = info.loaded && host.isFavourite (info.format, info.name);
    if (info.loaded) menu.addItem (menuFavourite, favourite ? "Remove from favourites" : "Add to favourites");
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
            default: break;
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
    setTitle ("Band " + juce::String (band + 1));
    removeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    removeButton.setTooltip ("Remove this band (its range joins the neighbouring band)");
    // The editor rebuilds strips from its timer, so this strip is not deleted inside its own callback.
    removeButton.onClick = [this] { processor.removeBand (band); };
    addAndMakeVisible (removeButton);
    menuButton.setTooltip ("Copy / paste band state, chain presets, LFOs, move or remove this band (or right-click the band)");
    menuButton.onClick = [this] { host.showBandMenu (band, menuButton); };
    addAndMakeVisible (menuButton);
    for (auto* label : { &startLabel, &endLabel })
    {
        label->setFont (juce::FontOptions (13.0f));
        label->setColour (juce::Label::textColourId, juce::Colour (0xffbac4b8));
        label->setColour (juce::Label::textWhenEditingColourId, textColour);
        label->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff0d0f0e));
        label->setColour (juce::Label::outlineWhenEditingColourId, colour);
        addAndMakeVisible (*label);
    }
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
    mix.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mix.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mix.setDoubleClickReturnValue (true, 100.0);
    mix.setColour (juce::Slider::rotarySliderFillColourId, colour);
    mix.setTitle ("Band " + juce::String (band + 1) + " mix");
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
    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds (0, y, juce::jmax (40, width), rowHeight);
        y += rowHeight + rowGap;
    }
    addButton.setBounds (0, y, juce::jmax (40, width), addHeight);
    slotHolder.setSize (juce::jmax (40, width), y + addHeight + 2);
}
void BandStrip::update (const std::vector<float>& crossovers, int bandCount, float nyquist, float newLevel)
{
    startHz = band == 0 ? 0.0f : crossovers[(size_t) band - 1];
    endHz = band < (int) crossovers.size() ? crossovers[(size_t) band] : nyquist;
    // Only band edges that are real crossovers can be typed in (0 Hz and Nyquist are fixed).
    startLabel.setEditable (band > 0, band > 0, false);
    endLabel.setEditable (band < bandCount - 1, band < bandCount - 1, false);
    if (! startLabel.isBeingEdited()) startLabel.setText (formatHz (startHz), juce::dontSendNotification);
    if (! endLabel.isBeingEdited()) endLabel.setText (formatHz (endHz), juce::dontSendNotification);
    startLabel.setTooltip (band > 0 ? "Click to type the band's lower edge (e.g. 250 or 2.5k)" : "The lowest band starts at 0 Hz");
    endLabel.setTooltip (band < bandCount - 1 ? "Click to type the band's upper edge" : "The highest band ends at Nyquist (half the sample rate)");
    removeButton.setVisible (bandCount > 1);
    const int count = processor.getSlotCount (band);
    if (count != (int) rows.size())
    {
        // Keep the scroll position unless rows were added (then the new slot is scrolled into view).
        const auto position = slotView.getViewPosition();
        const bool grew = count > (int) rows.size();
        rebuildRows (count);
        if (! grew) slotView.setViewPosition (position);
    }
    for (int s = 0; s < (int) rows.size(); ++s) rows[(size_t) s]->update (processor.getSlotInfo (band, s), count);
    // Level meter: fast attack, gentle fall, peak hold.
    level = juce::jmax (newLevel, level * 0.7f);
    if (newLevel >= peakHold) { peakHold = newLevel; holdFrames = 15; }
    else if (--holdFrames < 0) peakHold *= 0.85f;
    repaint (0, getHeight() - footerHeight, getWidth(), footerHeight);
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
    mix.setBounds (w - 76, footerTop + 8, 62, 62);
    volume.setBounds (10, footerTop + 26, w - 96, 18);
}
void BandStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (panel); g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (colour.withAlpha (0.75f)); g.fillRoundedRectangle (12.0f, 11.0f, 4.0f, 17.0f, 2.0f);
    g.setColour (colour); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("BAND " + juce::String (band + 1), 23, 8, getWidth() - 90, 24, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff778275)); g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), getWidth() / 2 - 10, 31, 20, 22, juce::Justification::centred);
    g.setColour (colour.withAlpha (0.45f));
    g.drawHorizontalLine (headerHeight - 4, 10.0f, (float) getWidth() - 10.0f);
    const int footerTop = getHeight() - footerHeight;
    g.drawHorizontalLine (footerTop + 2, 10.0f, (float) getWidth() - 10.0f);
    // Volume: label and value, slider (a component), then the level meter.
    const int left = 12, right = getWidth() - 88;
    g.setColour (juce::Colour (0xffa5afa2)); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("VOLUME", left, footerTop + 8, 80, 16, juce::Justification::centredLeft);
    const auto db = (float) volume.getValue();
    g.setColour (textColour); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ((db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB", right - 80, footerTop + 8, 80, 16, juce::Justification::centredRight);
    const auto meter = juce::Rectangle<float> ((float) left, (float) footerTop + 50.0f, (float) (right - left - 44), 6.0f);
    g.setColour (juce::Colour (0xff111312)); g.fillRoundedRectangle (meter, 2.0f);
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
    g.drawText (formatDb (peakHold), (int) meter.getRight() + 2, (int) meter.getY() - 5, 44, 16, juce::Justification::centredRight);
    const int n = (int) rows.size();
    g.drawText (juce::String (n) + (n == 1 ? " slot" : " slots"), left, footerTop + 64, 80, 16, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffa5afa2)); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("BAND MIX", getWidth() - 80, footerTop + 70, 70, 16, juce::Justification::centred);
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
    if (e.eventComponent == this && e.y < headerHeight && e.mods.isLeftButtonDown()) host.bandDrag (band, e, 0);
}
void BandStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && e.mouseDownPosition.y < headerHeight && ! e.mods.isPopupMenu()) host.bandDrag (band, e, 1);
}
void BandStrip::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && e.mouseDownPosition.y < headerHeight && ! e.mods.isPopupMenu()) host.bandDrag (band, e, 2);
}
