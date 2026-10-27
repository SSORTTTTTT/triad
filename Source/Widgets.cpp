#include "Widgets.h"

namespace triad::ui
{
MatrixColumns matrixColumns (int rowWidth)
{
    MatrixColumns c;
    int x = 8;
    const auto take = [&x] (int w) { const juce::Range<int> r (x, x + w); x += w + 6; return r; };
    c.light = take (18);
    c.source = take (60);
    c.amount = take (62);
    c.invert = take (32);
    c.remove = { rowWidth - 8 - 20, rowWidth - 8 };
    c.destination = { x, juce::jmax (x + 20, c.remove.getStart() - 6) };
    return c;
}
// ---- Route row ----------------------------------------------------------------------------------------
RouteRow::RouteRow (TriadProcessor& p, int b, int r, bool withSource, juce::Colour c)
    : processor (p), band (b), route (r), showSource (withSource), colour (c)
{
    light.onClick = [this] { processor.setRouteMuted (band, route, ! light.getToggleState()); };
    addAndMakeVisible (light);
    if (showSource)
    {
        source.setTooltip ("The LFO that moves the destination. Click to choose another; double-click the row to edit it.");
        source.onClick = [this]
        {
            juce::PopupMenu menu;
            menu.addSectionHeader ("SOURCE");
            for (int i = 0; i < processor.getLfoCount (band); ++i)
            {
                const auto l = processor.getLfoInfo (band, i);
                menu.addItem (i + 1, l.name + "  (" + l.target + ")", true, i == info.lfo);
            }
            menu.addSeparator();
            menu.addItem (1000, "Edit this LFO...");
            juce::Component::SafePointer<RouteRow> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&source), [safe] (int choice)
            {
                if (safe == nullptr || choice == 0) return;
                if (choice == 1000) { if (safe->onEditLfo) safe->onEditLfo (safe->band, safe->info.lfo); return; }
                safe->processor.setRouteSource (safe->band, safe->route, choice - 1);
            });
        };
        addAndMakeVisible (source);
    }
    // In the matrix: a small box (the value over a line meter), dragged left or right, a short way for the whole range
    // (0.16.0). The LFO page keeps its bar until its new layout.
    amount.setSliderStyle (showSource ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    amount.setMouseDragSensitivity (130);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amount.setRange (0.0, 100.0, 0.1);
    amount.setDoubleClickReturnValue (true, 100.0);
    amount.setColour (juce::Slider::trackColourId, colour);
    amount.setColour (juce::Slider::rotarySliderFillColourId, colour);
    amount.setTooltip ("Amount: at 100% the destination follows the LFO's line exactly; at 0% the LFO does nothing.");
    amount.onValueChange = [this] { processor.setRouteAmount (band, route, (float) amount.getValue() * 0.01f); repaint(); };
    addAndMakeVisible (amount);
    invert.setClickingTogglesState (true);
    invert.setTooltip ("Invert: the LFO's top becomes its bottom");
    invert.onClick = [this] { processor.setRouteInvert (band, route, invert.getToggleState()); };
    addAndMakeVisible (invert);
    destination.setTooltip ("What this LFO moves. Click to choose.");
    destination.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("DESTINATION");
        std::vector<TriadProcessor::Destination> list;
        addDestinationItems (menu, processor, band, &info.destination, list);
        juce::Component::SafePointer<RouteRow> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&destination), [safe, list] (int choice)
        {
            if (safe == nullptr || choice < 1 || choice > (int) list.size()) return;
            safe->processor.setRouteDestination (safe->band, safe->route, list[(size_t) choice - 1]);
        });
    };
    addAndMakeVisible (destination);
    remove.setTooltip ("Remove this route");
    remove.onClick = [this]
    {
        // Deleting this row inside its own callback is unsafe: the owner rebuilds rows on its next refresh.
        juce::MessageManager::callAsync ([&p = processor, b = band, r = route] { p.removeRoute (b, r); });
    };
    addAndMakeVisible (remove);
    lookAndFeelChanged();
    update();
}
void RouteRow::lookAndFeelChanged()
{
    if (! showSource) return;
    const auto pill = currentPalette.grid.interpolatedWith (currentPalette.controls, 0.3f);
    for (auto* b : { &source, &destination })
    {
        b->setColour (juce::TextButton::buttonColourId, pill);
        b->setColour (juce::TextButton::textColourOffId, textColour);
    }
    invert.setColour (juce::TextButton::buttonColourId, pill);
    invert.setColour (juce::TextButton::buttonOnColourId, accent);
    invert.setColour (juce::TextButton::textColourOffId, dimTextColour);
    invert.setColour (juce::TextButton::textColourOnId, inkColour);
    remove.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    remove.setColour (juce::TextButton::textColourOffId, dimTextColour);
}
void RouteRow::update()
{
    const auto next = processor.getRouteInfo (band, route);
    // Only what changed is redrawn (this runs several times a second; the screen's frames go to what moves).
    const bool changed = ! juce::exactlyEqual (next.amount, info.amount) || next.muted != info.muted || next.invert != info.invert
                         || next.sourceName != info.sourceName || next.destinationName != info.destinationName || ! shown;
    info = next;
    if (! changed) return;
    shown = true;
    light.setToggleState (! info.muted, juce::dontSendNotification);
    if (! amount.isMouseButtonDown()) amount.setValue (info.amount * 100.0, juce::dontSendNotification);
    invert.setToggleState (info.invert, juce::dontSendNotification);
    source.setButtonText (info.sourceName);
    destination.setButtonText (info.destinationName);
    setAlpha (info.muted ? 0.62f : 1.0f);
    repaint();
}
void RouteRow::resized()
{
    if (showSource)
    {
        const auto c = matrixColumns (getWidth());
        const auto at = [this] (juce::Range<int> x, int h) { return juce::Rectangle<int> (x.getStart(), (getHeight() - h) / 2, x.getLength(), h); };
        light.setBounds (at (c.light, 18));
        source.setBounds (at (c.source, 26));
        amount.setBounds (at (c.amount, getHeight() - 8));
        invert.setBounds (at (c.invert, 22));
        destination.setBounds (at (c.destination, getHeight() - 10));
        remove.setBounds (at (c.remove, 20));
        return;
    }
    auto r = getLocalBounds().reduced (4, 3);
    light.setBounds (r.removeFromLeft (18));
    r.removeFromLeft (4);
    remove.setBounds (r.removeFromRight (22));
    r.removeFromRight (4);
    if (showSource)
    {
        source.setBounds (r.removeFromLeft (juce::jmin (90, r.getWidth() / 4)));
        r.removeFromLeft (6);
        destination.setBounds (r.removeFromRight (juce::jmin (150, r.getWidth() / 3)));
    }
    else destination.setBounds (r.removeFromTop (r.getHeight() / 2).reduced (0, 1));
    r.removeFromRight (4);
    invert.setBounds (r.removeFromRight (36).reduced (0, showSource ? 0 : 1));
    r.removeFromRight (40);   // the percentage
    amount.setBounds (r);
}
void RouteRow::paint (juce::Graphics& g)
{
    g.setColour (slotRow);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), showSource ? 8.0f : 4.0f);
    if (showSource) return;   // the amount box shows its own value
    g.setColour (textColour);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (amount.getValue())) + "%", amount.getRight() + 2, amount.getY(), 38, amount.getHeight(),
                juce::Justification::centred);
}
void RouteRow::paintOverChildren (juce::Graphics& g)
{
    if (! showSource) return;
    // SOURCE and DESTINATION open menus: a small arrow says so.
    g.setColour (dimTextColour);
    for (auto* b : { &source, &destination })
    {
        juce::Path arrow;
        const float x = (float) b->getRight() - 11.0f, y = (float) b->getBounds().getCentreY();
        arrow.addTriangle (x, y - 2.0f, x + 6.0f, y - 2.0f, x + 3.0f, y + 2.5f);
        g.fillPath (arrow);
    }
}
}

void triad::ui::addDestinationItems (juce::PopupMenu& menu, TriadProcessor& processor, int band, const TriadProcessor::Destination* current,
                                     std::vector<TriadProcessor::Destination>& chosen)
{
    for (const auto& [destination, name] : processor.getDestinations (band))
    {
        chosen.push_back (destination);
        menu.addItem ((int) chosen.size(), name, true, current != nullptr && destination == *current);
    }
    bool header = false;
    for (int slot = 0; slot < processor.getSlotCount (band); ++slot)
    {
        const auto knobs = processor.getEffectParameters (band, slot);
        if (knobs.empty()) continue;
        if (! header) { menu.addSectionHeader ("KNOBS INSIDE EFFECTS"); header = true; }
        const auto info = processor.getSlotInfo (band, slot);
        juce::PopupMenu effectMenu;
        effectMenu.addSectionHeader (info.name.toUpperCase());
        constexpr int page = 40;
        auto addRange = [&] (juce::PopupMenu& m, int from, int to)
        {
            for (int k = from; k < to; ++k)
            {
                chosen.push_back (knobs[(size_t) k].first);
                m.addItem ((int) chosen.size(), knobs[(size_t) k].second, true, current != nullptr && knobs[(size_t) k].first == *current);
            }
        };
        if ((int) knobs.size() <= page) addRange (effectMenu, 0, (int) knobs.size());
        else
            for (int from = 0; from < (int) knobs.size(); from += page)
            {
                juce::PopupMenu part;
                const int to = juce::jmin ((int) knobs.size(), from + page);
                const auto range = knobs[(size_t) from].second + juce::String::fromUTF8 (" \xe2\x80\xa6 ") + knobs[(size_t) to - 1].second;
                part.addSectionHeader (range.toUpperCase());
                addRange (part, from, to);
                addSubMenu (effectMenu, range, std::move (part), juce::String (to - from));
            }
        addSubMenu (menu, juce::String (slot + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + info.name, std::move (effectMenu), juce::String ((int) knobs.size()));
    }
}
