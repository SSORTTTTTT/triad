#pragma once
#include "CrossoverDisplay.h"

class TriadLook final : public juce::LookAndFeel_V4
{
public:
    TriadLook();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
};
class TriadEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TriadEditor (TriadProcessor&);
    ~TriadEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    struct Card
    {
        juce::TextButton load { "Load effect..." }, open { "Open" }, remove { "Remove" };
        juce::Slider mix;
        juce::Label plugin, detail;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    TriadProcessor& processor;
    TriadLook look;
    CrossoverDisplay frequencyDisplay;
    std::array<Card, 3> cards;
    juce::Slider low, high;
    juce::Label lowLabel, highLabel, status;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowAttachment, highAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    void timerCallback() override;
    void choose (int);
    void browse (int);
    juce::Rectangle<int> cardBounds (int) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadEditor)
};
