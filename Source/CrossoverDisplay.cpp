#include "CrossoverDisplay.h"
#include "Look.h"

using namespace triad::ui;

CrossoverDisplay::CrossoverDisplay (std::unique_ptr<CrossoverHost> owned) : CrossoverDisplay (*owned)
{
    ownedHost = std::move (owned);
}
CrossoverDisplay::CrossoverDisplay (CrossoverHost& h) : host (h)
{
    setComponentID ("frequencyDisplay");
    setTitle ("Band crossover display");
    addButton.setTooltip ("Add a band (up to 10). Tip: double-click the display to split a band right there.");
    addButton.onClick = [this] { host.addBand(); refresh(); };
    addAndMakeVisible (addButton);
    preButton.setClickingTogglesState (false);
    preButton.setTooltip ("PRE: effects on the full sound before it is split into bands (Auto-Tune, a de-esser, a gate...). "
                          "Click to show or hide its strip.");
    preButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9dcd6));
    preButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111312));
    preButton.onClick = [this] { if (onTogglePre) onTogglePre(); };
    addChildComponent (preButton);
    preButton.setVisible (host.hasChains());
    postButton.setTooltip ("POST: effects on the whole sound after the bands come back together (a limiter, glue compression, a final EQ...). "
                           "Click to show or hide its strip.");
    postButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc3cde0));
    postButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111312));
    postButton.onClick = [this] { if (onTogglePost) onTogglePost(); };
    addChildComponent (postButton);
    postButton.setVisible (host.hasChains());
    liveButton.setClickingTogglesState (true);
    liveButton.setTooltip ("LIVE, for recording through Triad: effects that add latency are passed by, so Triad adds none. "
                           "Switch it off again for mixing.");
    liveButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc0392b));
    liveButton.onClick = [this] { host.setLiveMode (liveButton.getToggleState()); };
    addChildComponent (liveButton);
    liveButton.setVisible (host.hasLiveMode());
    meterTip.setInterceptsMouseClicks (true, false);
    addAndMakeVisible (meterTip);
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
    host.getSpectrum().active.store (true);
    refresh();
}
CrossoverDisplay::~CrossoverDisplay()
{
    host.getSpectrum().active.store (false);
    endCrossoverGesture();
    endSlopeGesture();
}
juce::Component* CrossoverDisplay::getHandle (int index) const
{
    return index >= 0 && index < (int) handles.size() ? handles[(size_t) index].get() : nullptr;
}
juce::Rectangle<float> CrossoverDisplay::plotBounds() const
{
    // Design: a lane under the spectrum for how far LFOs move the dividers (laneHeight), then the frequencies.
    return { 20.0f, 38.0f, juce::jmax (1.0f, (float) getWidth() - 40.0f - meterWidth), juce::jmax (1.0f, (float) getHeight() - 70.0f - laneHeight()) };
}
float CrossoverDisplay::xForFrequency (float frequency) const
{
    const auto plot = plotBounds();
    const auto maximum = host.nyquistFrequency();
    return plot.getX() + plot.getWidth() * std::log (juce::jlimit (20.0f, maximum, frequency) / 20.0f) / std::log (maximum / 20.0f);
}
float CrossoverDisplay::frequencyForX (float x) const
{
    const auto plot = plotBounds();
    const auto fraction = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return 20.0f * std::pow (host.nyquistFrequency() / 20.0f, fraction);
}
float CrossoverDisplay::constrainValue (int index, float f) const
{
    const auto current = host.effectiveCrossovers();
    if (index < 0 || index >= (int) current.size()) return f;
    const auto ceiling = juce::jmin (20000.0f, host.nyquistFrequency() * 0.9f);
    const auto lower = index == 0 ? 20.0f : current[(size_t) index - 1] * triad::minCrossoverRatio;
    const auto upper = index == (int) current.size() - 1 ? ceiling : current[(size_t) index + 1] / triad::minCrossoverRatio;
    return juce::jlimit (juce::jmin (lower, upper), upper, f);
}
void CrossoverDisplay::beginCrossoverGesture (int index)
{
    if (index < 0 || index >= (int) handles.size()) return;
    endCrossoverGesture();
    activeHandle = index;
    if (auto* p = host.crossoverParameter (index)) p->beginChangeGesture();
    repaint();
}
void CrossoverDisplay::dragCrossoverTo (float x)
{
    if (activeHandle < 0) return;
    if (auto* parameter = host.crossoverParameter (activeHandle))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (constrainValue (activeHandle, frequencyForX (x))));
    refresh();
}
void CrossoverDisplay::endCrossoverGesture()
{
    if (activeHandle < 0) return;
    const auto ended = activeHandle;
    activeHandle = -1;
    if (auto* p = host.crossoverParameter (ended)) p->endChangeGesture();
    repaint();
}
void CrossoverDisplay::setCrossoverValue (int index, float frequency)
{
    if (index < 0 || index >= host.getBandCount() - 1) return;
    beginCrossoverGesture (index);
    if (auto* parameter = host.crossoverParameter (index))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (constrainValue (index, frequency)));
    refresh();
    endCrossoverGesture();
}
// ---- Slopes ---------------------------------------------------------------------------------
// The slope dot moves on its divider between the soft end (top, 6 dB/oct) and the hardest end (96 dB/oct).
float CrossoverDisplay::slopeY (int order) const
{
    const auto plot = plotBounds();
    const float top = plot.getY() + 36.0f, bottom = plot.getY() + plot.getHeight() * 0.56f;
    return top + (bottom - top) * (float) (order - triad::CrossoverFilter::minOrder)
                                 / (float) (triad::CrossoverFilter::maxOrder - triad::CrossoverFilter::minOrder);
}
int CrossoverDisplay::slopeForY (float y) const
{
    const float top = slopeY (triad::CrossoverFilter::minOrder), bottom = slopeY (triad::CrossoverFilter::maxOrder);
    const auto steps = triad::CrossoverFilter::maxOrder - triad::CrossoverFilter::minOrder;
    return triad::CrossoverFilter::minOrder + juce::jlimit (0, steps, juce::roundToInt ((y - top) / juce::jmax (1.0f, bottom - top) * (float) steps));
}
void CrossoverDisplay::beginSlopeGesture (int index)
{
    endSlopeGesture();
    if (index < 0 || index >= (int) handles.size()) return;
    activeSlope = index;
    if (auto* p = host.slopeParameter (index)) p->beginChangeGesture();
    repaint();
}
void CrossoverDisplay::dragSlopeTo (int order)
{
    if (activeSlope < 0) return;
    order = juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, order);
    if (auto* p = host.slopeParameter (activeSlope))
        if (order != host.getCrossoverSlope (activeSlope))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) order));
    repaint();
}
void CrossoverDisplay::endSlopeGesture()
{
    if (activeSlope < 0) return;
    if (auto* p = host.slopeParameter (activeSlope)) p->endChangeGesture();
    activeSlope = -1;
    repaint();
}
void CrossoverDisplay::setSlope (int index, int order)
{
    beginSlopeGesture (index);
    dragSlopeTo (order);
    endSlopeGesture();
}
void CrossoverDisplay::showSlopeMenu (int index)
{
    if (index < 0 || index >= (int) cuts.size()) return;
    juce::PopupMenu menu;
    menu.addSectionHeader ("CROSSOVER " + juce::String (index + 1) + " SLOPE");
    addSlopeItems (menu, host.getCrossoverSlope (index), 0, cuts[(size_t) index], host.getSampleRateForDisplay());
    addAllDividerItems (menu, allDividersBase);
    juce::Component::SafePointer<CrossoverDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, index] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        if (choice > allDividersBase) { safe->host.setAllCrossoverSlopes (choice - allDividersBase); safe->refresh(); }
        else safe->setSlope (index, choice);
    });
}
// The quick list of slopes: 6 to 48 dB/oct, then the steeper, near-brickwall cuts up to 96 dB/oct.
void CrossoverDisplay::addSlopeItems (juce::PopupMenu& menu, int current, int idBase, float hz, double sampleRate)
{
    // Beside each slope: how much later the sound right at this crossover comes out (the phase of the bands
    // added back; it matters for the timing of a voice when the crossover sits in its lower notes).
    if (hz > 0.0f) menu.addSectionHeader ("RIGHT: SOUND AT " + formatHz (hz).toUpperCase() + " COMES OUT LATER BY");
    for (int order = triad::CrossoverFilter::minOrder; order <= triad::CrossoverFilter::maxOrder; ++order)
    {
        if (order == triad::CrossoverFilter::steepestClassic + 1) menu.addSectionHeader ("HARDER CUTS (NEAR BRICKWALL)");
        juce::PopupMenu::Item item (triad::slopeText (order) + (order == 1 ? "  (softest)" : order == triad::CrossoverFilter::defaultOrder ? "  (default)"
                                                                       : order == triad::CrossoverFilter::maxOrder ? "  (hardest)" : ""));
        item.itemID = idBase + order;
        item.isTicked = order == current;
        if (hz > 0.0f) item.shortcutKeyDescription = formatMs (triad::crossoverGroupDelayMs (order, hz, sampleRate));
        menu.addItem (std::move (item));
    }
}
void CrossoverDisplay::addAllDividerItems (juce::PopupMenu& menu, int idBase)
{
    menu.addSectionHeader ("ALL DIVIDERS");
    addItemWithRight (menu, idBase + triad::CrossoverFilter::maxOrder, "Hard wall", triad::slopeText (triad::CrossoverFilter::maxOrder));
    addItemWithRight (menu, idBase + triad::CrossoverFilter::defaultOrder, "Default", triad::slopeText (triad::CrossoverFilter::defaultOrder));
    addItemWithRight (menu, idBase + triad::CrossoverFilter::minOrder, "Soft", triad::slopeText (triad::CrossoverFilter::minOrder));
}
void CrossoverDisplay::setChainState (juce::TextButton& button, const juce::String& name, bool shown, int effects)
{
    const auto text = effects > 0 ? name + " " + juce::String (effects) : name;
    if (button.getButtonText() != text) button.setButtonText (text);
    button.setToggleState (shown, juce::dontSendNotification);
}
// Each slope readout sits beside its dot: to the right, else to the left, else a compact one ("24") on
// either side, whichever does not cover another readout or a frequency pill.
juce::Rectangle<float> CrossoverDisplay::readoutBoundsFor (int index, const std::vector<juce::Rectangle<float>>& taken) const
{
    const auto plot = plotBounds();
    constexpr float full = 70.0f, compact = 38.0f, height = 18.0f;
    const float line = xForFrequency (cuts[(size_t) index]);
    const float y = slopeY (host.getCrossoverSlope (index)) - height * 0.5f;
    // Besides the pills and earlier readouts, keep clear of the other dividers' lines and slope dots.
    auto obstacles = taken;
    for (int j = 0; j < (int) cuts.size(); ++j)
        if (j != index)
        {
            const float x = xForFrequency (cuts[(size_t) j]);
            obstacles.push_back ({ x - 1.5f, plot.getY(), 3.0f, plot.getHeight() });
            obstacles.push_back (juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ x, slopeY (host.getCrossoverSlope (j)) }));
        }
    auto fits = [&] (juce::Rectangle<float> r)
    {
        if (r.getX() < plot.getX() || r.getRight() > plot.getRight()) return false;
        for (const auto& t : obstacles) if (t.intersects (r.expanded (2.0f, 0.0f))) return false;
        return true;
    };
    for (float width : { full, compact })
        for (const auto& r : { juce::Rectangle<float> (line + 11.0f, y, width, height), juce::Rectangle<float> (line - 11.0f - width, y, width, height) })
            if (fits (r)) return r;
    const float x = juce::jlimit (plot.getX(), juce::jmax (plot.getX(), plot.getRight() - compact), line + 11.0f);
    return { x, y, compact, height };
}
juce::Rectangle<float> CrossoverDisplay::slopeReadoutBounds (int index) const
{
    if (index < 0 || index >= (int) cuts.size()) return {};
    auto taken = pillRects();
    juce::Rectangle<float> r;
    for (int i = 0; i <= index; ++i)
    {
        r = readoutBoundsFor (i, taken);
        taken.push_back (r);
    }
    return r;
}
void CrossoverDisplay::paintSlopeReadouts (juce::Graphics& g)
{
    auto taken = pillRects();
    for (int i = 0; i < (int) cuts.size(); ++i)
    {
        const auto r = readoutBoundsFor (i, taken);
        taken.push_back (r);
        const bool active = activeSlope == i || (i < (int) handles.size() && handles[(size_t) i]->isMouseOverOrDragging());
        g.setColour (currentPalette.pill.withAlpha (0.91f)); g.fillRoundedRectangle (r, 4.0f);
        if (active) { g.setColour (bandColour (i).withAlpha (0.8f)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f); }
        g.setColour (active ? bandColour (i) : currentPalette.text.interpolatedWith (currentPalette.softText, 0.35f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        const int order = host.getCrossoverSlope (i);
        g.drawText (r.getWidth() < 60.0f ? juce::String (triad::CrossoverFilter::dbPerOctave (order)) : triad::slopeText (order),
                    r.withTrimmedRight (12.0f), juce::Justification::centred);
        // A small chevron: click for the list of slopes.
        juce::Path chevron;
        const float cx = r.getRight() - 9.0f, cy = r.getCentreY();
        chevron.startNewSubPath (cx - 3.0f, cy - 1.5f); chevron.lineTo (cx, cy + 1.5f); chevron.lineTo (cx + 3.0f, cy - 1.5f);
        g.strokePath (chevron, juce::PathStrokeType (1.2f));
    }
}
// ---- Ghost movement (0.19.0): an LFO on the band edges shown behind the dividers, which stay where they are set ----
juce::ColourGradient CrossoverDisplay::litGradient (int b, const std::vector<float>& at, juce::Colour colour, float alpha) const
{
    // Across the plot: `colour` as strong as the band's response there (its filters at the crossovers `at`), so its
    // edges fade as its slopes do: soft (6 dB/oct) over octaves, a hard wall (96 dB/oct) in a quarter of one.
    const auto plot = plotBounds();
    juce::ColourGradient gradient (colour.withAlpha (0.0f), plot.getX(), 0.0f, colour.withAlpha (0.0f), plot.getRight(), 0.0f, false);
    gradient.clearColours();
    const int count = host.getBandCount();
    const double sr = host.getSampleRateForDisplay();
    constexpr int steps = 160;
    for (int i = 0; i <= steps; ++i)
    {
        const float f = frequencyForX (plot.getX() + plot.getWidth() * (float) i / (float) steps);
        float m = 1.0f;
        if (b > 0 && b - 1 < (int) at.size()) m *= triad::CrossoverFilter::highMagnitude (host.getCrossoverSlope (b - 1), f, at[(size_t) b - 1], sr);
        if (b < count - 1 && b < (int) at.size()) m *= triad::CrossoverFilter::lowMagnitude (host.getCrossoverSlope (b), f, at[(size_t) b], sr);
        gradient.addColour ((double) i / steps, colour.withAlpha (alpha * std::pow (juce::jlimit (0.0f, 1.0f, m), 0.8f)));
    }
    return gradient;
}
int CrossoverDisplay::lfoBandOf (int j) const
{
    if (j < 0 || j >= (int) sweeps.size() || sweeps[(size_t) j].second <= 0.0f) return -1;
    const bool fromBelow = host.edgeLfo (j, 1) >= 0 || host.edgeLfo (j, 2) >= 0 || host.edgeLfo (j, 3) >= 0;
    return fromBelow ? j : j + 1;
}
juce::Path CrossoverDisplay::bandResponse (int b, const std::vector<float>& at, bool fill, bool floor, float height) const
{
    // As paintBandCurves draws a band, at the crossovers `at` (height < 1: a smaller one, from the floor up).
    const auto plot = plotBounds();
    const int count = host.getBandCount();
    const double sr = host.getSampleRateForDisplay();
    const float bottom = plot.getBottom() - 3.0f, top = bottom - (bottom - (plot.getY() + 30.0f)) * height;
    constexpr float rangeDb = 30.0f;
    juce::Path p;
    bool started = false;
    if (fill) { p.startNewSubPath (plot.getX(), bottom); started = true; }
    for (float x = plot.getX(); x <= plot.getRight() + 0.5f; x += 2.0f)
    {
        const float f = frequencyForX (x);
        float magnitude = 1.0f;
        if (b > 0 && b - 1 < (int) at.size()) magnitude *= triad::CrossoverFilter::highMagnitude (host.getCrossoverSlope (b - 1), f, at[(size_t) b - 1], sr);
        if (b < count - 1 && b < (int) at.size()) magnitude *= triad::CrossoverFilter::lowMagnitude (host.getCrossoverSlope (b), f, at[(size_t) b], sr);
        const float db = juce::jlimit (-rangeDb, 0.0f, juce::Decibels::gainToDecibels (magnitude, -100.0f));
        if (! fill && ! floor && db <= -rangeDb + 0.05f) { started = false; continue; }
        const juce::Point<float> pt (x, top + (bottom - top) * (-db / rangeDb));
        if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
    }
    if (fill) { p.lineTo (plot.getRight(), bottom); p.closeSubPath(); }
    return p;
}
void CrossoverDisplay::paintGhost (juce::Graphics& g, const std::vector<float>& heard)
{
    const auto plot = plotBounds();
    const int count = host.getBandCount();
    if ((int) heard.size() != (int) cuts.size()) return;
    std::vector<int> moving;
    for (int j = 0; j < (int) cuts.size(); ++j)
        if (const int b = lfoBandOf (j); b >= 0 && std::find (moving.begin(), moving.end(), b) == moving.end()) moving.push_back (b);
    if (moving.empty()) return;
    const float dash[] { 4.0f, 4.0f };
    juce::ignoreUnused (dash);
    // Thomas: the band as set stays as it is; the ghost is the quiet one, smaller, more see-through, its slopes dotted.
    constexpr float size = 0.62f;   // of the bands' height
    for (int b : moving)
    {
        const auto colour = bandColour (b);
        g.setColour (colour.withAlpha (0.1f));
        g.fillPath (bandResponse (b, heard, true, true, size));
        juce::Path dots;
        const float dotted[] { 0.01f, 4.5f };
        juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createDashedStroke (dots, bandResponse (b, heard, false, false, size), dotted, 2);
        g.setColour (colour.withAlpha (0.85f));
        g.fillPath (dots);
    }
    // On the lane under the spectrum: a dot where each moved divider is right now.
    for (const auto& lane : laneRanges())
    {
        if (lane.divider >= (int) heard.size()) continue;
        const float x = juce::jlimit (lane.x1, lane.x2, xForFrequency (heard[(size_t) lane.divider]));
        const int b = lfoBandOf (lane.divider);
        g.setColour (currentPalette.text);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, lane.y }));
        g.setColour (bandColour (juce::jmax (0, b)));
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, lane.y }), 1.5f);
        if (laneLabelBounds (lane).expanded (4.0f, 0.0f).contains (x, lane.y)) paintLaneLabel (g, lane);   // the dot passes behind its numbers
    }
}
// Each band's response (the product of its low- and high-pass) as a filled curve in its colour.
void CrossoverDisplay::paintBandCurves (juce::Graphics& g)
{
    const auto plot = plotBounds();
    const int count = host.getBandCount();
    const double sr = host.getSampleRateForDisplay();
    std::vector<int> slopes;
    for (int j = 0; j + 1 < count; ++j) slopes.push_back (host.getCrossoverSlope (j));
    std::vector<float> key { (float) count, plot.getWidth(), plot.getHeight(), (float) sr, (float) activeSlope };
    key.insert (key.end(), cuts.begin(), cuts.end());
    for (auto o : slopes) key.push_back ((float) o);
    if (key != curveKey)
    {
        curveKey = key;
        curveFills.assign ((size_t) count, {});
        curveLines.assign ((size_t) count, {});
        const float top = plot.getY() + 30.0f, bottom = plot.getBottom() - 3.0f;
        constexpr float rangeDb = 30.0f;
        for (int b = 0; b < count; ++b)
        {
            auto& fill = curveFills[(size_t) b];
            auto& line = curveLines[(size_t) b];
            fill.startNewSubPath (plot.getX(), bottom);
            for (float x = plot.getX(); x <= plot.getRight() + 0.5f; x += 2.0f)
            {
                const float f = frequencyForX (x);
                float magnitude = 1.0f;
                if (b > 0 && b - 1 < (int) cuts.size())
                    magnitude *= triad::CrossoverFilter::highMagnitude (slopes[(size_t) b - 1], f, cuts[(size_t) b - 1], sr);
                if (b < (int) cuts.size())
                    magnitude *= triad::CrossoverFilter::lowMagnitude (slopes[(size_t) b], f, cuts[(size_t) b], sr);
                const float db = juce::jlimit (-rangeDb, 0.0f, juce::Decibels::gainToDecibels (magnitude, -100.0f));
                const float y = top + (bottom - top) * (-db / rangeDb);
                fill.lineTo (x, y);
                if (line.isEmpty()) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            fill.lineTo (plot.getRight(), bottom);
            fill.closeSubPath();
        }
    }
    // The bands as see-through colours crossing, without outlines (0.16.0, Thomas: "no outline on the band colours").
    // While a slope is being set, the two bands it joins stand out.
    for (int b = 0; b < count && b < (int) curveFills.size(); ++b)
    {
        const bool joined = activeSlope >= 0 && (b == activeSlope || b == activeSlope + 1);
        const bool dimmed = activeSlope >= 0 && ! joined;
        if (! host.isBandHeard (b))
        {
            // Not heard (muted, or another band soloed): its colour drops out, a faint grey outline stays.
            g.setColour (currentPalette.gridLines.withAlpha (0.10f));
            g.strokePath (curveLines[(size_t) b], juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
            continue;
        }
        g.setColour (bandColour (b).withAlpha (joined ? 0.3f : dimmed ? 0.07f : 0.17f));
        g.fillPath (curveFills[(size_t) b]);
    }
}

void CrossoverDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) cuts.size(); ++i)
        if (slopeReadoutBounds (i).contains (e.position)) return;
    if (! plotBounds().contains (e.position) || e.mods.isPopupMenu()) return;
    host.addBand (frequencyForX (e.position.x));
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
    // A slope readout: pick the slope from a list.
    for (int i = 0; i < (int) cuts.size(); ++i)
        if (slopeReadoutBounds (i).contains (e.position)) { showSlopeMenu (i); return; }
    // Right-click a band's area: remove it (or add a band right there).
    if (e.mods.isPopupMenu() && plotBounds().contains (e.position)) { showBandMenu (bandAt (e.position.x), frequencyForX (e.position.x)); return; }
    // An end of a range on the lane: drag it to change where the LFO takes the divider (0.21.0).
    if (! e.mods.isPopupMenu())
        if (const auto end = laneEndAt (e.position); end.divider >= 0 && host.beginSweepDrag (end.divider, end.high))
        {
            laneDrag = end;
            laneHover = {};
            repaint();
        }
}
void CrossoverDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (laneDrag.divider < 0) return;
    host.dragSweepTo (frequencyForX (e.position.x));
    for (int j = 0; j < (int) cuts.size() && j < (int) sweeps.size(); ++j) sweeps[(size_t) j] = host.crossoverSweep (j);
    repaint();
}
void CrossoverDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (laneDrag.divider < 0) return;
    host.endSweepDrag();
    laneDrag = {};
    refresh();
    mouseMove (e);
}
void CrossoverDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto end = laneEndAt (e.position);
    if (end.divider != laneHover.divider || end.high != laneHover.high)
    {
        laneHover = end;
        setMouseCursor (end.divider >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}
void CrossoverDisplay::mouseExit (const juce::MouseEvent&)
{
    if (laneHover.divider < 0) return;
    laneHover = {};
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}
juce::String CrossoverDisplay::getTooltip()
{
    if (laneHover.divider < 0) return {};
    const auto who = host.sweepDescription (laneHover.divider);
    return juce::String (laneHover.high ? "The top" : "The bottom") + " of where " + (who.isEmpty() ? juce::String ("its LFO") : who)
         + " takes divider " + juce::String (laneHover.divider + 1) + ". Drag left or right to change it (the LFO's wave stretches to match).";
}
juce::Rectangle<float> CrossoverDisplay::laneLabelBounds (const LaneRange& lane) const
{
    if (lane.x2 - lane.x1 <= lane.labelWidth + 20.0f) return {};   // no room: none
    return juce::Rectangle<float> (lane.labelWidth, 14.0f).withCentre ({ (lane.x1 + lane.x2) * 0.5f, lane.y });
}
void CrossoverDisplay::paintLaneLabel (juce::Graphics& g, const LaneRange& lane) const
{
    const auto pill = laneLabelBounds (lane);
    if (pill.isEmpty()) return;
    const int j = lane.divider;
    const bool fromBelow = host.edgeLfo (j, 1) >= 0 || host.edgeLfo (j, 2) >= 0 || host.edgeLfo (j, 3) >= 0;
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (currentPalette.grid); g.fillRoundedRectangle (pill, 4.0f);
    g.setColour (bandColour (fromBelow ? j : j + 1)); g.drawText (lane.label, pill, juce::Justification::centred);
}
std::vector<CrossoverDisplay::LaneRange> CrossoverDisplay::laneRanges() const
{
    // As drawn: overlapping ranges (with their labels) take a second row.
    std::vector<LaneRange> lanes;
    const auto plot = plotBounds();
    const juce::Font font (juce::FontOptions (10.5f, juce::Font::bold));
    std::array<float, 2> rowRight { -1.0e9f, -1.0e9f };
    for (int j = 0; j < (int) sweeps.size(); ++j)
    {
        const auto [low, high] = sweeps[(size_t) j];
        if (high <= 0.0f) continue;
        LaneRange lane;
        lane.divider = j;
        lane.x1 = xForFrequency (low);
        lane.x2 = juce::jmax (lane.x1 + 3.0f, xForFrequency (high));
        lane.label = formatHz (low) + juce::String::fromUTF8 (" \xe2\x86\x94 ") + formatHz (high);
        lane.labelWidth = juce::GlyphArrangement::getStringWidth (font, lane.label) + 12.0f;
        const int row = lane.x1 - 8.0f > rowRight[0] ? 0 : 1;
        lane.y = plot.getBottom() + 9.0f + (float) row * 14.0f;
        rowRight[(size_t) row] = juce::jmax (lane.x2, (lane.x1 + lane.x2) * 0.5f + lane.labelWidth * 0.5f);
        lanes.push_back (lane);
    }
    return lanes;
}
CrossoverDisplay::LaneEnd CrossoverDisplay::laneEndAt (juce::Point<float> p) const
{
    const auto plot = plotBounds();
    if (p.y < plot.getBottom() + 1.0f || p.y > plot.getBottom() + laneHeight() + 2.0f) return {};
    LaneEnd best;
    float bestDistance = 9.0f;   // pixels from a cap
    for (const auto& lane : laneRanges())
    {
        if (std::abs (p.y - lane.y) > 8.0f) continue;
        for (const bool high : { false, true })
            if (const float d = std::abs (p.x - (high ? lane.x2 : lane.x1)); d < bestDistance || (d <= bestDistance && high == (p.x > (lane.x1 + lane.x2) * 0.5f)))
            { bestDistance = d; best = { lane.divider, high }; }
    }
    return best;
}
juce::Point<float> CrossoverDisplay::laneEndPosition (LaneEnd end) const
{
    for (const auto& lane : laneRanges())
        if (lane.divider == end.divider) return { end.high ? lane.x2 : lane.x1, lane.y };
    return {};
}
void CrossoverDisplay::addLinkItems (juce::PopupMenu& menu, CrossoverHost& h, int band, int idBase)
{
    // ~ Move the band / Open / close / Low edge / High edge, each with where it is now (or its LFO, once linked).
    const auto cuts = h.effectiveCrossovers();
    const int count = h.getBandCount();
    // Design: what each does, in words and as a small filter picture (Thomas: "make it a lot more legible").
    menu.addSectionHeader (menuLfo ("AN LFO ON BAND " + juce::String (band + 1) + "'S RANGE"));
    const auto lo = band > 0 ? formatHz (cuts[(size_t) band - 1]) : juce::String ("20 Hz");
    const auto hi = band < count - 1 ? formatHz (cuts[(size_t) band]) : juce::String ("top");
    for (int edge : { 2, 3, 0, 1 })
    {
        if ((edge == 0 && band == 0) || (edge == 1 && band >= count - 1)) continue;
        const juce::String names[] { "Low cut", "High cut", "Sweep the band", "Widen / narrow" };
        const char kinds[] { 'l', 'h', 's', 'w' };
        juce::String right = edge == 0 ? "moves " + lo : edge == 1 ? "moves " + hi : edge == 2 ? lo + juce::String::fromUTF8 (" \xe2\x80\x93 ") + hi + " together"
                                                                                            : "edges apart or in";
        if (const int lfo = h.edgeLfo (band, edge); lfo >= 0) right = "LFO " + juce::String (lfo + 1);
        addItemWithRight (menu, idBase + edge, menuFilter (kinds[edge], names[edge]), right);
    }
    // LOCK (0.19.0): the band's LFOs move its edges only inside the band as set (once it has one that can be locked).
    if (const int state = h.edgeLockState (band); state >= 0)
        addItemWithRight (menu, idBase + lockItem, "Lock LFOs inside the band", lo + juce::String::fromUTF8 (" \xe2\x80\x93 ") + hi, true, state == 1);
}
void CrossoverDisplay::showBandMenu (int band, float hz)
{
    enum { removeId = 1, addId, lfoId, copyId, pasteId, resetId, ghostId, linkBase = 200 };
    const int count = host.getBandCount();
    juce::PopupMenu menu;
    {
        // Design: the band and its range as the menu's title.
        const auto cuts = host.effectiveCrossovers();
        const auto lo = band > 0 && band - 1 < (int) cuts.size() ? formatHz (cuts[(size_t) band - 1]) : juce::String ("0 Hz");
        const auto hi = band < (int) cuts.size() ? formatHz (cuts[(size_t) band]) : formatHz (host.nyquistFrequency());
        menu.addSectionHeader ("Band " + juce::String (band + 1) + juce::String::fromUTF8 ("  \xc2\xb7  ") + lo + juce::String::fromUTF8 (" \xe2\x80\x93 ") + hi);
    }
    if (host.hasBandState())
    {
        menu.addItem (copyId, "Copy band state");
        menu.addItem (pasteId, host.canPasteBandState() ? "Paste band state (" + host.pasteBandDescription() + ")"
                                                         : juce::String ("Paste band state"),
                      host.canPasteBandState());
        menu.addItem (resetId, "Reset band state");   // 0.20.0: as a new band
        menu.addSeparator();
    }
    if (host.canLinkToLfo() && count > 1)
    {
        // 0.18.0 (Thomas): "link to LFO" on the band's edges, with the LFO's wave as on a slot's ~ button.
        addLinkItems (menu, host, band, linkBase);
        // 0.20.0 (Thomas): the ghost behind the dividers, hidden or shown from here too (as the gear menu).
        addItemWithRight (menu, ghostId, "Ghost movement", "the band moving, behind", true, ghostMovement);
        menu.addSeparator();
    }
    menu.addItem (removeId, "Remove band " + juce::String (band + 1), count > 1);
    menu.addItem (addId, "Add a band here (" + formatHz (hz) + ")", count < triad::maxBands);
    if (host.hasBandState()) menu.addItem (lfoId, "Edit LFO for band " + juce::String (band + 1) + " mix...");
    if (count > 1)
    {
        menu.addSeparator();
        addAllDividerItems (menu, allDividersBase);
    }
    juce::Component::SafePointer<CrossoverDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, band, hz] (int choice)
    {
        if (safe == nullptr) return;
        if (choice > allDividersBase) { safe->host.setAllCrossoverSlopes (choice - allDividersBase); safe->refresh(); return; }
        if (choice >= linkBase && choice < linkBase + 4) { if (safe->onLinkEdges) safe->onLinkEdges (band, choice - linkBase); return; }
        if (choice == linkBase + lockItem) { safe->host.setEdgesLocked (band, safe->host.edgeLockState (band) != 1); safe->refresh(); return; }
        if (choice == copyId) { safe->host.copyBandState (band); return; }
        if (choice == resetId) safe->host.resetBandState (band);
        else if (choice == ghostId) { ghostMovement = ! ghostMovement; safe->host.setGhostMovement (ghostMovement); }
        if (choice == pasteId) safe->host.pasteBandState (band);
        else if (choice == resetId || choice == ghostId) {}
        else if (choice == removeId) safe->host.removeBand (band);
        else if (choice == addId) safe->host.addBand (hz);
        else if (choice == lfoId) { if (safe->onEditBandLfo) safe->onEditBandLfo (band); return; }
        safe->refresh();
    });
}
void CrossoverDisplay::showCrossoverMenu (int index)
{
    enum { removeBelow = 1, removeAbove, resetId, linkId, slopeBase = 100 };
    juce::PopupMenu menu;
    menu.addSectionHeader ("CROSSOVER " + juce::String (index + 1) + "  " + (index < (int) cuts.size() ? formatHz (cuts[(size_t) index]) : juce::String()));
    juce::PopupMenu slopes;
    const int current = host.getCrossoverSlope (index);
    addSlopeItems (slopes, current, slopeBase, index < (int) cuts.size() ? cuts[(size_t) index] : 0.0f, host.getSampleRateForDisplay());
    addSubMenu (menu, "Slope", std::move (slopes), triad::slopeText (current));
    if (host.canLinkToLfo())
    {
        // This divider is band index's high edge (and the next band's low edge): its LFO is one of band index's.
        const int lfo = host.edgeLfo (index, 1);
        addItemWithRight (menu, linkId, menuLfo ("Link to LFO"), lfo >= 0 ? "LFO " + juce::String (lfo + 1) : "band " + juce::String (index + 1) + "'s high edge");
    }
    addAllDividerItems (menu, allDividersBase);
    menu.addSeparator();
    menu.addItem (removeBelow, "Remove band " + juce::String (index + 1) + " (below)");
    menu.addItem (removeAbove, "Remove band " + juce::String (index + 2) + " (above)");
    menu.addItem (resetId, "Reset this crossover");
    juce::Component::SafePointer<CrossoverDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, index] (int choice)
    {
        if (safe == nullptr) return;
        if (choice > allDividersBase) { safe->host.setAllCrossoverSlopes (choice - allDividersBase); safe->refresh(); return; }
        if (choice > slopeBase) { safe->setSlope (index, choice - slopeBase); return; }
        if (choice == linkId) { if (safe->onLinkEdges) safe->onLinkEdges (index, 1); return; }
        if (choice == removeBelow) safe->host.removeBand (index);
        else if (choice == removeAbove) safe->host.removeBand (index + 1);
        else if (choice == resetId)
        {
            safe->setCrossoverValue (index, triad::defaultCrossover (index, safe->host.getBandCount()));
            safe->setSlope (index, triad::CrossoverFilter::defaultOrder);
        }
        safe->refresh();
    });
}
void CrossoverDisplay::Meter::push (float v, double dt)
{
    // Timed by the frame clock: about -50 dB a second fall, peak held for 1.5 s.
    level = juce::jmax (v, level * (float) std::pow (0.82, dt * 30.0));
    if (v >= hold) { hold = v; holdTime = 1.5; }
    else if ((holdTime -= dt) < 0.0) hold *= (float) std::pow (0.9, dt * 30.0);
}
void CrossoverDisplay::setMeters (float input, float output, float cpu, double dt)
{
    inputMeter.push (input, dt);
    outputMeter.push (output, dt);
    // The CPU reading glides (rises within about 0.1 s, falls over about half a second).
    const float k = (float) (1.0 - std::exp (-dt / (cpu > cpuShown ? 0.08 : 0.4)));
    cpuShown += k * (cpu - cpuShown);
    if (host.hasLiveMode()) liveButton.setToggleState (host.isLiveMode(), juce::dontSendNotification);
}
void CrossoverDisplay::setLatency (int latency, double sampleRate, const juce::String& breakdown)
{
    if (latency != latencySamples || ! juce::exactlyEqual (sampleRate, latencyRate)) repaint (getWidth() - (int) meterWidth - 24, 0, (int) meterWidth + 24, getHeight());
    latencySamples = latency;
    latencyRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto name = host.getProductName();
    meterTip.setTooltip ("Latency: " + getLatencyText() + (latency == 0 ? " - " + name + " adds none, safe to sing or play through."
                                                                         : " (" + juce::String (latency) + " samples) - the effects' own latency; " + name + " adds none.\n" + breakdown)
                         + "\nCPU: the share of each audio buffer's time " + name + " needs (" + juce::String (juce::roundToInt (cpuShown * 100.0f)) + "% now).");
}
juce::String CrossoverDisplay::getLatencyText() const
{
    const double ms = 1000.0 * latencySamples / latencyRate;
    return latencySamples == 0 ? juce::String ("0 ms") : triad::ui::formatMs (ms);
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
    // Latency (LAT) and CPU, beside IN and OUT. Latency: green at zero (safe to record through), amber,
    // red above 10 ms (too late to sing through); the bar grows on a square-root scale up to 20 ms.
    {
        const double ms = 1000.0 * latencySamples / latencyRate;
        const auto lat = juce::Rectangle<float> (x0 + 56.0f, top, 12.0f, bottom - top);
        const auto green = juce::Colour (0xff9bd85c), amber = juce::Colour (0xffe8b35c), red = juce::Colour (0xffe8705c);
        const auto latColour = latencySamples == 0 ? green : ms > 10.0 ? red : amber;
        g.setColour (currentPalette.meterBack); g.fillRoundedRectangle (lat, 2.0f);
        if (latencySamples == 0)
        {
            g.setColour (green.withAlpha (0.35f)); g.drawRoundedRectangle (lat.reduced (0.5f), 2.0f, 1.0f);
            g.setColour (green); g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ lat.getCentreX(), bottom - 6.0f }));
        }
        else
        {
            const float f = std::sqrt ((float) juce::jmin (1.0, ms / 20.0));
            g.setColour (latColour); g.fillRoundedRectangle (lat.withTop (bottom - (bottom - top) * juce::jmax (0.04f, f)), 2.0f);
        }
        const auto cpuBar = juce::Rectangle<float> (x0 + 84.0f, top, 12.0f, bottom - top);
        const float load = juce::jlimit (0.0f, 1.0f, cpuShown);
        g.setColour (currentPalette.meterBack); g.fillRoundedRectangle (cpuBar, 2.0f);
        if (load > 0.002f)
        {
            g.setColour (load < 0.5f ? green.withAlpha (0.8f) : load < 0.8f ? amber : red);
            g.fillRoundedRectangle (cpuBar.withTop (bottom - (bottom - top) * juce::jmax (0.02f, load)), 2.0f);
        }
        g.setColour (currentPalette.softText); g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("LAT", juce::Rectangle<float> (lat.getX() - 8.0f, top - 18.0f, 28.0f, 14.0f), juce::Justification::centred);
        g.drawText ("CPU", juce::Rectangle<float> (cpuBar.getX() - 8.0f, top - 18.0f, 28.0f, 14.0f), juce::Justification::centred);
        g.setFont (juce::FontOptions (9.0f));
        g.setColour (latColour);
        g.drawText (latencySamples == 0 ? juce::String ("0ms") : triad::ui::formatMs (ms).removeCharacters (" "),
                    juce::Rectangle<float> (lat.getX() - 11.0f, bottom + 10.0f, 34.0f, 14.0f), juce::Justification::centred);
        g.setColour (dimTextColour);
        g.drawText (juce::String (juce::roundToInt (cpuShown * 100.0f)) + "%", juce::Rectangle<float> (cpuBar.getX() - 11.0f, bottom + 10.0f, 34.0f, 14.0f),
                    juce::Justification::centred);
    }
    const char* labels[] { "IN", "OUT" };
    const Meter* meters[] { &inputMeter, &outputMeter };
    for (int i = 0; i < 2; ++i)
    {
        const float x = x0 + (float) i * 28.0f;
        const auto bar = juce::Rectangle<float> (x, top, 12.0f, bottom - top);
        g.setColour (currentPalette.meterBack); g.fillRoundedRectangle (bar, 2.0f);
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
        g.setColour (currentPalette.softText); g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (labels[i], juce::Rectangle<float> (x - 8.0f, top - 18.0f, 28.0f, 14.0f), juce::Justification::centred);
        g.setColour (dimTextColour); g.setFont (juce::FontOptions (9.0f));
        const auto text = m.hold < 1.0e-4f ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (m.hold), 1);
        g.drawText (text, juce::Rectangle<float> (x - 9.0f, bottom + 10.0f, 30.0f, 14.0f), juce::Justification::centred);
    }
    g.setColour (currentPalette.separator);
    g.drawVerticalLine (juce::roundToInt (x0 - 10.0f), top - 18.0f, bottom + 22.0f);
}
void CrossoverDisplay::refreshIfChanged()
{
    std::vector<float> key { (float) host.getBandCount(), host.nyquistFrequency() };
    for (auto c : host.effectiveCrossovers()) key.push_back (c);
    for (int j = 0; j + 1 < host.getBandCount(); ++j) key.push_back ((float) host.getCrossoverSlope (j));
    for (int j = 0; j + 1 < host.getBandCount(); ++j) { const auto r = host.crossoverSweep (j); key.push_back (r.first); key.push_back (r.second); }
    for (int b = 0; b < host.getBandCount(); ++b) key.push_back (host.isBandHeard (b) ? 1.0f : 0.0f);
    if (key == refreshKey) return;
    refreshKey = key;
    refresh();
}
void CrossoverDisplay::refresh()
{
    const int wanted = juce::jmax (0, host.getBandCount() - 1);
    if ((int) handles.size() != wanted)
    {
        endCrossoverGesture();
        endSlopeGesture();
        handles.clear();
        for (int i = 0; i < wanted; ++i)
        {
            handles.push_back (std::make_unique<Handle> (*this, i));
            addAndMakeVisible (*handles.back());
        }
    }
    addButton.setEnabled (host.getBandCount() < triad::maxBands);
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
    postButton.setBounds (addButton.getX() - 70, 7, 64, 24);
    preButton.setBounds (postButton.getX() - 64, 7, 58, 24);
    {
        const int x0 = getWidth() - 18 - (int) meterWidth + 12;
        liveButton.setBounds (x0 + 50, 6, 52, 20);
        const auto plot = plotBounds();
        meterTip.setBounds (x0 + 46, (int) plot.getY() - 12, 56, (int) plot.getHeight() + 40);
    }
    cuts = host.effectiveCrossovers();
    sweeps.clear();
    for (int j = 0; j < (int) cuts.size(); ++j) sweeps.push_back (host.crossoverSweep (j));
    layoutPills();
    const auto plot = plotBounds();
    for (size_t i = 0; i < handles.size() && i < cuts.size(); ++i)
    {
        const auto f = cuts[i];
        handles[i]->setBounds (juce::roundToInt (xForFrequency (f)) - 12, juce::roundToInt (plot.getY()), 24, juce::roundToInt (plot.getHeight()) + 10);
        handles[i]->setDescription ("Crossover " + juce::String ((int) i + 1) + ": " + formatHz (f) + ", slope "
                                    + triad::slopeText (host.getCrossoverSlope ((int) i))
                                    + ". Drag left or right; left/right arrows adjust (Shift: finer). Drag the upper dot, or up/down arrows, for the slope.");
    }
    // The selected handle stays reachable even when two boundaries are close.
    if (activeHandle >= 0 && activeHandle < (int) handles.size()) handles[(size_t) activeHandle]->toFront (false);
}

// ---- Live spectrum --------------------------------------------------------------------------
void CrossoverDisplay::frame (double time)
{
    // Every frame of the screen, up to 120 a second while the computer keeps up (see FramePacer).
    const double dt = lastFrame > 0.0 ? juce::jlimit (0.0, 0.1, time - lastFrame) : 1.0 / 60.0;
    if (! pacer->due (lastFrame, time)) return;
    const FramePacer::Timed timed (*pacer);
    if (onFrame) onFrame (dt);
    updateSpectrum (dt);
    repaint (plotBounds().withHeight (plotBounds().getHeight() + laneHeight()).toNearestInt().expanded (2));   // the lane's dots move too
    repaint (getWidth() - (int) meterWidth - 24, 0, (int) meterWidth + 24, getHeight());
}
void CrossoverDisplay::updateSpectrum (double dt)
{
    const float decayPerFrame = (float) (36.0 * dt);   // dB: -36 dB a second
    const auto writeIndex = host.getSpectrum().getWriteIndex();
    const bool fresh = writeIndex != lastWriteIndex;
    lastWriteIndex = writeIndex;
    const auto sampleRate = (float) host.getSampleRateForDisplay();
    const auto binHz = sampleRate / (float) fftSize;
    if (fresh && host.getSpectrum().read (samples.data(), fftSize))
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

// The parts that only change when the bands do (background, band curves, band labels, grid) are drawn
// once into an image at the screen's pixel scale; each frame then only draws the spectrum, labels and meters.
void CrossoverDisplay::paintStaticLayer (juce::Graphics& g)
{
    const auto plot = plotBounds();
    const int count = host.getBandCount();
    std::vector<float> xs { plot.getX() };
    for (auto c : cuts) xs.push_back (xForFrequency (c));
    xs.push_back (plot.getRight());
    g.setColour (currentPalette.grid); g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    g.setColour (currentPalette.softText); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("BAND CROSSOVERS", 18, 8, 175, 23, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.0f)); g.setColour (currentPalette.faintText);
    g.drawFittedText ("Drag a divider  /  Upper dot: slope  /  Double-click: add a band  /  Right-click: menu",
                      juce::Rectangle<int> (200, 8, juce::jmax (40, getWidth() - 96 - (int) meterWidth - 134 - 8 - 200), 23),
                      juce::Justification::centredRight, 1, 0.8f);
    paintBandCurves (g);
    for (int i = 0; i < count && i + 1 < (int) xs.size(); ++i)
    {
        const bool heard = host.isBandHeard (i);
        const auto colour = heard ? bandColour (i) : currentPalette.faintText.withAlpha (0.6f);
        g.setColour (colour.withAlpha (heard ? 0.8f : 0.5f));
        g.fillRect (xs[(size_t) i], plot.getBottom() - 2.0f, xs[(size_t) i + 1] - xs[(size_t) i], 3.0f);
        const auto width = xs[(size_t) i + 1] - xs[(size_t) i];
        if (width > 26.0f)
        {
            g.setColour (colour); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (width > 78.0f ? "BAND " + juce::String (i + 1) : juce::String (i + 1),
                        juce::Rectangle<float> (xs[(size_t) i] + 8.0f, plot.getY() + 6.0f, width - 12.0f, 20.0f), juce::Justification::centredLeft);
        }
    }
    // Where LFOs take each divider (0.18.0): a faint light across its sweep with thin lines at its ends, and the
    // LFO's wave on a small tag beside the divider's slope dot.
    for (int j = 0; j < (int) sweeps.size() && j + 1 < (int) xs.size(); ++j)
    {
        const auto [low, high] = sweeps[(size_t) j];
        if (high <= 0.0f) continue;
        const float x1 = xForFrequency (low), x2 = juce::jmax (x1 + 2.0f, xForFrequency (high));
        // In the colour of the band whose LFO moves it (the band above, when only its low edge moves).
        const bool fromBelow = host.edgeLfo (j, 1) >= 0 || host.edgeLfo (j, 2) >= 0 || host.edgeLfo (j, 3) >= 0;
        const auto colour = bandColour (fromBelow ? j : j + 1);
        const float top = plot.getY() + 29.0f;
        g.setColour (colour.withAlpha (0.45f));
        const float dash[] { 2.0f, 4.0f };
        for (float x : { x1, x2 }) g.drawDashedLine (juce::Line<float> (x, top, x, plot.getBottom()), dash, 2, 1.0f);
        const float y = slopeY (host.getCrossoverSlope (j));
        const auto tag = juce::Rectangle<float> (xs[(size_t) j + 1] - 38.0f, y - 7.5f, 24.0f, 15.0f);
        g.setColour (currentPalette.grid.withAlpha (0.92f)); g.fillRoundedRectangle (tag, 4.5f);
        g.setColour (colour.withAlpha (0.6f)); g.drawRoundedRectangle (tag.reduced (0.5f), 4.5f, 1.0f);
        IconButton::drawIcon (g, IconButton::Icon::lfo, tag.reduced (5.0f, 4.0f), colour);
    }
    // The lane under the spectrum (design, Thomas): each moved divider's range as a line with end caps, in the colour of
    // the band whose LFO moves it, with its two frequencies; overlapping ranges take a second row. 0.21.0 (Thomas: "make
    // that slightly more visible and clickable"): a thicker line, and caps to grab (drag them to change the range).
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (const auto& lane : laneRanges())
    {
        const int j = lane.divider;
        const bool fromBelow = host.edgeLfo (j, 1) >= 0 || host.edgeLfo (j, 2) >= 0 || host.edgeLfo (j, 3) >= 0;
        const auto colour = bandColour (fromBelow ? j : j + 1);
        const float x1 = lane.x1, x2 = lane.x2, y = lane.y;
        g.setColour (colour.withAlpha (0.22f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x1, y - 4.0f, x2 - x1, 8.0f), 4.0f);   // a soft glow under the line
        g.setColour (colour.withAlpha (0.95f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x1, y - 1.75f, x2 - x1, 3.5f), 1.75f);
        for (float x : { x1, x2 })
        {
            const auto cap = juce::Rectangle<float> (6.0f, 15.0f).withCentre ({ x, y });
            g.setColour (colour); g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (currentPalette.grid.withAlpha (0.9f)); g.fillRoundedRectangle (cap.reduced (2.0f, 4.0f), 1.0f);   // a grip
        }
        paintLaneLabel (g, lane);
    }
    g.setColour (currentPalette.gridLines.withAlpha (0.07f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        if (f < host.nyquistFrequency()) g.drawVerticalLine (juce::roundToInt (xForFrequency (f)), plot.getY() + 29.0f, plot.getBottom());
    for (int row = 1; row <= 3; ++row)
        g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) row / 4.0f), plot.getX(), plot.getRight());
    g.setFont (juce::FontOptions (10.0f)); g.setColour (currentPalette.faintText);
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        if (f < host.nyquistFrequency() * 0.8f)
            g.drawText (formatHz (f), juce::Rectangle<float> (xForFrequency (f) - 35, plot.getBottom() + laneHeight() + 12.0f, 70, 16), juce::Justification::centred);
    g.drawText ("20 Hz", juce::Rectangle<float> (plot.getX(), plot.getBottom() + laneHeight() + 12.0f, 65, 16), juce::Justification::centredLeft);
    g.drawText (formatHz (host.nyquistFrequency()), juce::Rectangle<float> (plot.getRight() - 90, plot.getBottom() + laneHeight() + 12.0f, 90, 16), juce::Justification::centredRight);
}
void CrossoverDisplay::paint (juce::Graphics& g)
{
    const FramePacer::Timed timed (*pacer);
    const auto plot = plotBounds();
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    std::vector<float> key { (float) getWidth(), (float) getHeight(), scale, (float) host.getBandCount(), (float) activeSlope,
                             host.nyquistFrequency(), (float) host.getSampleRateForDisplay(), (float) themeGeneration };
    key.insert (key.end(), cuts.begin(), cuts.end());
    for (int j = 0; j + 1 < host.getBandCount(); ++j) key.push_back ((float) host.getCrossoverSlope (j));
    for (const auto& r : sweeps) { key.push_back (r.first); key.push_back (r.second); }
    for (int b = 0; b < host.getBandCount(); ++b) key.push_back (host.isBandHeard (b) ? 1.0f : 0.0f);
    if (key != staticKey || ! staticLayer.isValid())
    {
        staticKey = key;
        ++staticRenders;
        staticLayer = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                                   juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
        juce::Graphics layer (staticLayer);
        layer.addTransform (juce::AffineTransform::scale (scale));
        paintStaticLayer (layer);
    }
    drawPixelExact (g, staticLayer, scale);
    if (spectrumVisible)
    {
        // While a band is muted or another soloed, each band's stretch of the spectrum is drawn in its own colour when
        // it is heard and in grey when it is not; otherwise the spectrum is drawn as one.
        const int count = host.getBandCount();
        bool anySilent = false;
        for (int b = 0; b < count; ++b) anySilent = anySilent || ! host.isBandHeard (b);
        const auto drawSpectrum = [&] (juce::Colour colour, float strength)
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.19f * strength), 0.0f, plot.getY() + 26.0f,
                                                     colour.withAlpha (0.024f * strength), 0.0f, plot.getBottom(), false));
            g.fillPath (spectrumFill);
            g.setColour (colour.withAlpha (juce::jmin (1.0f, 0.72f * strength)));
            g.strokePath (spectrumLine, juce::PathStrokeType (1.3f));
        };
        // Ghost movement (0.19.0): the band an LFO moves lights up the spectrum where it is right now, the rest dimmed.
        std::vector<int> movingBands;
        if (ghostMovement)
            for (int j = 0; j < (int) cuts.size(); ++j)
                if (const int b = lfoBandOf (j); b >= 0 && std::find (movingBands.begin(), movingBands.end(), b) == movingBands.end()) movingBands.push_back (b);
        // A band lit in its colour fades out as its slopes do (0.20.0, Thomas: "visually be sloped"), where it is as
        // heard (with Ghost movement) or as set.
        const auto litCuts = ghostMovement ? host.heardCrossovers() : cuts;
        const auto drawLit = [&] (int b)
        {
            const auto colour = bandColour (b).brighter (0.2f);
            g.setGradientFill (litGradient (b, litCuts, colour, 0.3f));
            g.fillPath (spectrumFill);
            g.setGradientFill (litGradient (b, litCuts, colour, 1.0f));
            g.strokePath (spectrumLine, juce::PathStrokeType (1.3f));
        };
        if (anySilent)
        {
            drawSpectrum (currentPalette.faintText, 0.45f);   // MUTE / SOLO: the heard bands lit, the rest grey
            for (int b = 0; b < count; ++b) if (host.isBandHeard (b)) drawLit (b);
        }
        else if (! movingBands.empty())
        {
            drawSpectrum (currentPalette.faintText, 0.5f);    // the rest unlit, as with SOLO
            for (int b : movingBands) drawLit (b);
        }
        else
            drawSpectrum (currentPalette.spectrum, 1.0f);
        if (anySilent || ! movingBands.empty())
        {
            // As the plain spectrum, fainter towards the bottom.
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, plot.getY() + 26.0f,
                                                     juce::Colours::black.withAlpha (0.3f), 0.0f, plot.getBottom(), false));
            g.fillPath (spectrumFill);
        }
    }
    if (ghostMovement) paintGhost (g, host.heardCrossovers());
    // The range end under the mouse, or being dragged: larger and lit; while dragged, where it is.
    if (const auto end = laneDrag.divider >= 0 ? laneDrag : laneHover; end.divider >= 0)
    {
        const auto at = laneEndPosition (end);
        if (at.x > 0.0f)
        {
            const bool fromBelow = host.edgeLfo (end.divider, 1) >= 0 || host.edgeLfo (end.divider, 2) >= 0 || host.edgeLfo (end.divider, 3) >= 0;
            const auto colour = bandColour (fromBelow ? end.divider : end.divider + 1);
            const auto cap = juce::Rectangle<float> (9.0f, 19.0f).withCentre (at);
            g.setColour (colour.withAlpha (0.35f)); g.fillRoundedRectangle (cap.expanded (3.0f), 6.0f);
            g.setColour (currentPalette.text); g.fillRoundedRectangle (cap, 4.0f);
            g.setColour (colour); g.drawRoundedRectangle (cap.reduced (0.75f), 4.0f, 1.5f);
            if (laneDrag.divider >= 0 && laneDrag.divider < (int) sweeps.size())
            {
                const auto range = sweeps[(size_t) laneDrag.divider];
                const auto text = formatHz (laneDrag.high ? range.second : range.first);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
                const auto tag = juce::Rectangle<float> (w, 20.0f).withCentre ({ juce::jlimit (plot.getX() + w * 0.5f, plot.getRight() - w * 0.5f, at.x), plot.getBottom() - 16.0f });
                g.setColour (currentPalette.pill.withAlpha (0.96f)); g.fillRoundedRectangle (tag, 4.0f);
                g.setColour (colour); g.drawRoundedRectangle (tag.reduced (0.5f), 4.0f, 1.0f);
                g.setColour (currentPalette.text); g.drawText (text, tag, juce::Justification::centred);
            }
        }
    }
    const auto pills = pillRects();
    for (size_t i = 0; i < cuts.size() && i < pills.size(); ++i)
    {
        g.setColour (currentPalette.pill.withAlpha (0.94f)); g.fillRoundedRectangle (pills[i], 4.0f);
        g.setColour (activeHandle == (int) i ? bandColour ((int) i) : currentPalette.text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (formatHz (cuts[i]), pills[i], juce::Justification::centred);
    }
    paintSlopeReadouts (g);
    paintMeters (g);
}
std::vector<juce::Rectangle<float>> CrossoverDisplay::pillRects() const
{
    const auto plot = plotBounds();
    std::vector<juce::Rectangle<float>> rects;
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const auto x = juce::jlimit (plot.getX(), juce::jmax (plot.getX(), plot.getRight() - pillWidth), xForFrequency (cuts[i]) - pillWidth * 0.5f);
        rects.push_back ({ x, pillTop ((int) i), pillWidth, 24.0f });
    }
    return rects;
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
    g.setColour (highlighted ? colour : currentPalette.softText);
    // The dashed line stops at every frequency pill and slope readout it would run through (its own and
    // those of nearby dividers), so their text stays clean.
    {
        std::vector<juce::Range<float>> gaps;
        const float x = (float) getX() + centre;
        auto covers = [&] (juce::Rectangle<float> r)
        {
            if (x >= r.getX() - 1.0f && x <= r.getRight() + 1.0f) gaps.push_back ({ r.getY() - 3.0f - (float) getY(), r.getBottom() + 3.0f - (float) getY() });
        };
        for (const auto& r : owner.pillRects()) covers (r);
        for (int i = 0; i < (int) owner.cuts.size(); ++i) if (i != index) covers (owner.slopeReadoutBounds (i));
        std::sort (gaps.begin(), gaps.end(), [] (auto a, auto b) { return a.getStart() < b.getStart(); });
        float from = 0.0f;
        const float to = bottom - 7.0f;
        for (const auto& gap : gaps)
        {
            if (gap.getStart() > from) g.drawDashedLine (juce::Line<float> (centre, from, centre, juce::jmin (to, gap.getStart())), dash, 2, highlighted ? 2.0f : 1.5f);
            from = juce::jmax (from, gap.getEnd());
            if (from >= to) break;
        }
        if (from < to) g.drawDashedLine (juce::Line<float> (centre, from, centre, to), dash, 2, highlighted ? 2.0f : 1.5f);
    }
    g.setColour (currentPalette.grid); g.fillEllipse (centre - 8, bottom - 8, 16, 16);
    g.setColour (highlighted ? colour : currentPalette.text);
    g.fillEllipse (centre - 5.5f, bottom - 5.5f, 11, 11);

    if (hasKeyboardFocus (false)) g.drawEllipse (centre - 9, bottom - 9, 18, 18, 1.0f);
    // The slope dot: a ring, up for soft, down for hard.
    const float slope = owner.slopeY (owner.host.getCrossoverSlope (index)) - (float) getY();
    const bool slopeActive = owner.activeSlope == index || draggingSlope;
    g.setColour (currentPalette.grid); g.fillEllipse (centre - 8, slope - 8, 16, 16);
    g.setColour (slopeActive || highlighted ? colour : currentPalette.text);
    g.drawEllipse (centre - 5.0f, slope - 5.0f, 10.0f, 10.0f, 2.2f);
    if (slopeActive) g.fillEllipse (centre - 2.5f, slope - 2.5f, 5.0f, 5.0f);
}
bool CrossoverDisplay::Handle::isOverSlopeDot (juce::Point<float> local) const
{
    const float slope = owner.slopeY (owner.host.getCrossoverSlope (index)) - (float) getY();
    return local.getDistanceFrom ({ (float) getWidth() * 0.5f, slope }) < 10.0f;
}
void CrossoverDisplay::Handle::mouseMove (const juce::MouseEvent& event)
{
    setMouseCursor (isOverSlopeDot (event.position) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
}
bool CrossoverDisplay::Handle::hitTest (int x, int y)
{
    if (! getLocalBounds().contains (x, y)) return false;
    // The ends of the ranges on the lane are the panel's (0.21.0: dragged to change where an LFO takes a divider).
    if (owner.laneEndAt (getPosition().toFloat() + juce::Point<float> ((float) x, (float) y)).divider >= 0) return false;
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
    if (isOverSlopeDot (event.position))
    {
        draggingSlope = true;
        slopeGrabY = event.getEventRelativeTo (&owner).position.y;
        slopeGrabOrder = owner.host.getCrossoverSlope (index);
        owner.beginSlopeGesture (index);
        return;
    }
    const auto current = owner.host.effectiveCrossovers();
    if (index >= (int) current.size()) return;
    grabOffset = owner.xForFrequency (current[(size_t) index]) - event.getEventRelativeTo (&owner).position.x;
    owner.beginCrossoverGesture (index);
}
void CrossoverDisplay::Handle::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingSlope)
    {
        // Every slopeDragStep pixels of mouse travel is one slope step (6 dB/oct); down is harder. (The
        // dot's own track is shorter, so 6 to 96 dB/oct stays easy to set precisely.)
        const float moved = event.getEventRelativeTo (&owner).position.y - slopeGrabY;
        owner.dragSlopeTo (slopeGrabOrder + juce::roundToInt (moved / slopeDragStep));
        return;
    }
    if (owner.activeHandle == index) owner.dragCrossoverTo (event.getEventRelativeTo (&owner).position.x + grabOffset);
}
void CrossoverDisplay::Handle::mouseUp (const juce::MouseEvent&)
{
    if (draggingSlope) { draggingSlope = false; owner.endSlopeGesture(); return; }
    owner.endCrossoverGesture();
}
void CrossoverDisplay::Handle::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown()) return;
    if (isOverSlopeDot (event.position)) owner.setSlope (index, triad::CrossoverFilter::defaultOrder);   // back to 24 dB/oct
    else owner.setCrossoverValue (index, triad::defaultCrossover (index, owner.host.getBandCount()));
}
bool CrossoverDisplay::Handle::keyPressed (const juce::KeyPress& key)
{
    // Up / down: softer / harder slope.
    if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        owner.setSlope (index, owner.host.getCrossoverSlope (index) + (key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1));
        return true;
    }
    const bool left = key.getKeyCode() == juce::KeyPress::leftKey;
    if (! left && key.getKeyCode() != juce::KeyPress::rightKey) return false;
    const auto current = owner.host.effectiveCrossovers();
    if (index >= (int) current.size()) return false;
    const float step = (key.getModifiers().isShiftDown() ? 1.0f / 120.0f : 1.0f / 12.0f) * (left ? -1.0f : 1.0f);
    owner.setCrossoverValue (index, current[(size_t) index] * std::pow (2.0f, step));
    return true;
}
