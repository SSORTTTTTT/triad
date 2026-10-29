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

// The columns of a matrix row (0.16.0, compact: Thomas, "each block a little bit smaller ... so you don't have to
// slide so much"): the light, SOURCE, the AMOUNT box, INV, DESTINATION, remove. The panel's column titles use them too.
struct MatrixColumns { juce::Range<int> light, source, amount, invert, destination, remove; };
MatrixColumns matrixColumns (int rowWidth);

// One row of a modulation matrix:  [light] SOURCE | AMOUNT box | INV | DESTINATION [x]
// (the LFO page shows the same rows without the source column).
class RouteRow final : public juce::Component
{
public:
    RouteRow (TriadProcessor&, int band, int route, bool showSource, juce::Colour);
    void update();
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void lookAndFeelChanged() override;   // the pills' colours follow the theme
    std::function<void (int band, int lfo)> onEditLfo;
    juce::Slider amount;
private:
    TriadProcessor& processor;
    int band, route;
    bool showSource;
    juce::Colour colour;
    Led light { "Route on/off" };
    juce::TextButton source, invert { "INV" }, destination, remove { "x" };
    juce::TextButton lock { "LOCK" };   // design: a band-edge LFO kept inside its band
    TriadProcessor::RouteInfo info;
    bool shown = false;
};

}
