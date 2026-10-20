#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"
#include "Look.h"

namespace triad::ui
{
// The destinations an LFO can move, as menu items: Band Mix, Band Volume, each slot's mix, then the knobs
// inside each loaded effect (a submenu per effect, 40 knobs per page). Item ids are 1 + index into `chosen`.
void addDestinationItems (juce::PopupMenu& menu, TriadProcessor& processor, int band, const TriadProcessor::Destination* current,
                          std::vector<TriadProcessor::Destination>& chosen);

// One row of a modulation matrix:  [light] SOURCE | AMOUNT ===|--- 75% | INV | DESTINATION [x]
// (the LFO page shows the same rows without the source column).
class RouteRow final : public juce::Component
{
public:
    RouteRow (TriadProcessor&, int band, int route, bool showSource, juce::Colour);
    void update();
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void (int band, int lfo)> onEditLfo;
    juce::Slider amount;
private:
    TriadProcessor& processor;
    int band, route;
    bool showSource;
    juce::Colour colour;
    Led light { "Route on/off" };
    juce::TextButton source, invert { "INV" }, destination, remove { "x" };
    TriadProcessor::RouteInfo info;
    bool shown = false;
};

}
