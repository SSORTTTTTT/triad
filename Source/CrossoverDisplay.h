#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"
#include <complex>

// The frequency panel: a live spectrum of the incoming audio, the bands, and one draggable divider
// per crossover. Every gesture edits the same crossover parameters as automation and recall.
// Double-click empty space (or press + BAND) to split a band.
// Each divider also has a slope dot higher up: drag it up for a soft slope (bands blend into each
// other), down for a hard cut; its readout shows the slope in dB/octave (click it for a list).
// Each band is drawn as its own coloured response curve, so the overlap between bands is visible.
class CrossoverDisplay final : public juce::Component
{
public:
    explicit CrossoverDisplay (TriadProcessor&);
    ~CrossoverDisplay() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    void refreshIfChanged();   // refresh() only when the bands, crossovers or slopes changed
    // Peak levels since the last call (linear) and Triad's CPU share, once a frame (dt: seconds since the last).
    void setMeters (float input, float output, float cpu = 0.0f, double dt = 0.05);
    // Latency meter: what Triad reports to the DAW, and which effects add it (for its tooltip).
    void setLatency (int samples, double sampleRate, const juce::String& breakdown);
    juce::Button& getLiveButton() noexcept { return liveButton; }
    juce::String getLatencyText() const;
    void showBandMenu (int band, float hz);
    void showCrossoverMenu (int index);
    int bandAt (float x) const;
    std::function<void (int band)> onEditBandLfo;
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
    // Slope dot: the y position for a slope order, and a slope change as one undoable gesture.
    float slopeY (int order) const;
    int slopeForY (float y) const;
    void setSlope (int index, int order);
    void beginSlopeGesture (int index);
    void dragSlopeTo (int order);
    void endSlopeGesture();
    void showSlopeMenu (int index);
    static constexpr float slopeDragStep = 6.0f;   // mouse travel (pixels) per slope step when dragging the dot
    static void addSlopeItems (juce::PopupMenu& menu, int current, int idBase);
    juce::Rectangle<float> slopeReadoutBounds (int index) const;
    int getActiveSlope() const noexcept { return activeSlope; }
    void updateSpectrum (double dt = 1.0 / 30.0);
    int getStaticLayerRenders() const noexcept { return staticRenders; }
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
        void mouseMove (const juce::MouseEvent&) override;
        bool isOverSlopeDot (juce::Point<float> local) const;
    private:
        CrossoverDisplay& owner;
        int index;
        float grabOffset = 0.0f, slopeGrabY = 0.0f;
        int slopeGrabOrder = 4;
        bool draggingSlope = false;
    };
    void frame (double time);   // every frame of the screen (up to 60 a second): the spectrum
    float constrainValue (int, float) const;
    void paintStaticLayer (juce::Graphics&);
    juce::Rectangle<float> readoutBoundsFor (int index, const std::vector<juce::Rectangle<float>>& taken) const;
    std::vector<juce::Rectangle<float>> pillRects() const;
    void paintMeters (juce::Graphics&);
    void paintBandCurves (juce::Graphics&);
    void paintSlopeReadouts (juce::Graphics&);
    float pillTop (int index) const;
    void layoutPills();

    TriadProcessor& processor;
    std::vector<std::unique_ptr<Handle>> handles;
    juce::TextButton addButton { "+ BAND" };
    int activeHandle = -1, activeSlope = -1;
    std::vector<float> cuts;
    std::vector<int> pillRows;
    static constexpr float pillWidth = 78.0f, meterWidth = 120.0f;
    struct Meter { float level = 0.0f, hold = 0.0f; double holdTime = 0.0; void push (float v, double dt); };
    Meter inputMeter, outputMeter;
    int latencySamples = 0;
    double latencyRate = 48000.0;
    float cpuShown = 0.0f;
    juce::TextButton liveButton { "LIVE" };
    // What does not move (background, band curves, grid, labels) is drawn once into an image.
    juce::Image staticLayer;
    std::vector<float> staticKey, refreshKey;
    int staticRenders = 0;
    double lastFrame = 0.0;
    juce::VBlankAttachment frames { this, [this] (double t) { frame (t); } };
    struct Tip final : juce::Component, juce::SettableTooltipClient {} meterTip;

    static constexpr int fftSize = 4096;
    std::vector<float> samples, window, levels;
    std::vector<std::complex<float>> bins;
    float windowSum = 1.0f;
    uint32_t lastWriteIndex = 0;
    bool spectrumVisible = false;
    juce::Path spectrumLine, spectrumFill;
    std::vector<juce::Path> curveFills, curveLines;
    std::vector<float> curveKey;
    friend class Handle;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverDisplay)
};
