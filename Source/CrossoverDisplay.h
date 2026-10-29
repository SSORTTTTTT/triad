#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioEngine.h"
#include "UiCommon.h"
#include <complex>

// What the frequency panel shows and edits: Triad's bands, or Triad Split's. (No Triad code here, so Triad
// Split draws its bands with exactly the same panel.)
struct CrossoverHost
{
    virtual ~CrossoverHost() = default;
    virtual juce::String getProductName() const = 0;              // "Triad", "Triad Split" (in tooltips)
    virtual int getBandCount() const = 0;
    virtual std::vector<float> effectiveCrossovers() const = 0;   // one per divider, in order, as heard
    virtual int getCrossoverSlope (int index) const = 0;          // slope order (x 6 dB/oct)
    virtual juce::RangedAudioParameter* crossoverParameter (int index) const = 0;
    virtual juce::RangedAudioParameter* slopeParameter (int index) const = 0;
    virtual double getSampleRateForDisplay() const = 0;
    float nyquistFrequency() const { return (float) getSampleRateForDisplay() * 0.5f; }
    virtual void setAllCrossoverSlopes (int order) = 0;
    virtual bool addBand (float hz = -1.0f) = 0;                  // split the band at hz (<= 0: the widest band)
    virtual bool removeBand (int band) = 0;
    virtual triad::SpectrumTap& getSpectrum() = 0;
    // Triad only: the PRE / POST buttons, LIVE, and the band menu's copy / paste / LFO items.
    virtual bool hasChains() const { return false; }
    virtual bool hasLiveMode() const { return false; }
    virtual bool isLiveMode() const { return false; }
    virtual void setLiveMode (bool) {}
    virtual bool hasBandState() const { return false; }
    virtual void copyBandState (int) {}
    virtual void pasteBandState (int) {}
    virtual bool canPasteBandState() const { return false; }
    virtual juce::String pasteBandDescription() const { return {}; }
    // Triad only (0.18.0): LFOs on the band edges. The range an LFO sweeps a divider across ({0, 0}: none), and the
    // LFO already linked to a band's edge (-1: none).
    virtual bool canLinkToLfo() const { return false; }
    // Whether a band is heard (MUTE and SOLO): a band that is not loses its colour in the panel (0.18.0, Thomas).
    virtual bool isBandHeard (int) const { return true; }
    virtual std::pair<float, float> crossoverSweep (int) const { return {}; }
    virtual int edgeLfo (int /*band*/, int /*edge*/) const { return -1; }
    // Where the dividers are as heard right now, moved by their LFOs (the panel's dividers stay where they are set,
    // so they can be clicked and dragged; the movement is shown behind them as a ghost).
    virtual std::vector<float> heardCrossovers() const { return effectiveCrossovers(); }
};

// Mock-up switch (design, Thomas: "you can't click anything because it's moving all the time"): with an LFO on the
// band edges, the dividers stay where they are set and the movement shows behind them. 0: the dividers move (v17);
// 1: a ghost of the moving band; 2: the ghost, and the spectrum lit in the band's colour where it is now (backlit).
inline int ghostStyle()
{
    static const int style = juce::SystemStats::getEnvironmentVariable ("TRIAD_GHOST_STYLE", "2").getIntValue();
    return style;
}

// The frequency panel: a live spectrum of the incoming audio, the bands, and one draggable divider
// per crossover. Every gesture edits the same crossover parameters as automation and recall.
// Double-click empty space (or press + BAND) to split a band.
// Each divider also has a slope dot higher up: drag it up for a soft slope (bands blend into each
// other), down for a hard cut; its readout shows the slope in dB/octave (click it for a list).
// Each band is drawn as its own coloured response curve, so the overlap between bands is visible.
class CrossoverDisplay final : public juce::Component
{
public:
    explicit CrossoverDisplay (CrossoverHost&);
    explicit CrossoverDisplay (std::unique_ptr<CrossoverHost> owned);   // keeps the host
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
    // "~ Link to LFO": the band's edge (0 low, 1 high, 2 move the band, 3 open / close) to an LFO of that band.
    std::function<void (int band, int edge)> onLinkEdges;
    static void addLinkItems (juce::PopupMenu&, CrossoverHost&, int band, int idBase);   // the four, with the wave
    // Gear menu > GHOST MOVEMENT (design): with an LFO on the band edges, the band as it moves, behind the dividers
    // (which stay where they are set either way). Set by the editor from TriadSettings.
    static inline bool ghostMovement = true;
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
    // The slope list; with `hz`, each slope also shows how much later the sound at that crossover comes out.
    static void addSlopeItems (juce::PopupMenu& menu, int current, int idBase, float hz = 0.0f, double sampleRate = 48000.0);
    // "All dividers": hard wall (96 dB/oct), default (24) or soft (6), as menu items `idBase` + order.
    static void addAllDividerItems (juce::PopupMenu& menu, int idBase);
    static constexpr int allDividersBase = 1000;
    // PRE: shows or hides the PRE chain's strip (the editor decides; it keeps the button up to date).
    std::function<void()> onTogglePre, onTogglePost;
    void setPreState (bool shown, int effects) { setChainState (preButton, "PRE", shown, effects); }
    void setPostState (bool shown, int effects) { setChainState (postButton, "POST", shown, effects); }
    juce::Button& getPreButton() noexcept { return preButton; }
    juce::Button& getPostButton() noexcept { return postButton; }
    juce::Rectangle<float> slopeReadoutBounds (int index) const;
    int getActiveSlope() const noexcept { return activeSlope; }
    void updateSpectrum (double dt = 1.0 / 30.0);
    // Called on each frame it draws (up to 120 a second), before the spectrum: what else moves with the screen.
    std::function<void (double dt)> onFrame;
    triad::ui::FramePacer& getPacer() noexcept { return *pacer; }
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
    void paintGhost (juce::Graphics&, const std::vector<float>& heard);   // the dividers as heard, behind the controls
    juce::Path bandResponse (int band, const std::vector<float>& at, bool fill, bool floor) const;
    int lfoBandOf (int divider) const;   // the band whose LFO moves a divider (-1: none)
    juce::Rectangle<float> readoutBoundsFor (int index, const std::vector<juce::Rectangle<float>>& taken) const;
    std::vector<juce::Rectangle<float>> pillRects() const;
    void paintMeters (juce::Graphics&);
    void paintBandCurves (juce::Graphics&);
    void paintSlopeReadouts (juce::Graphics&);
    float pillTop (int index) const;
    void layoutPills();

    std::unique_ptr<CrossoverHost> ownedHost;
    CrossoverHost& host;
    std::vector<std::unique_ptr<Handle>> handles;
    juce::TextButton addButton { "+ BAND" }, preButton { "PRE" }, postButton { "POST" };
    static void setChainState (juce::TextButton&, const juce::String& name, bool shown, int effects);
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
    std::vector<std::pair<float, float>> sweeps;   // per divider: where its LFOs take it (empty: none)
    int staticRenders = 0;
    double lastFrame = 0.0;
    juce::SharedResourcePointer<triad::ui::FramePacer> pacer;
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
