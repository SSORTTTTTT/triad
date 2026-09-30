#pragma once
#include "PluginProcessor.h"

// One view of the existing crossover parameters. All gestures use the same
// parameter IDs as the numeric controls, DAW automation, and project state.
class CrossoverDisplay final : public juce::Component
{
public:
    explicit CrossoverDisplay (TriadProcessor&);
    ~CrossoverDisplay() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    float xForFrequency (float) const;
    float frequencyForX (float) const;
    juce::Rectangle<float> plotBounds() const;
    void beginCrossoverGesture (int index);
    void dragCrossoverTo (float x);
    void endCrossoverGesture();
    void setCrossoverValue (int index, float hz);

private:
    class Handle final : public juce::Component
    {
    public:
        Handle (CrossoverDisplay&, int);
        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        bool keyPressed (const juce::KeyPress&) override;
    private:
        CrossoverDisplay& owner;
        int index;
        float grabOffset = 0.0f;
    };
    TriadProcessor& processor;
    std::array<juce::RangedAudioParameter*, 2> parameters;
    std::array<std::unique_ptr<Handle>, 2> handles;
    int activeHandle = -1;
    float constrainValue (int, float) const;
    friend class Handle;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverDisplay)
};
