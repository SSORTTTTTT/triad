#include "Widgets.h"

namespace triad::ui
{
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
    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amount.setRange (0.0, 100.0, 0.1);
    amount.setDoubleClickReturnValue (true, 100.0);
    amount.setColour (juce::Slider::trackColourId, colour);
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
    update();
}
void RouteRow::update()
{
    info = processor.getRouteInfo (band, route);
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
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (amount.getValue())) + "%", amount.getRight() + 2, amount.getY(), 38, amount.getHeight(),
                juce::Justification::centred);
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
        juce::PopupMenu effectMenu;
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
                addRange (part, from, to);
                effectMenu.addSubMenu (knobs[(size_t) from].second + juce::String::fromUTF8 (" \xe2\x80\xa6 ") + knobs[(size_t) to - 1].second, part);
            }
        const auto info = processor.getSlotInfo (band, slot);
        menu.addSubMenu (juce::String (slot + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + info.name + "  (" + juce::String ((int) knobs.size()) + ")", effectMenu);
    }
}
