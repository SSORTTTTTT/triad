#include "CrossoverDisplay.h"

using namespace triad::ui;

CrossoverDisplay::CrossoverDisplay (TriadProcessor& p) : processor (p)
{
    setComponentID ("frequencyDisplay");
    setTitle ("Band crossover display");
    addButton.setTooltip ("Add a band (up to 10). Tip: double-click the display to split a band right there.");
    addButton.onClick = [this] { processor.addBand(); refresh(); };
    addAndMakeVisible (addButton);
    samples.resize ((size_t) fftSize);
    window.resize ((size_t) fftSize);
    bins.resize ((size_t) fftSize);
    levels.assign ((size_t) fftSize / 2 + 1, -200.0f);
    windowSum = 0.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
        windowSum += window[(size_t) i];
    }
    processor.spectrum.active.store (true);
    refresh();
    startTimerHz (30);
}
CrossoverDisplay::~CrossoverDisplay()
{
    stopTimer();
    processor.spectrum.active.store (false);
    endCrossoverGesture();
}
juce::Component* CrossoverDisplay::getHandle (int index) const
{
    return index >= 0 && index < (int) handles.size() ? handles[(size_t) index].get() : nullptr;
}
juce::Rectangle<float> CrossoverDisplay::plotBounds() const
{
    return { 20.0f, 38.0f, juce::jmax (1.0f, (float) getWidth() - 40.0f - meterWidth), juce::jmax (1.0f, (float) getHeight() - 70.0f) };
}
float CrossoverDisplay::xForFrequency (float frequency) const
{
    const auto plot = plotBounds();
    const auto maximum = processor.nyquistFrequency();
    return plot.getX() + plot.getWidth() * std::log (juce::jlimit (20.0f, maximum, frequency) / 20.0f) / std::log (maximum / 20.0f);
}
float CrossoverDisplay::frequencyForX (float x) const
{
    const auto plot = plotBounds();
    const auto fraction = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return 20.0f * std::pow (processor.nyquistFrequency() / 20.0f, fraction);
}
float CrossoverDisplay::constrainValue (int index, float f) const
{
    const auto current = processor.effectiveCrossovers();
    if (index < 0 || index >= (int) current.size()) return f;
    const auto ceiling = juce::jmin (20000.0f, processor.nyquistFrequency() * 0.9f);
    const auto lower = index == 0 ? 20.0f : current[(size_t) index - 1] * triad::minCrossoverRatio;
    const auto upper = index == (int) current.size() - 1 ? ceiling : current[(size_t) index + 1] / triad::minCrossoverRatio;
    return juce::jlimit (juce::jmin (lower, upper), upper, f);
}
void CrossoverDisplay::beginCrossoverGesture (int index)
{
    if (index < 0 || index >= (int) handles.size()) return;
    endCrossoverGesture();
    activeHandle = index;
    if (auto* p = processor.crossoverParameter (index)) p->beginChangeGesture();
    repaint();
}
void CrossoverDisplay::dragCrossoverTo (float x)
{
    if (activeHandle < 0) return;
    if (auto* parameter = processor.crossoverParameter (activeHandle))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (constrainValue (activeHandle, frequencyForX (x))));
    refresh();
}
void CrossoverDisplay::endCrossoverGesture()
{
    if (activeHandle < 0) return;
    const auto ended = activeHandle;
    activeHandle = -1;
    if (auto* p = processor.crossoverParameter (ended)) p->endChangeGesture();
    repaint();
}
void CrossoverDisplay::setCrossoverValue (int index, float frequency)
{
    if (index < 0 || index >= processor.getBandCount() - 1) return;
    beginCrossoverGesture (index);
    if (auto* parameter = processor.crossoverParameter (index))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (constrainValue (index, frequency)));
    refresh();
    endCrossoverGesture();
}
void CrossoverDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! plotBounds().contains (e.position) || e.mods.isPopupMenu()) return;
    processor.addBand (frequencyForX (e.position.x));
    refresh();
}
int CrossoverDisplay::bandAt (float x) const
{
    int band = 0;
    for (size_t i = 0; i < cuts.size(); ++i) if (x > xForFrequency (cuts[i])) band = (int) i + 1;
    return band;
}
void CrossoverDisplay::mouseDown (const juce::MouseEvent& e)
{
    // Right-click a band's area: remove it (or add a band right there).
    if (e.mods.isPopupMenu() && plotBounds().contains (e.position)) showBandMenu (bandAt (e.position.x), frequencyForX (e.position.x));
}
void CrossoverDisplay::showBandMenu (int band, float hz)
{
    enum { removeId = 1, addId, lfoId };
    const int count = processor.getBandCount();
    juce::PopupMenu menu;
    menu.addSectionHeader ("BAND " + juce::String (band + 1));
    menu.addItem (removeId, "Remove band " + juce::String (band + 1), count > 1);
    menu.addItem (addId, "Add a band here (" + formatHz (hz) + ")", count < triad::maxBands);
    menu.addItem (lfoId, "Edit LFO for band " + juce::String (band + 1) + " mix...");
    juce::Component::SafePointer<CrossoverDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, band, hz] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == removeId) safe->processor.removeBand (band);
        else if (choice == addId) safe->processor.addBand (hz);
        else if (choice == lfoId) { if (safe->onEditBandLfo) safe->onEditBandLfo (band); return; }
        safe->refresh();
    });
}
void CrossoverDisplay::showCrossoverMenu (int index)
{
    enum { removeBelow = 1, removeAbove, resetId };
    juce::PopupMenu menu;
    menu.addSectionHeader ("CROSSOVER " + juce::String (index + 1) + "  " + (index < (int) cuts.size() ? formatHz (cuts[(size_t) index]) : juce::String()));
    menu.addItem (removeBelow, "Remove band " + juce::String (index + 1) + " (below)");
    menu.addItem (removeAbove, "Remove band " + juce::String (index + 2) + " (above)");
    menu.addItem (resetId, "Reset this crossover");
    juce::Component::SafePointer<CrossoverDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, index] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == removeBelow) safe->processor.removeBand (index);
        else if (choice == removeAbove) safe->processor.removeBand (index + 1);
        else if (choice == resetId) safe->setCrossoverValue (index, TriadProcessor::defaultCrossover (index, safe->processor.getBandCount()));
        safe->refresh();
    });
}
void CrossoverDisplay::Meter::push (float v)
{
    level = juce::jmax (v, level * 0.82f);
    if (v >= hold) { hold = v; holdFrames = 45; }
    else if (--holdFrames < 0) hold *= 0.9f;
}
void CrossoverDisplay::setMeters (float input, float output)
{
    inputMeter.push (input);
    outputMeter.push (output);
}
void CrossoverDisplay::paintMeters (juce::Graphics& g)
{
    const auto plot = plotBounds();
    const float top = plot.getY() + 8.0f, bottom = plot.getBottom();
    const float x0 = (float) getWidth() - 18.0f - meterWidth + 12.0f;
    auto yFor = [&] (float gain)
    {
        const float db = juce::jlimit (-60.0f, 6.0f, juce::Decibels::gainToDecibels (gain, -60.0f));
        return bottom - (bottom - top) * (db + 60.0f) / 66.0f;
    };
    const char* labels[] { "IN", "OUT" };
    const Meter* meters[] { &inputMeter, &outputMeter };
    for (int i = 0; i < 2; ++i)
    {
        const float x = x0 + (float) i * 28.0f;
        const auto bar = juce::Rectangle<float> (x, top, 12.0f, bottom - top);
        g.setColour (juce::Colour (0xff1c1f1e)); g.fillRoundedRectangle (bar, 2.0f);
        const auto& m = *meters[i];
        if (m.level > 0.001f)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9bd85c), 0.0f, bottom, juce::Colour (0xffe8705c), 0.0f, yFor (1.0f), false));
            g.fillRoundedRectangle (bar.withTop (yFor (m.level)), 2.0f);
        }
        if (m.hold > 0.001f)
        {
            g.setColour (m.hold > 1.0f ? juce::Colour (0xffe8705c) : textColour);
            g.fillRect (x, yFor (m.hold) - 1.0f, 12.0f, 2.0f);
        }
        g.setColour (juce::Colour (0xffb8bcba)); g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (labels[i], juce::Rectangle<float> (x - 8.0f, top - 18.0f, 28.0f, 14.0f), juce::Justification::centred);
        g.setColour (dimTextColour); g.setFont (juce::FontOptions (9.0f));
        const auto text = m.hold < 1.0e-4f ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (m.hold), 1);
        g.drawText (text, juce::Rectangle<float> (x - 9.0f, bottom + 10.0f, 30.0f, 14.0f), juce::Justification::centred);
    }
    g.setColour (juce::Colour (0xff3a3f3c));
    g.drawVerticalLine (juce::roundToInt (x0 - 10.0f), top - 18.0f, bottom + 22.0f);
}
void CrossoverDisplay::refresh()
{
    const int wanted = juce::jmax (0, processor.getBandCount() - 1);
    if ((int) handles.size() != wanted)
    {
        endCrossoverGesture();
        handles.clear();
        for (int i = 0; i < wanted; ++i)
        {
            handles.push_back (std::make_unique<Handle> (*this, i));
            addAndMakeVisible (*handles.back());
        }
    }
    addButton.setEnabled (processor.getBandCount() < triad::maxBands);
    resized();
    repaint();
}
void CrossoverDisplay::layoutPills()
{
    // Readouts that would overlap are stacked on up to three rows.
    pillRows.assign (cuts.size(), 0);
    std::array<float, 3> rowRight { -1.0e9f, -1.0e9f, -1.0e9f };
    const auto plot = plotBounds();
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const auto left = juce::jlimit (plot.getX(), juce::jmax (plot.getX(), plot.getRight() - pillWidth), xForFrequency (cuts[i]) - pillWidth * 0.5f);
        int row = 0;
        while (row < 2 && left < rowRight[(size_t) row] + 4.0f) ++row;
        pillRows[i] = row;
        rowRight[(size_t) row] = left + pillWidth;
    }
}
float CrossoverDisplay::pillTop (int index) const
{
    const auto row = index >= 0 && index < (int) pillRows.size() ? pillRows[(size_t) index] : 0;
    return plotBounds().getBottom() - 37.0f - (float) row * 28.0f;
}
void CrossoverDisplay::resized()
{
    addButton.setBounds (getWidth() - 96 - (int) meterWidth, 7, 78, 24);
    cuts = processor.effectiveCrossovers();
    layoutPills();
    const auto plot = plotBounds();
    for (size_t i = 0; i < handles.size() && i < cuts.size(); ++i)
    {
        const auto f = cuts[i];
        handles[i]->setBounds (juce::roundToInt (xForFrequency (f)) - 12, juce::roundToInt (plot.getY()), 24, juce::roundToInt (plot.getHeight()) + 10);
        handles[i]->setDescription ("Crossover " + juce::String ((int) i + 1) + ": " + formatHz (f)
                                    + ". Drag left or right; arrow keys adjust; Shift gives finer adjustment.");
    }
    // The selected handle stays reachable even when two boundaries are close.
    if (activeHandle >= 0 && activeHandle < (int) handles.size()) handles[(size_t) activeHandle]->toFront (false);
}

// ---- Live spectrum --------------------------------------------------------------------------
void CrossoverDisplay::timerCallback()
{
    updateSpectrum();
    repaint (plotBounds().toNearestInt().expanded (2));
    repaint (getWidth() - (int) meterWidth - 24, 0, (int) meterWidth + 24, getHeight());
}
void CrossoverDisplay::updateSpectrum()
{
    constexpr float decayPerFrame = 1.2f;   // dB per 1/30 s
    const auto writeIndex = processor.spectrum.getWriteIndex();
    const bool fresh = writeIndex != lastWriteIndex;
    lastWriteIndex = writeIndex;
    const auto sampleRate = (float) processor.getSampleRateForDisplay();
    const auto binHz = sampleRate / (float) fftSize;
    if (fresh && processor.spectrum.read (samples.data(), fftSize))
    {
        for (size_t i = 0; i < (size_t) fftSize; ++i) bins[i] = { samples[i] * window[i], 0.0f };
        triad::fft (bins);
        for (size_t k = 1; k < levels.size(); ++k)
        {
            const auto magnitude = std::abs (bins[k]) * 2.0f / windowSum;
            // +3 dB/octave tilt around 1 kHz so typical music reads as roughly level.
            const auto db = 20.0f * std::log10 (magnitude + 1.0e-9f) + 3.0f * std::log2 (juce::jmax (1.0f, (float) k * binHz) / 1000.0f);
            levels[k] = juce::jmax (db, levels[k] - decayPerFrame);
        }
    }
    else
    {
        for (auto& l : levels) l -= decayPerFrame;
    }
    const auto plot = plotBounds();
    constexpr float floorDb = -84.0f, topDb = 6.0f;
    auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (floorDb, topDb, db), floorDb, topDb, plot.getBottom(), plot.getY() + 26.0f); };
    auto levelAt = [&] (float binPosition)
    {
        const auto k = juce::jlimit (1.0f, (float) levels.size() - 2.0f, binPosition);
        const auto i = (size_t) k;
        return juce::jmap (k - (float) i, levels[i], levels[i + 1]);
    };
    spectrumLine.clear();
    spectrumFill.clear();
    spectrumVisible = false;
    spectrumFill.startNewSubPath (plot.getX(), plot.getBottom());
    for (float x = plot.getX(); x <= plot.getRight() + 0.5f; x += 2.0f)
    {
        const auto k0 = frequencyForX (x - 1.0f) / binHz, k1 = frequencyForX (x + 1.0f) / binHz;
        float db;
        if (k1 - k0 < 1.0f) db = levelAt (0.5f * (k0 + k1));
        else
        {
            db = -200.0f;
            for (auto k = (size_t) juce::jmax (1.0f, k0); k <= (size_t) k1 && k < levels.size(); ++k) db = juce::jmax (db, levels[k]);
        }
        spectrumVisible |= db > floorDb + 3.0f;
        const auto y = yFor (db);
        if (spectrumLine.isEmpty()) spectrumLine.startNewSubPath (x, y); else spectrumLine.lineTo (x, y);
        spectrumFill.lineTo (x, y);
    }
    spectrumFill.lineTo (plot.getRight(), plot.getBottom());
    spectrumFill.closeSubPath();
}

void CrossoverDisplay::paint (juce::Graphics& g)
{
    const auto plot = plotBounds();
    const int count = processor.getBandCount();
    std::vector<float> xs { plot.getX() };
    for (auto c : cuts) xs.push_back (xForFrequency (c));
    xs.push_back (plot.getRight());
    g.setColour (juce::Colour (0xff111213)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    g.setColour (juce::Colour (0xffb8bcba)); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("BAND CROSSOVERS", 18, 8, 175, 23, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.0f)); g.setColour (juce::Colour (0xff898f8b));
    g.drawText ("Drag a divider  /  Double-click to add a band  /  Right-click to remove", getWidth() - 540 - (int) meterWidth, 8, 430, 23,
                juce::Justification::centredRight);
    for (int i = 0; i < count && i + 1 < (int) xs.size(); ++i)
    {
        const auto colour = bandColour (i);
        g.setColour (colour.withAlpha (0.07f));
        g.fillRect (xs[(size_t) i], plot.getY(), xs[(size_t) i + 1] - xs[(size_t) i], plot.getHeight());
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (xs[(size_t) i], plot.getBottom() - 2.0f, xs[(size_t) i + 1] - xs[(size_t) i], 3.0f);
        const auto width = xs[(size_t) i + 1] - xs[(size_t) i];
        if (width > 26.0f)
        {
            g.setColour (colour); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (width > 78.0f ? "BAND " + juce::String (i + 1) : juce::String (i + 1),
                        juce::Rectangle<float> (xs[(size_t) i] + 8.0f, plot.getY() + 6.0f, width - 12.0f, 20.0f), juce::Justification::centredLeft);
        }
    }
    g.setColour (juce::Colour (0xff929991).withAlpha (0.12f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        if (f < processor.nyquistFrequency()) g.drawVerticalLine (juce::roundToInt (xForFrequency (f)), plot.getY() + 29.0f, plot.getBottom());
    for (int row = 1; row <= 3; ++row)
        g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) row / 4.0f), plot.getX(), plot.getRight());
    if (spectrumVisible)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x30e8ece6), 0.0f, plot.getY() + 26.0f,
                                                 juce::Colour (0x06e8ece6), 0.0f, plot.getBottom(), false));
        g.fillPath (spectrumFill);
        g.setColour (juce::Colour (0xb8e2e6e0));
        g.strokePath (spectrumLine, juce::PathStrokeType (1.3f));
    }
    g.setFont (juce::FontOptions (10.0f)); g.setColour (juce::Colour (0xff969e98));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        if (f < processor.nyquistFrequency() * 0.8f)
            g.drawText (formatHz (f), juce::Rectangle<float> (xForFrequency (f) - 35, plot.getBottom() + 12, 70, 16), juce::Justification::centred);
    g.drawText ("20 Hz", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 12, 65, 16), juce::Justification::centredLeft);
    g.drawText (formatHz (processor.nyquistFrequency()), juce::Rectangle<float> (plot.getRight() - 90, plot.getBottom() + 12, 90, 16), juce::Justification::centredRight);
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const auto x = juce::jlimit (plot.getX(), juce::jmax (plot.getX(), plot.getRight() - pillWidth), xs[i + 1] - pillWidth * 0.5f);
        const auto y = pillTop ((int) i);
        g.setColour (juce::Colour (0xf0090b09)); g.fillRoundedRectangle (x, y, pillWidth, 24.0f, 4.0f);
        g.setColour (activeHandle == (int) i ? bandColour ((int) i) : juce::Colour (0xffe1e6e0));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (formatHz (cuts[i]), juce::Rectangle<float> (x, y, pillWidth, 24.0f), juce::Justification::centred);
    }
    paintMeters (g);
}

// ---- Handles --------------------------------------------------------------------------------
CrossoverDisplay::Handle::Handle (CrossoverDisplay& display, int which) : owner (display), index (which)
{
    setTitle ("Crossover " + juce::String (which + 1));
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}
void CrossoverDisplay::Handle::paint (juce::Graphics& g)
{
    const bool highlighted = isMouseOverOrDragging() || hasKeyboardFocus (false) || owner.activeHandle == index;
    const float centre = (float) getWidth() * 0.5f, bottom = (float) getHeight() - 10.0f;
    const float dash[] { 4.0f, 5.0f };
    const auto colour = bandColour (index);
    g.setColour (highlighted ? colour : juce::Colour (0xffb6bcb5));
    const auto pill = owner.pillTop (index) - (float) getY();
    g.drawDashedLine (juce::Line<float> (centre, 0.0f, centre, pill - 3.0f), dash, 2, highlighted ? 2.0f : 1.5f);
    g.drawDashedLine (juce::Line<float> (centre, pill + 27.0f, centre, bottom - 7.0f), dash, 2, highlighted ? 2.0f : 1.5f);
    g.setColour (juce::Colour (0xff111413)); g.fillEllipse (centre - 8, bottom - 8, 16, 16);
    g.setColour (highlighted ? colour : juce::Colour (0xffe3e8df));
    g.fillEllipse (centre - 5.5f, bottom - 5.5f, 11, 11);
    if (hasKeyboardFocus (false)) g.drawEllipse (centre - 9, bottom - 9, 18, 18, 1.0f);
}
bool CrossoverDisplay::Handle::hitTest (int x, int y)
{
    if (! getLocalBounds().contains (x, y)) return false;
    // Overlapping hit areas go to the nearest divider, so no handle is ever unreachable.
    const float position = (float) (getX() + x);
    int nearest = -1;
    float best = 1.0e9f;
    for (size_t i = 0; i < owner.cuts.size(); ++i)
    {
        const auto distance = std::abs (position - owner.xForFrequency (owner.cuts[i]));
        if (distance < best) { best = distance; nearest = (int) i; }
    }
    return nearest == index;
}
void CrossoverDisplay::Handle::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu()) { owner.showCrossoverMenu (index); return; }
    if (! event.mods.isLeftButtonDown()) return;
    const auto current = owner.processor.effectiveCrossovers();
    if (index >= (int) current.size()) return;
    grabOffset = owner.xForFrequency (current[(size_t) index]) - event.getEventRelativeTo (&owner).position.x;
    owner.beginCrossoverGesture (index);
}
void CrossoverDisplay::Handle::mouseDrag (const juce::MouseEvent& event)
{
    if (owner.activeHandle == index) owner.dragCrossoverTo (event.getEventRelativeTo (&owner).position.x + grabOffset);
}
void CrossoverDisplay::Handle::mouseUp (const juce::MouseEvent&) { owner.endCrossoverGesture(); }
void CrossoverDisplay::Handle::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown())
        owner.setCrossoverValue (index, TriadProcessor::defaultCrossover (index, owner.processor.getBandCount()));
}
bool CrossoverDisplay::Handle::keyPressed (const juce::KeyPress& key)
{
    const bool left = key.getKeyCode() == juce::KeyPress::leftKey;
    if (! left && key.getKeyCode() != juce::KeyPress::rightKey) return false;
    const auto current = owner.processor.effectiveCrossovers();
    if (index >= (int) current.size()) return false;
    const float step = (key.getModifiers().isShiftDown() ? 1.0f / 120.0f : 1.0f / 12.0f) * (left ? -1.0f : 1.0f);
    owner.setCrossoverValue (index, current[(size_t) index] * std::pow (2.0f, step));
    return true;
}
