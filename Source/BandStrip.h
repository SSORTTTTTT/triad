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
    // Copy/paste band state, chain presets, move, remove. atMouse: at the mouse (a right-click) rather than under `target`.
    virtual void showBandMenu (int band, juce::Component& target, bool atMouse = false) = 0;
    virtual void bandDrag (int band, const juce::MouseEvent&, int phase) = 0;   // 0 down, 1 drag, 2 up
    virtual bool isFavourite (const juce::String& format, const juce::String& name) = 0;
    virtual void setFavourite (const juce::String& format, const juce::String& name, bool) = 0;
    // The FAVORITES type a favorite is listed under (DELAY, REVERB ...; 0.16.0): the plug-in's own, or one set by hand.
    virtual juce::String getFavouriteType (const juce::String& format, const juce::String& name) { juce::ignoreUnused (format, name); return {}; }
    virtual void setFavouriteType (const juce::String& format, const juce::String& name, const juce::String& type) { juce::ignoreUnused (format, name, type); }
};

// One effect slot, laid out like an FL Studio mixer slot:
//   [>] Slot 3 / plug-in name ....... (~ LFO) (mix knob with %) (green on/off light)
// Click the name: open the loaded effect's window, or choose an effect for an empty slot.
// Click the arrow (or right-click): replace, copy / paste plugin state, move, bypass, LFO, favourite or remove.
// Press and drag a row up or down to move the effect to another slot of its band.
class SlotRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlotRow (TriadProcessor&, StripHost&, int band, int slot, juce::Colour);
    void update (const TriadProcessor::SlotInfo&, int slotCount);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void showMenu();
    void setLifted (bool on) { lifted = on; setAlpha (on ? 0.92f : 1.0f); repaint(); }   // drawn as picked up while dragged
    double latencyMs (int samples) const;
    void animate (double dt = 1.0 / 60.0);   // every frame: the LFO dot riding round the knob (glides between audio blocks)
    juce::String latencyText (int samples) const;
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
    float shownMix = -1.0f;   // where the dot is drawn (follows info.liveMix smoothly)
    bool hover = false, first = true, lifted = false;
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
    void update (const std::vector<float>& crossovers, int bandCount, float nyquist);   // a few times a second
    void updateEdges (const std::vector<float>& crossovers, int bandCount, float nyquist);   // every frame
    void animate (float level, double dt);                                             // every frame
    juce::Rectangle<int> meterArea() const;
    juce::Rectangle<int> peakTextArea() const;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void lookAndFeelChanged() override;          // the theme changed: re-colour the frequency labels
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setLifted (bool);   // drawn as picked up while being dragged
    // A slot row pressed and dragged (phase 0 down, 1 drag, 2 up): the effect moves to where it is dropped.
    // Returns true while (and when) a drag is under way, so the row does not also take it as a click.
    bool slotDrag (int slot, const juce::MouseEvent&, int phase);
    bool isDraggingSlot() const noexcept { return slotDragState.active; }
    int getNumRows() const noexcept { return (int) rows.size(); }
    SlotRow& getRow (int slot) { return *rows[(size_t) slot]; }
    AddSlotButton& getAddButton() noexcept { return addButton; }
    juce::Slider& getMixKnob() { return mix; }
    juce::Slider& getVolumeSlider() { return volume; }
    juce::Button& getMuteButton() { return muteButton; }
    int getBand() const noexcept { return band; }
    bool isPre() const noexcept { return band == TriadProcessor::preBand; }     // the PRE chain's strip
    bool isPost() const noexcept { return band == TriadProcessor::postBand; }   // the POST chain's strip
    bool isExtra() const noexcept { return isPre() || isPost(); }
    static constexpr int preferredHeight = 352;   // header, six slot rows before scrolling, volume and band mix
    static constexpr int headerHeight = 58;
private:
    void rebuildRows (int count);
    void layoutRows();
    void refreshRows();
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
    juce::TextButton muteButton { "MUTE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    float startHz = 0.0f, endHz = 0.0f, level = 0.0f, peakHold = 0.0f;
    int chainLatency = 0;
    double holdTime = 0.0;
    bool lifted = false;
    struct SlotDragState { int slot = -1; float startY = 0.0f; int startTop = 0; bool active = false; int target = -1; } slotDragState;
};
