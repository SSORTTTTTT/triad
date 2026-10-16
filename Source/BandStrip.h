#pragma once
#include "Widgets.h"

// Implemented by the editor: the plug-in chooser, menus and crossover edits live there.
class StripHost
{
public:
    virtual ~StripHost() = default;
    // `slot` may equal the band's slot count: the chosen effect then goes into a new slot.
    virtual void chooseEffect (int band, int slot, juce::Component& target) = 0;
    virtual void setCrossoverFrequency (int index, float hz) = 0;
    virtual void showLfo (int band, int slot) = 0;                     // slot -1: the Band Mix LFO
    virtual void showBandMenu (int band, juce::Component& target) = 0;  // chain presets, move, remove
    virtual void bandDrag (int band, const juce::MouseEvent&, int phase) = 0;   // 0 down, 1 drag, 2 up
    virtual bool isFavourite (const juce::String& format, const juce::String& name) = 0;
    virtual void setFavourite (const juce::String& format, const juce::String& name, bool) = 0;
};

// One effect slot, laid out like an FL Studio mixer slot:
//   [>] Slot 3 / plug-in name ....... (~ LFO) (mix knob with %) (green on/off light)
// Click the name: open the loaded effect's window, or choose an effect for an empty slot.
// Click the arrow (or right-click): replace, move, bypass, LFO, favourite or remove.
class SlotRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlotRow (TriadProcessor&, StripHost&, int band, int slot, juce::Colour);
    void update (const TriadProcessor::SlotInfo&, int slotCount);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void showMenu();
    juce::Slider knob;
    juce::Rectangle<int> ledArea() const { return { getWidth() - 20, 0, 18, getHeight() }; }
    juce::Rectangle<int> arrowArea() const { return { 0, 0, 20, getHeight() }; }
    juce::Rectangle<int> lfoArea() const { return { knob.getX() - 24, 0, 22, getHeight() }; }
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
    void update (const std::vector<float>& crossovers, int bandCount, float nyquist, float level);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setLifted (bool);   // drawn as picked up while being dragged
    int getNumRows() const noexcept { return (int) rows.size(); }
    SlotRow& getRow (int slot) { return *rows[(size_t) slot]; }
    AddSlotButton& getAddButton() noexcept { return addButton; }
    juce::Slider& getMixKnob() { return mix; }
    juce::Slider& getVolumeSlider() { return volume; }
    int getBand() const noexcept { return band; }
    static constexpr int preferredHeight = 352;   // header, six slot rows before scrolling, volume and band mix
    static constexpr int headerHeight = 58;
private:
    void rebuildRows (int count);
    void layoutRows();
    TriadProcessor& processor;
    StripHost& host;
    int band;
    juce::Colour colour;
    juce::TextButton removeButton, menuButton { "..." };
    juce::Label startLabel, endLabel;
    juce::Viewport slotView;
    juce::Component slotHolder;
    std::vector<std::unique_ptr<SlotRow>> rows;
    AddSlotButton addButton;
    juce::Slider mix, volume;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, volumeAttachment;
    float startHz = 0.0f, endHz = 0.0f, level = 0.0f, peakHold = 0.0f;
    int holdFrames = 0;
    bool lifted = false;
};
