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
        g.setColour (active ? ledColour : juce::Colour (0xff4a504c));
        g.fillEllipse (light);
    }
private:
    juce::String line1, line2;
    bool active;
    juce::Colour colour;
};

}

// ---- LFO page -----------------------------------------------------------------------------------------
LfoPage::LfoPage (TriadProcessor& p) : processor (p)
{
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
    addAndMakeVisible (shapes);
    generator.setTarget (&shapes.getEditor());
    addAndMakeVisible (generator);
    addRouteButton.setTooltip ("Send this LFO to another knob in this band: a mix, the band's volume, or a knob inside one of its effects "
                               "(from the list, or LEARN: move the knob in the effect's window)");
    addRouteButton.onClick = [this]
    {
        if (processor.isLearningParameter()) { processor.cancelParameterLearn(); updateToolbar(); return; }
        enum { learnId = 100000 };
        juce::PopupMenu menu;
        menu.addSectionHeader ("ROUTE " + processor.getLfoInfo (band, lfo).name.toUpperCase() + " TO");
        std::vector<TriadProcessor::Destination> list;
        addDestinationItems (menu, processor, band, nullptr, list);
        menu.addSeparator();
        menu.addItem (learnId, "LEARN: move a knob in an effect's window...", processor.getSlotCount (band) > 0);
        juce::Component::SafePointer<LfoPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addRouteButton), [safe, list] (int choice)
        {
            if (safe == nullptr) return;
            if (choice == learnId) { safe->startLearn(); return; }
            if (choice < 1 || choice > (int) list.size()) return;
            safe->processor.addRoute (safe->band, safe->lfo, list[(size_t) choice - 1]);
            safe->refresh();
        });
    };
    addAndMakeVisible (addRouteButton);
    removeLfoButton.onClick = [this]
    {
        if (processor.removeLfo (band, lfo)) select (band, juce::jmax (0, lfo - 1));
    };
    addChildComponent (removeLfoButton);
    select (0, processor.getBandLfo (0));
}
LfoPage::~LfoPage() = default;

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
void LfoPage::startLearn()
{
    if (! processor.beginParameterLearn (band, lfo))
    {
        processor.setStatus (TriadProcessor::isExtraChain (band) ? "LEARN: " + TriadProcessor::chainName (band) + " has no effect loaded"
                                                                 : juce::String ("LEARN: this band has no effect loaded"));
        return;
    }
    // With a single effect in the band, open its window to move a knob in.
    int loaded = -1, count = 0;
    for (int s = 0; s < processor.getSlotCount (band); ++s)
        if (processor.getSlotInfo (band, s).loaded) { loaded = s; ++count; }
    if (count == 1) processor.showEffect (band, loaded);
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
void LfoPage::rebuild()
{
    builtSignature = signature();
    host.band = band; host.lfo = lfo;
    shapes.setHost (&host);
    bandButtons.clear();
    // PRE first, then the bands, then POST: each has its own LFOs, like a band.
    const int count = processor.getBandCount();
    for (int b = -1; b <= count; ++b)
    {
        const int index = b < 0 ? TriadProcessor::preBand : b == count ? TriadProcessor::postBand : b;
        auto button = std::make_unique<juce::TextButton> (TriadProcessor::isExtraChain (index) ? TriadProcessor::chainName (index)
                                                                                              : count > 8 ? "B" + juce::String (b + 1) : "BAND " + juce::String (b + 1));
        button->setColour (juce::TextButton::buttonColourId, index == band ? bandColour (index).withAlpha (0.85f) : slotRow);
        button->setColour (juce::TextButton::textColourOffId, index == band ? juce::Colour (0xff111312) : bandColour (index));
        button->onClick = [this, index] { select (index, juce::jmax (0, processor.getBandLfo (index))); };
        addAndMakeVisible (*button);
        bandButtons.push_back (std::move (button));
    }
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
        addAndMakeVisible (*row);
        routeRows.push_back (std::move (row));
    }
    const auto info = processor.getLfoInfo (band, lfo);
    removeLfoButton.setVisible (! info.bandLfo && info.slot < 0);
    resized();
    repaint();
}
void LfoPage::updateToolbar()
{
    addRouteButton.setButtonText (processor.isLearningParameter() ? "LEARNING: MOVE A KNOB... (CLICK TO STOP)" : "+ ROUTE TO...");
    if (processor.isLearningParameter()) addRouteButton.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.35f));
    else addRouteButton.removeColour (juce::TextButton::buttonColourId);
    shapes.updateToolbar();
}
void LfoPage::resized()
{
    auto r = getLocalBounds();
    auto bandRow = r.removeFromTop (26);
    for (auto& b : bandButtons) { b->setBounds (bandRow.removeFromLeft (chipWidth()).withTrimmedRight (4)); }
    r.removeFromTop (6);
    auto tabRow = r.removeFromTop (44);
    addLfoButton.setBounds (tabRow.removeFromRight (30).withSizeKeepingCentre (28, 28));
    tabRow.removeFromRight (6);
    tabView.setBounds (tabRow);
    int x = 0;
    for (auto& t : tabs) { t->setBounds (x, 0, 156, 38); x += 160; }
    tabHolder.setSize (juce::jmax (x, tabRow.getWidth()), 38);
    r.removeFromTop (8);
    routingArea = r.removeFromRight (250);
    generatorArea = routingArea.removeFromBottom (158);
    routingArea.removeFromBottom (8);
    generator.setBounds (generatorArea);
    r.removeFromRight (12);
    shapes.setBounds (r);
    auto panel = routingArea.reduced (10, 0).withTrimmedTop (48);
    for (auto& row : routeRows) { row->setBounds (panel.removeFromTop (54)); panel.removeFromTop (4); }
    addRouteButton.setBounds (panel.removeFromTop (28));
    removeLfoButton.setBounds (routingArea.reduced (10, 0).removeFromBottom (28).translated (0, -46));
}
void LfoPage::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (routingArea.toFloat(), 7.0f);
    const auto info = processor.getLfoInfo (band, lfo);
    auto header = routingArea.reduced (12, 8);
    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("ROUTING", header.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ((TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "Band " + juce::String (band + 1)) + u8 (" \xc2\xb7 ") + info.name + u8 (" \xe2\x86\x92"), header.removeFromTop (18),
                juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (11.0f));
    auto notes = routingArea.reduced (12, 10).removeFromBottom (44);
    if (notes.getY() > addRouteButton.getBottom() + 6)
        g.drawFittedText ("Amount 100%: the knob follows the line (top = 100%, bottom = 0%). The light switches a route on or off.",
                          notes, juce::Justification::bottomLeft, 3);
    g.setColour (dimTextColour);
    // How to edit, under the band row.
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (u8 ("Double-click: add point  \xc2\xb7  Drag: move  \xc2\xb7  Double-click a point: delete  \xc2\xb7  Drag the small dot: curve"
                    "  \xc2\xb7  Right-click: more"),
                juce::Rectangle<int> ((int) bandButtons.size() * chipWidth() + 8, 0, getWidth() - (int) bandButtons.size() * chipWidth() - 8, 26),
                juce::Justification::centredRight);
}
