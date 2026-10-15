#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"
#include <complex>

// The frequency panel: a live spectrum of the incoming audio, the bands, and one draggable divider
// per crossover. Every gesture edits the same crossover parameters as automation and recall.
// Double-click empty space (or press + BAND) to split a band.
class CrossoverDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit CrossoverDisplay (TriadProcessor&);
    ~CrossoverDisplay() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();
    float xForFrequency (float) const;
    float frequencyForX (float) const;
    juce::Rectangle<float> plotBounds() const;
    int getNumHandles() const noexcept { return (int) handles.size(); }
    juce::Component* getHandle (int index) const;
    juce::Button& getAddButton() noexcept { return addButton; }
    void beginCrossoverGesture (int index);
    void dragCrossoverTo (float x);
    void endCrossoverGesture();
    void setCrossoverValue (int index, float hz);
    void updateSpectrum();
    bool isSpectrumVisible() const noexcept { return spectrumVisible; }

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
    void timerCallback() override;
    float constrainValue (int, float) const;
    float pillTop (int index) const;
    void layoutPills();

    TriadProcessor& processor;
    std::vector<std::unique_ptr<Handle>> handles;
    juce::TextButton addButton { "+ BAND" };
    int activeHandle = -1;
    std::vector<float> cuts;
    std::vector<int> pillRows;
    static constexpr float pillWidth = 78.0f;

    static constexpr int fftSize = 4096;
    std::vector<float> samples, window, levels;
    std::vector<std::complex<float>> bins;
    float windowSum = 1.0f;
    uint32_t lastWriteIndex = 0;
    bool spectrumVisible = false;
    juce::Path spectrumLine, spectrumFill;
    friend class Handle;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverDisplay)
};
