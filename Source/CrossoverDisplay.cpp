#include "CrossoverDisplay.h"

namespace
{
const std::array<juce::Colour, 3> bandColours { juce::Colour (0xff9bd85c), juce::Colour (0xff67c6b8), juce::Colour (0xffa39ce8) };
juce::String frequencyText (float f)
{
    return (f >= 1000.0f ? juce::String (f / 1000.0f, 3) : juce::String (f, 2))
        .trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + (f >= 1000.0f ? " kHz" : " Hz");
}
}
CrossoverDisplay::CrossoverDisplay (TriadProcessor& p) : processor (p),
    parameters { p.parameters.getParameter ("lowCut"), p.parameters.getParameter ("highCut") }
{
    setComponentID ("frequencyDisplay");
    setTitle ("Three-band crossover display");
    for (int i = 0; i < 2; ++i)
    {
        handles[(size_t) i] = std::make_unique<Handle> (*this, i);
        addAndMakeVisible (*handles[(size_t) i]);
    }
}
CrossoverDisplay::~CrossoverDisplay() { endCrossoverGesture(); }
juce::Rectangle<float> CrossoverDisplay::plotBounds() const
{
    return { 20.0f, 36.0f, juce::jmax (1.0f, (float) getWidth() - 40.0f), juce::jmax (1.0f, (float) getHeight() - 68.0f) };
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
    const auto cuts = processor.effectiveCutoffs();
    const auto ceiling = juce::jmin (20000.0f, processor.nyquistFrequency() * 0.9f);
    return index == 0 ? juce::jlimit (20.0f, cuts.second / 1.01f, f)
                      : juce::jlimit (juce::jmin (ceiling, juce::jmax (30.0f, cuts.first * 1.01f)), ceiling, f);
}
void CrossoverDisplay::beginCrossoverGesture (int index)
{
    if (index < 0 || index > 1) return;
    endCrossoverGesture();
    activeHandle = index;
    parameters[(size_t) index]->beginChangeGesture();
    repaint();
}
void CrossoverDisplay::dragCrossoverTo (float x)
{
    if (activeHandle < 0) return;
    auto* parameter = parameters[(size_t) activeHandle];
    const auto frequency = constrainValue (activeHandle, frequencyForX (x));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (frequency));
    refresh();
}
void CrossoverDisplay::endCrossoverGesture()
{
    if (activeHandle < 0) return;
    const auto ended = activeHandle;
    activeHandle = -1;
    parameters[(size_t) ended]->endChangeGesture();
    repaint();
}
void CrossoverDisplay::setCrossoverValue (int index, float frequency)
{
    if (index < 0 || index > 1) return;
    beginCrossoverGesture (index);
    auto* parameter = parameters[(size_t) index];
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (constrainValue (index, frequency)));
    refresh();
    endCrossoverGesture();
}
void CrossoverDisplay::refresh() { resized(); repaint(); }
void CrossoverDisplay::resized()
{
    const auto cuts = processor.effectiveCutoffs();
    const auto plot = plotBounds();
    for (int i = 0; i < 2; ++i)
    {
        const auto f = i == 0 ? cuts.first : cuts.second;
        handles[(size_t) i]->setBounds (juce::roundToInt (xForFrequency (f)) - 12,
                                       juce::roundToInt (plot.getY()), 24, juce::roundToInt (plot.getHeight()) + 10);
        handles[(size_t) i]->setDescription (frequencyText (f) + ". Drag left or right; arrow keys adjust; Shift gives finer adjustment.");
    }
    // The selected handle stays reachable even when the two boundaries are close.
    if (activeHandle >= 0) handles[(size_t) activeHandle]->toFront (false);
}
void CrossoverDisplay::paint (juce::Graphics& g)
{
    const auto plot = plotBounds();
    const auto cuts = processor.effectiveCutoffs();
    const std::array<float, 4> xs { plot.getX(), xForFrequency (cuts.first), xForFrequency (cuts.second), plot.getRight() };
    g.setColour (juce::Colour (0xff111213)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    g.setColour (juce::Colour (0xffb8bcba)); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("BAND CROSSOVERS", 18, 7, 175, 23, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.0f)); g.setColour (juce::Colour (0xff898f8b));
    g.drawText ("Drag a divider  /  Double-click to reset", getWidth() - 300, 7, 280, 23, juce::Justification::centredRight);
    for (size_t i = 0; i < 3; ++i)
    {
        g.setColour (bandColours[i].withAlpha (0.08f));
        g.fillRect (xs[i], plot.getY(), xs[i + 1] - xs[i], plot.getHeight());
        g.setColour (bandColours[i].withAlpha (0.78f));
        g.fillRect (xs[i], plot.getBottom() - 2.0f, xs[i + 1] - xs[i], 3.0f);
        if (xs[i + 1] - xs[i] > 64.0f)
        {
            g.setColour (bandColours[i]); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (juce::StringArray { "LOW", "MID", "HIGH" }[(int) i],
                        juce::Rectangle<float> (xs[i] + 10.0f, plot.getY() + 7.0f, xs[i + 1] - xs[i] - 20.0f, 20.0f), juce::Justification::centredLeft);
        }
    }
    // Frequency grid only: no simulated spectrum is presented as live audio.
    g.setColour (juce::Colour (0xff929991).withAlpha (0.12f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        if (f < processor.nyquistFrequency()) g.drawVerticalLine (juce::roundToInt (xForFrequency (f)), plot.getY() + 29.0f, plot.getBottom());
    for (int row = 1; row <= 3; ++row)
        g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) row / 4.0f), plot.getX(), plot.getRight());
    g.setFont (juce::FontOptions (10.0f)); g.setColour (juce::Colour (0xff969e98));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        if (f < processor.nyquistFrequency() * 0.8f)
            g.drawText (frequencyText (f), juce::Rectangle<float> (xForFrequency (f) - 35, plot.getBottom() + 13, 70, 16), juce::Justification::centred);
    g.drawText ("20 Hz", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 13, 65, 16), juce::Justification::centredLeft);
    g.drawText (frequencyText (processor.nyquistFrequency()), juce::Rectangle<float> (plot.getRight() - 90, plot.getBottom() + 13, 90, 16), juce::Justification::centredRight);
    for (int i = 0; i < 2; ++i)
    {
        const float pillWidth = 92.0f;
        const bool close = xs[2] - xs[1] < pillWidth + 8.0f;
        const auto x = juce::jlimit (plot.getX(), plot.getRight() - pillWidth, xs[(size_t) i + 1] - pillWidth * 0.5f);
        const auto y = plot.getBottom() - 37.0f - (close && i == 1 ? 29.0f : 0.0f);
        g.setColour (juce::Colour (0xff090b09)); g.fillRoundedRectangle (x, y, pillWidth, 24.0f, 4.0f);
        g.setColour (activeHandle == i ? bandColours[(size_t) i] : juce::Colour (0xffe1e6e0));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (frequencyText (i == 0 ? cuts.first : cuts.second), juce::Rectangle<float> (x, y, pillWidth, 24.0f), juce::Justification::centred);
    }
}
CrossoverDisplay::Handle::Handle (CrossoverDisplay& display, int which) : owner (display), index (which)
{
    setTitle (which == 0 ? "Low / Mid crossover" : "Mid / High crossover");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}
void CrossoverDisplay::Handle::paint (juce::Graphics& g)
{
    const bool highlighted = isMouseOverOrDragging() || hasKeyboardFocus (false) || owner.activeHandle == index;
    const float centre = (float) getWidth() * 0.5f, bottom = (float) getHeight() - 10.0f;
    const float dash[] { 4.0f, 5.0f };
    g.setColour (highlighted ? bandColours[(size_t) index] : juce::Colour (0xffb6bcb5));
    const auto cuts = owner.processor.effectiveCutoffs();
    const bool close = owner.xForFrequency (cuts.second) - owner.xForFrequency (cuts.first) < 100.0f;
    const float firstPillTop = bottom - 37.0f - (close ? 29.0f : 0.0f);
    const float lastPillBottom = bottom - 13.0f;
    g.drawDashedLine (juce::Line<float> (centre, 0.0f, centre, firstPillTop - 3.0f), dash, 2, highlighted ? 2.0f : 1.5f);
    g.drawDashedLine (juce::Line<float> (centre, lastPillBottom + 3.0f, centre, bottom - 7.0f), dash, 2, highlighted ? 2.0f : 1.5f);
    g.setColour (juce::Colour (0xff111413)); g.fillEllipse (centre - 8, bottom - 8, 16, 16);
    g.setColour (highlighted ? bandColours[(size_t) index] : juce::Colour (0xffe3e8df));
    g.fillEllipse (centre - 5.5f, bottom - 5.5f, 11, 11);
    if (hasKeyboardFocus (false)) g.drawEllipse (centre - 9, bottom - 9, 18, 18, 1.0f);
}
bool CrossoverDisplay::Handle::hitTest (int x, int y)
{
    if (! getLocalBounds().contains (x, y)) return false;
    const auto cuts = owner.processor.effectiveCutoffs();
    const float position = (float) (getX() + x);
    const float lowDistance = std::abs (position - owner.xForFrequency (cuts.first));
    const float highDistance = std::abs (position - owner.xForFrequency (cuts.second));
    // Overlapping hit areas are divided at the midpoint, so a narrow band does
    // not leave one handle completely covered by the other.
    return index == 0 ? lowDistance <= highDistance : highDistance < lowDistance;
}
void CrossoverDisplay::Handle::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown()) return;
    const auto cuts = owner.processor.effectiveCutoffs();
    grabOffset = owner.xForFrequency (index == 0 ? cuts.first : cuts.second) - event.getEventRelativeTo (&owner).position.x;
    owner.beginCrossoverGesture (index);
}
void CrossoverDisplay::Handle::mouseDrag (const juce::MouseEvent& event)
{
    if (owner.activeHandle == index) owner.dragCrossoverTo (event.getEventRelativeTo (&owner).position.x + grabOffset);
}
void CrossoverDisplay::Handle::mouseUp (const juce::MouseEvent&) { owner.endCrossoverGesture(); }
void CrossoverDisplay::Handle::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown()) owner.setCrossoverValue (index, index == 0 ? 250.0f : 2500.0f);
}
bool CrossoverDisplay::Handle::keyPressed (const juce::KeyPress& key)
{
    const bool left = key.getKeyCode() == juce::KeyPress::leftKey;
    if (! left && key.getKeyCode() != juce::KeyPress::rightKey) return false;
    const auto cuts = owner.processor.effectiveCutoffs();
    const float step = (key.getModifiers().isShiftDown() ? 1.0f / 120.0f : 1.0f / 12.0f) * (left ? -1.0f : 1.0f);
    owner.setCrossoverValue (index, (index == 0 ? cuts.first : cuts.second) * std::pow (2.0f, step));
    return true;
}
