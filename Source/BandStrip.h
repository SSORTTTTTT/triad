#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"

// Implemented by the editor: the plug-in chooser and crossover edits live there.
class StripHost
{
public:
    virtual ~StripHost() = default;
    virtual void chooseEffect (int band, int slot, juce::Component& target) = 0;
    virtual void setCrossoverFrequency (int index, float hz) = 0;
};

// One effect slot, laid out like an FL Studio mixer slot:
//   [>] Slot 3 / plug-in name ............ (mix knob with %) (green on/off light)
// Click the name: open the loaded effect's window, or choose an effect for an empty slot.
// Click the arrow (or right-click): replace, move, bypass or remove.
class SlotRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlotRow (TriadProcessor&, StripHost&, int band, int slot, juce::Colour);
    void update (const TriadProcessor::SlotInfo&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void showMenu();
    juce::Slider knob;
    juce::Rectangle<int> ledArea() const { return { getWidth() - 20, 0, 18, getHeight() }; }
    juce::Rectangle<int> arrowArea() const { return { 0, 0, 20, getHeight() }; }
private:
    TriadProcessor& processor;
    StripHost& host;
    int band, slot;
    juce::Colour colour;
    TriadProcessor::SlotInfo info;
    bool hover = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class BandStrip final : public juce::Component
{
public:
    BandStrip (TriadProcessor&, StripHost&, int band);
    void update (const std::vector<float>& crossovers, int bandCount, float nyquist);
    void paint (juce::Graphics&) override;
    void resized() override;
    SlotRow& getRow (int slot) { return *rows[(size_t) slot]; }
    juce::Slider& getMixKnob() { return mix; }
    static constexpr int preferredHeight = 462;   // header 58 + 10 slots x 32 + footer
private:
    TriadProcessor& processor;
    StripHost& host;
    int band;
    juce::Colour colour;
    juce::TextButton removeButton;
    juce::Label startLabel, endLabel;
    std::array<std::unique_ptr<SlotRow>, triad::maxSlots> rows;
    juce::Slider mix;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    float startHz = 0.0f, endHz = 0.0f;
};
