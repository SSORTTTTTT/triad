#include "BandStrip.h"

using namespace triad::ui;

namespace
{
constexpr int headerHeight = 58, rowHeight = 30, rowGap = 2;
}

// ---- Slot row -------------------------------------------------------------------------------
SlotRow::SlotRow (TriadProcessor& p, StripHost& h, int b, int s, juce::Colour c)
    : processor (p), host (h), band (b), slot (s), colour (c)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setDoubleClickReturnValue (true, 100.0);
    knob.setColour (juce::Slider::rotarySliderFillColourId, colour);
    knob.setTitle ("Band " + juce::String (band + 1) + " slot " + juce::String (slot + 1) + " mix");
    knob.setPopupDisplayEnabled (false, false, nullptr);
    addAndMakeVisible (knob);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, TriadProcessor::slotMixID (band, slot), knob);
    setTitle ("Band " + juce::String (band + 1) + " slot " + juce::String (slot + 1));
}
void SlotRow::update (const TriadProcessor::SlotInfo& next)
{
    if (next.name == info.name && next.status == info.status && next.loaded == info.loaded && next.enabled == info.enabled
        && next.pending == info.pending && next.hasContent == info.hasContent && next.format == info.format)
        return;
    info = next;
    const auto label = info.name.isNotEmpty() ? info.name : "Slot " + juce::String (slot + 1);
    setTooltip (info.loaded ? label + " (" + info.format + ")  -  click to open, arrow for options"
                            : info.hasContent ? label + "  -  " + (info.pending ? juce::String ("loading...") : info.status)
                                              : "Slot " + juce::String (slot + 1) + "  -  click to load an effect");
    knob.setTooltip ((info.name.isNotEmpty() ? info.name : "Slot " + juce::String (slot + 1)) + " mix (dry/wet). Double-click for 100%.");
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
    const auto nameArea = juce::Rectangle<float> (20.0f, 0.0f, (float) knob.getX() - 24.0f, r.getHeight());
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
void SlotRow::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    if (e.mods.isPopupMenu() || arrowArea().contains (e.getPosition())) { showMenu(); return; }
    if (ledArea().contains (e.getPosition())) { processor.setSlotEnabled (band, slot, ! info.enabled); return; }
    if (info.loaded) processor.showEffect (band, slot);   // one click opens the effect
    else host.chooseEffect (band, slot, *this);
}
void SlotRow::showMenu()
{
    if (! info.hasContent) { host.chooseEffect (band, slot, *this); return; }
    enum { menuOpen = 1, menuReplace, menuUp, menuDown, menuToggle, menuRemove };
    juce::PopupMenu menu;
    menu.addSectionHeader (info.name.isNotEmpty() ? info.name : "Slot " + juce::String (slot + 1));
    menu.addItem (menuOpen, "Open window", info.loaded);
    menu.addItem (menuReplace, "Replace...");
    menu.addSeparator();
    menu.addItem (menuUp, "Move up", slot > 0);
    menu.addItem (menuDown, "Move down", slot < triad::maxSlots - 1);
    menu.addItem (menuToggle, info.enabled ? "Bypass" : "Enable");
    menu.addSeparator();
    menu.addItem (menuRemove, "Remove");
    juce::Component::SafePointer<SlotRow> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int choice)
    {
        if (safe == nullptr) return;
        auto& self = *safe;
        switch (choice)
        {
            case menuOpen:    self.processor.showEffect (self.band, self.slot); break;
            case menuReplace: self.host.chooseEffect (self.band, self.slot, self); break;
            case menuUp:      self.processor.moveSlot (self.band, self.slot, -1); break;
            case menuDown:    self.processor.moveSlot (self.band, self.slot, 1); break;
            case menuToggle:  self.processor.setSlotEnabled (self.band, self.slot, ! self.info.enabled); break;
            case menuRemove:  self.processor.removeEffect (self.band, self.slot); break;
            default: break;
        }
    });
}

// ---- Band strip -----------------------------------------------------------------------------
BandStrip::BandStrip (TriadProcessor& p, StripHost& h, int b) : processor (p), host (h), band (b), colour (bandColour (b))
{
    setTitle ("Band " + juce::String (band + 1));
    removeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    removeButton.setTooltip ("Remove this band (its range joins the neighbouring band)");
    // The editor rebuilds strips from its timer, so this strip is not deleted inside its own callback.
    removeButton.onClick = [this] { processor.removeBand (band); };
    addAndMakeVisible (removeButton);
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
    for (int s = 0; s < triad::maxSlots; ++s)
    {
        rows[(size_t) s] = std::make_unique<SlotRow> (p, h, band, s, colour);
        addAndMakeVisible (*rows[(size_t) s]);
    }
    mix.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mix.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mix.setDoubleClickReturnValue (true, 100.0);
    mix.setColour (juce::Slider::rotarySliderFillColourId, colour);
    mix.setTitle ("Band " + juce::String (band + 1) + " mix");
    mix.setTooltip ("Band mix: 0% is the unprocessed band, 100% is the band's full effect chain.");
    addAndMakeVisible (mix);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, TriadProcessor::bandMixID (band), mix);
}
void BandStrip::update (const std::vector<float>& crossovers, int bandCount, float nyquist)
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
    for (int s = 0; s < triad::maxSlots; ++s) rows[(size_t) s]->update (processor.getSlotInfo (band, s));
}
void BandStrip::resized()
{
    const int w = getWidth();
    removeButton.setBounds (w - 32, 8, 22, 22);
    startLabel.setBounds (8, 31, w / 2 - 12, 22);
    endLabel.setBounds (w / 2 + 4, 31, w / 2 - 12, 22);
    for (int s = 0; s < triad::maxSlots; ++s)
        rows[(size_t) s]->setBounds (6, headerHeight + s * (rowHeight + rowGap), w - 12, rowHeight);
    const int footerTop = headerHeight + triad::maxSlots * (rowHeight + rowGap) + 4;
    mix.setBounds (w - 82, footerTop + 8, 66, 66);
}
void BandStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (panel); g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (colour.withAlpha (0.75f)); g.fillRoundedRectangle (12.0f, 11.0f, 4.0f, 17.0f, 2.0f);
    g.setColour (colour); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("BAND " + juce::String (band + 1), 23, 8, getWidth() - 60, 24, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff778275)); g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), getWidth() / 2 - 10, 31, 20, 22, juce::Justification::centred);
    g.setColour (colour.withAlpha (0.45f));
    g.drawHorizontalLine (headerHeight - 4, 10.0f, (float) getWidth() - 10.0f);
    const int footerTop = headerHeight + triad::maxSlots * (rowHeight + rowGap) + 4;
    g.setColour (juce::Colour (0xffa5afa2)); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("BAND MIX", 12, footerTop + 20, 120, 20, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.0f)); g.setColour (juce::Colour (0xff8a948a));
    g.drawText ("Dry 0%  /  Wet 100%", 12, footerTop + 42, 130, 18, juce::Justification::centredLeft);
}
