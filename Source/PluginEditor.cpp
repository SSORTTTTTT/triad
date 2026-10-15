#include "PluginEditor.h"

using namespace triad::ui;

namespace
{
constexpr int editorWidth = 1000, editorHeight = 786, margin = 24, stripGap = 10, minStripWidth = 196, maxStripWidth = 420;
enum MenuIds { scanningId = 1000001, noneId, browseId, refreshId, rescanId };
}

TriadLook::TriadLook()
{
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303331));
    setColour (juce::TextButton::textColourOffId, textColour);
    setColour (juce::Label::textColourId, dimTextColour);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff202420));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3d4a36));
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff111312));
    setColour (juce::TooltipWindow::textColourId, textColour);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff4a504c));
}
void TriadLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& slider)
{
    const bool small = juce::jmin (w, h) < 40;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (small ? 1.5f : 5.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float stroke = small ? 2.5f : 4.5f;
    const auto centre = bounds.getCentre();
    // Dark body like FL's slot knobs, with the value arc in the band colour around it.
    g.setColour (juce::Colour (0xff131615));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    const auto arcRadius = radius - stroke * 0.5f;
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0, start, end, true);
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0, start, start + pos * (end - start), true);
    g.setColour (juce::Colour (0xff3b413a)); g.strokePath (track, juce::PathStrokeType (stroke));
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId)); g.strokePath (value, juce::PathStrokeType (stroke));
    // The percentage, inside the circle.
    const auto v = slider.getValue();
    const auto label = (v >= 99.95 ? juce::String ("100") : juce::String (juce::roundToInt (v))) + "%";
    g.setColour (textColour.withAlpha (slider.isEnabled() ? 0.95f : 0.5f));
    g.setFont (small ? juce::FontOptions (8.5f, juce::Font::bold) : juce::FontOptions (juce::jmax (11.0f, radius * 0.46f), juce::Font::bold));
    const auto inner = juce::Rectangle<float> ((arcRadius - stroke) * 1.9f, radius).withCentre (centre).toNearestInt();
    g.drawFittedText (label, inner, juce::Justification::centred, 1, 0.75f);
}

TriadEditor::TriadEditor (TriadProcessor& p) : AudioProcessorEditor (p), processor (p), frequencyDisplay (p)
{
    setLookAndFeel (&look);
    setSize (editorWidth, editorHeight);
    addAndMakeVisible (frequencyDisplay);
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    status.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (status);
    catalog->addChangeListener (this);
    rebuildStrips();
    timerCallback();
    startTimerHz (10);
}
TriadEditor::~TriadEditor()
{
    stopTimer();
    catalog->removeChangeListener (this);
    processor.closeWindows();
    setLookAndFeel (nullptr);
}
void TriadEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colour (0xff303235)); g.fillRect (0, 0, getWidth(), 56);
    g.setColour (juce::Colour (0xff9bd85c)); g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("TRIAD", margin, 9, 150, 38, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffaeb6ab)); g.setFont (juce::FontOptions (12.0f));
    g.drawText ("MULTIBAND EFFECT HOST", 168, 15, 360, 26, juce::Justification::centredLeft);
    g.drawText ("VST3 + AU EFFECTS  /  0.2.0", getWidth() - 300, 15, 276, 26, juce::Justification::centredRight);
}
void TriadEditor::resized()
{
    frequencyDisplay.setBounds (margin, 66, getWidth() - 2 * margin, 204);
    viewport.setBounds (margin, 280, getWidth() - 2 * margin, BandStrip::preferredHeight + 10);
    status.setBounds (margin, getHeight() - 30, getWidth() - 2 * margin, 24);
    layoutStrips();
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
    layoutStrips();
    frequencyDisplay.refresh();
}
void TriadEditor::layoutStrips()
{
    const int available = viewport.getWidth();
    const int n = juce::jmax (1, (int) strips.size());
    const int width = juce::jlimit (minStripWidth, maxStripWidth, (available - stripGap * (n - 1)) / n);
    const int total = n * width + stripGap * (n - 1);
    stripHolder.setSize (juce::jmax (total, available), BandStrip::preferredHeight);
    for (int i = 0; i < (int) strips.size(); ++i)
        strips[(size_t) i]->setBounds (i * (width + stripGap), 0, width, BandStrip::preferredHeight);
}
void TriadEditor::timerCallback()
{
    if (processor.getBandCount() != builtCount) rebuildStrips();
    const auto cuts = processor.effectiveCrossovers();
    for (auto& strip : strips) strip->update (cuts, builtCount, processor.nyquistFrequency());
    frequencyDisplay.refresh();
    juce::String text;
    for (int b = 0; b < builtCount && text.isEmpty(); ++b)
        for (int s = 0; s < triad::maxSlots; ++s)
            if (auto info = processor.getSlotInfo (b, s); info.status.isNotEmpty())
            { text = "Band " + juce::String (b + 1) + " / Slot " + juce::String (s + 1) + ": " + info.status; break; }
    if (text.isEmpty() && catalog->isScanning()) text = catalog->getProgress();
    if (text.isEmpty()) text = processor.getStatus();
    if (text.isEmpty()) text = "Click a slot to load an effect, click a loaded slot to open it. Double-click the display to add a band.";
    status.setText (text, juce::dontSendNotification);
}
void TriadEditor::changeListenerCallback (juce::ChangeBroadcaster*) { timerCallback(); }
void TriadEditor::setCrossoverFrequency (int index, float hz) { frequencyDisplay.setCrossoverValue (index, hz); }

void TriadEditor::chooseEffect (int band, int slot, juce::Component& target)
{
    auto list = std::make_shared<std::vector<EffectEntry>> (catalog->getEffects());
    juce::PopupMenu menu;
    menu.addSectionHeader ("Band " + juce::String (band + 1) + "  /  Slot " + juce::String (slot + 1));
    for (const juce::String format : { "VST3", "AU" })
    {
        juce::PopupMenu formatMenu, makerMenu;
        juce::String maker;
        bool any = false;
        auto flush = [&]
        {
            if (makerMenu.containsAnyActiveItems()) formatMenu.addSubMenu (maker.isNotEmpty() ? maker : "Other", makerMenu);
            makerMenu = juce::PopupMenu();
        };
        for (int i = 0; i < (int) list->size(); ++i)
        {
            const auto& e = (*list)[(size_t) i];
            if (e.format != format) continue;
            if (! any || e.manufacturer != maker) { flush(); maker = e.manufacturer; }
            makerMenu.addItem (i + 1, e.name);
            any = true;
        }
        flush();
        if (any) menu.addSubMenu (format == "AU" ? "Audio Units" : "VST3", formatMenu);
    }
    if (catalog->isScanning()) menu.addItem (scanningId, catalog->getProgress(), false);
    else if (list->empty()) menu.addItem (noneId, "No effects found", false);
    menu.addSeparator();
    menu.addItem (browseId, "Browse for a plug-in file...");
    menu.addItem (refreshId, "Refresh plug-in list");
    menu.addItem (rescanId, "Rescan all plug-ins");
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, list, band, slot] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto& self = *safe;
        if (choice == browseId) { self.browse (band, slot); return; }
        if (choice == refreshId) { self.catalog->refresh(); return; }
        if (choice == rescanId) { self.catalog->rescanAll(); return; }
        if (choice < 1 || choice > (int) list->size()) return;
        const auto& e = (*list)[(size_t) choice - 1];
        // Loading opens the effect's window straight away: one less click.
        if (e.loadByFile) self.processor.loadFile (band, slot, juce::File (e.description.fileOrIdentifier), true);
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
