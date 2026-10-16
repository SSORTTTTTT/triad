#pragma once
#include "Widgets.h"

// One band's modulation matrix (Serum / Omnisphere style): SOURCE | AMOUNT | INV | DESTINATION.
class BandMatrix final : public juce::Component
{
public:
    BandMatrix (TriadProcessor&, int band);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    int getNumRows() const noexcept { return (int) rows.size(); }
    std::function<void (int band, int lfo)> onEditLfo;
private:
    void rebuild();
    TriadProcessor& processor;
    int band;
    juce::Colour colour;
    juce::Viewport view;
    juce::Component holder;
    std::vector<std::unique_ptr<triad::ui::RouteRow>> rows;
    juce::TextButton addButton { "+ ADD ROUTE" };
    juce::String built;
};

// The MATRIX tab: a matrix per band, side by side like the band strips (scroll sideways).
class MatrixPage final : public juce::Component
{
public:
    explicit MatrixPage (TriadProcessor&);
    void refresh();
    void resized() override;
    int getNumPanels() const noexcept { return (int) panels.size(); }
    BandMatrix* getPanel (int band) const { return band >= 0 && band < (int) panels.size() ? panels[(size_t) band].get() : nullptr; }
    std::function<void (int band, int lfo)> onEditLfo;
private:
    TriadProcessor& processor;
    juce::Viewport view;
    juce::Component holder;
    std::vector<std::unique_ptr<BandMatrix>> panels;
    int builtCount = -1;
};
