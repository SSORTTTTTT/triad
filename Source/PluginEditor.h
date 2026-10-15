#pragma once
#include "CrossoverDisplay.h"
#include "BandStrip.h"
#include "PluginCatalog.h"

// Rotary knobs show their value as a percentage inside the circle (FL-style slot knobs).
class TriadLook final : public juce::LookAndFeel_V4
{
public:
    TriadLook();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
};

class TriadEditor final : public juce::AudioProcessorEditor, public StripHost,
                          private juce::Timer, private juce::ChangeListener
{
public:
    explicit TriadEditor (TriadProcessor&);
    ~TriadEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void chooseEffect (int band, int slot, juce::Component& target) override;
    void setCrossoverFrequency (int index, float hz) override;
    CrossoverDisplay& getDisplay() noexcept { return frequencyDisplay; }
    int getNumStrips() const noexcept { return (int) strips.size(); }
    BandStrip* getStrip (int band) const { return band >= 0 && band < (int) strips.size() ? strips[(size_t) band].get() : nullptr; }
    juce::String getStatusText() const { return status.getText(); }
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildStrips();
    void layoutStrips();
    void browse (int band, int slot);

    TriadProcessor& processor;
    TriadLook look;
    juce::SharedResourcePointer<PluginCatalog> catalog;
    CrossoverDisplay frequencyDisplay;
    juce::Component stripHolder;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<BandStrip>> strips;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };
    int builtCount = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadEditor)
};
