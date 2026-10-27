#include "MatrixPage.h"

using namespace triad::ui;

namespace
{
// Compact (0.16.0): three panels side by side in the window, so there is less to scroll sideways.
constexpr int panelWidth = 348, panelGap = 12, rowHeight = 40;
}

BandMatrix::BandMatrix (TriadProcessor& p, int b) : processor (p), band (b), colour (bandColour (b))
{
    setTitle ((TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "Band " + juce::String (band + 1)) + " matrix");
    view.setViewedComponent (&holder, false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (6);
    addAndMakeVisible (view);
    addButton.setTooltip ("Route an LFO of this band to a knob of this band");
    addButton.onClick = [this]
    {
        const int lfo = juce::jmax (0, processor.getBandLfo (band));
        if (TriadProcessor::isExtraChain (band))
        {
            // PRE and POST have no band volume: the new route starts on the first slot's mix.
            const auto destinations = processor.getDestinations (band);
            if (! destinations.empty()) processor.addRoute (band, lfo, destinations.front().first);
        }
        else processor.addRoute (band, lfo, TriadProcessor::Destination { TriadProcessor::Destination::bandVolume, 0 });
        refresh();
        view.setViewPosition (0, juce::jmax (0, holder.getHeight() - view.getHeight()));
    };
    holder.addAndMakeVisible (addButton);
    refresh();
}
void BandMatrix::rebuild()
{
    rows.clear();
    for (int r = 0; r < processor.getRouteCount (band); ++r)
    {
        auto row = std::make_unique<RouteRow> (processor, band, r, true, colour);
        row->onEditLfo = [this] (int b, int l) { if (onEditLfo) onEditLfo (b, l); };
        holder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    resized();
}
void BandMatrix::refresh()
{
    juce::String signature;
    signature << processor.getStructureVersion() << ":" << processor.getRouteCount (band) << ":" << processor.getLfoCount (band);
    if (signature != built) { built = signature; rebuild(); }
    for (auto& row : rows) row->update();
    // The band's range in the title follows the dividers.
    juce::String range;
    if (! TriadProcessor::isExtraChain (band))
    {
        const auto cuts = processor.effectiveCrossovers();
        range << (band == 0 ? 0.0f : cuts[(size_t) juce::jmin (band, (int) cuts.size()) - 1]) << "/"
              << (band < (int) cuts.size() ? cuts[(size_t) band] : processor.nyquistFrequency());
    }
    if (range != shownRange) { shownRange = range; repaint (0, 0, getWidth(), 36); }
}
void BandMatrix::resized()
{
    view.setBounds (getLocalBounds().withTrimmedTop (76).reduced (8, 0).withTrimmedBottom (8));
    const int width = view.getWidth() - 8;
    int y = 0;
    for (auto& row : rows) { row->setBounds (0, y, width, rowHeight); y += rowHeight + 6; }
    addButton.setBounds (0, y + 4, 132, 30);
    holder.setSize (width, y + 40);
}
void BandMatrix::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (14.0f, 14.0f, 4.0f, 20.0f, 2.0f);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    const bool extra = TriadProcessor::isExtraChain (band);
    g.drawText ((extra ? TriadProcessor::chainName (band) : "BAND " + juce::String (band + 1)) + "  MATRIX", 26, 12, 170, 24, juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    if (extra)
        g.drawText (band == TriadProcessor::preBand ? "before the bands" : "after the bands", getWidth() - 160, 12, 146, 24,
                    juce::Justification::centredRight);
    else
    {
        const auto cuts = processor.effectiveCrossovers();
        const float lo = band == 0 ? 0.0f : cuts[(size_t) band - 1];
        const float hi = band < (int) cuts.size() ? cuts[(size_t) band] : processor.nyquistFrequency();
        g.drawText (formatHz (lo) + u8 ("  \xe2\x86\x92  ") + formatHz (hi), getWidth() - 160, 12, 146, 24, juce::Justification::centredRight);
    }
    // Column titles over the rows' columns, as in Serum's and Omnisphere's matrix.
    const auto strip = juce::Rectangle<float> (8.0f, 46.0f, (float) getWidth() - 16.0f, 24.0f);
    g.setColour (currentPalette.grid.interpolatedWith (currentPalette.panel, 0.4f));
    g.fillRoundedRectangle (strip, 6.0f);
    g.setColour (currentPalette.softText);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.06f));
    const auto c = triad::ui::matrixColumns (view.getWidth() - 8);
    const int x0 = view.getX();
    const auto title = [&] (juce::Range<int> x, const char* text)
    {
        g.drawText (text, x0 + x.getStart() - 4, 46, x.getLength() + 8, 24, juce::Justification::centred);
    };
    title (c.source, "SOURCE");
    title (c.amount, "AMOUNT");
    title (c.invert, "INV");
    title (c.destination, "DESTINATION");
}
MatrixPage::MatrixPage (TriadProcessor& p) : processor (p)
{
    setTitle ("Matrix");
    view.setViewedComponent (&holder, false);
    view.setScrollBarsShown (false, true);
    view.setScrollBarThickness (8);
    addAndMakeVisible (view);
    refresh();
}
void MatrixPage::refresh()
{
    if (processor.getBandCount() != builtCount)
    {
        builtCount = processor.getBandCount();
        panels.clear();
        // PRE first, then the bands, then POST.
        for (int b = -1; b <= builtCount; ++b)
        {
            panels.push_back (std::make_unique<BandMatrix> (processor, b < 0 ? TriadProcessor::preBand : b == builtCount ? TriadProcessor::postBand : b));
            panels.back()->onEditLfo = [this] (int band, int lfo) { if (onEditLfo) onEditLfo (band, lfo); };
            holder.addAndMakeVisible (*panels.back());
        }
        resized();
    }
    for (auto& p : panels) p->refresh();
}
void MatrixPage::resized()
{
    view.setBounds (getLocalBounds());
    const int height = getHeight() - 10;
    for (int i = 0; i < (int) panels.size(); ++i) panels[(size_t) i]->setBounds (i * (panelWidth + panelGap), 0, panelWidth, height);
    holder.setSize (juce::jmax (getWidth(), (int) panels.size() * (panelWidth + panelGap) - panelGap), height);
}
