#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"

// Implemented by the editor: the plug-in chooser and crossover edits live there.
class StripHost
{
public:
    virtual ~StripHost() = default;
    // `slot` may equal the band's slot count: the chosen effect then goes into a new slot.
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
    void update (const TriadProcessor::SlotInfo&, int slotCount);
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
    int band, slot, count = 1;
    juce::Colour colour;
    TriadProcessor::SlotInfo info;
    bool hover = false, first = true;
};

// The "+" under the last slot: adds a slot and picks its effect in one go.
class AddSlotButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    AddSlotButton (StripHost&, int band, juce::Colour);
    void setSlotCount (int n) { count = n; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    StripHost& host;
    int band, count = 1;
    juce::Colour colour;
    bool hover = false;
};

class BandStrip final : public juce::Component
{
public:
    BandStrip (TriadProcessor&, StripHost&, int band);
    void update (const std::vector<float>& crossovers, int bandCount, float nyquist);
    void paint (juce::Graphics&) override;
    void resized() override;
    int getNumRows() const noexcept { return (int) rows.size(); }
    SlotRow& getRow (int slot) { return *rows[(size_t) slot]; }
    AddSlotButton& getAddButton() noexcept { return addButton; }
    juce::Slider& getMixKnob() { return mix; }
    static constexpr int preferredHeight = 462;   // header, ten slot rows before scrolling, band mix
private:
    void rebuildRows (int count);
    void layoutRows();
    TriadProcessor& processor;
    StripHost& host;
    int band;
    juce::Colour colour;
    juce::TextButton removeButton;
    juce::Label startLabel, endLabel;
    juce::Viewport slotView;
    juce::Component slotHolder;
    std::vector<std::unique_ptr<SlotRow>> rows;
    AddSlotButton addButton;
    int builtStructure = -1;
    juce::Slider mix;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    float startHz = 0.0f, endHz = 0.0f;
};
