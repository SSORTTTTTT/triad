#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"

namespace triad::ui
{
// Small drawn icons for toolbar buttons (no font glyphs needed).
class IconButton final : public juce::Button
{
public:
    enum class Icon { trash, undo, redo, reverse, flipVertical, left, right, gear, menu, plus, close, lfo, dice };
    IconButton (Icon, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);
private:
    Icon icon;
};

// FL-style green on/off light.
class Led final : public juce::Button
{
public:
    explicit Led (const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

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
};

// The look of every flat button, slider and knob in Triad.
class TriadLook final : public juce::LookAndFeel_V4
{
public:
    TriadLook();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&,
                                                const juce::PopupMenu::Options&) override;
};
}
