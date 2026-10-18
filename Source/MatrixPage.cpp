#include "MatrixPage.h"

using namespace triad::ui;

namespace
{
constexpr int panelWidth = 468, panelGap = 10, rowHeight = 34;
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
}
void BandMatrix::resized()
{
    view.setBounds (getLocalBounds().withTrimmedTop (68).reduced (8, 0).withTrimmedBottom (8));
    const int width = view.getWidth() - 8;
    int y = 0;
    for (auto& row : rows) { row->setBounds (0, y, width, rowHeight); y += rowHeight + 3; }
    addButton.setBounds (0, y + 4, 120, 26);
    holder.setSize (width, y + 36);
}
void BandMatrix::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    g.setColour (colour.withAlpha (0.75f));
    g.fillRoundedRectangle (12.0f, 11.0f, 4.0f, 17.0f, 2.0f);
    g.setColour (colour);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const bool extra = TriadProcessor::isExtraChain (band);
    g.drawText ((extra ? TriadProcessor::chainName (band) : "BAND " + juce::String (band + 1)) + "  MATRIX", 23, 8, 200, 24, juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (11.5f));
    if (extra)
        g.drawText (band == TriadProcessor::preBand ? "full range, before the bands" : "full range, after the bands", getWidth() - 220, 8, 206, 24,
                    juce::Justification::centredRight);
    else
    {
        const auto cuts = processor.effectiveCrossovers();
        const float lo = band == 0 ? 0.0f : cuts[(size_t) band - 1];
        const float hi = band < (int) cuts.size() ? cuts[(size_t) band] : processor.nyquistFrequency();
        g.drawText (formatHz (lo) + u8 ("  \xe2\x86\x92  ") + formatHz (hi), getWidth() - 190, 8, 176, 24, juce::Justification::centredRight);
    }
    // Column titles, as in Serum's and Omnisphere's matrix.
    g.setColour (juce::Colour (0xff2b2f2d));
    g.fillRect (8, 40, getWidth() - 16, 22);
    g.setColour (juce::Colour (0xffb8bfb5));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    const int x0 = 8 + 4 + 22;
    const int usable = getWidth() - 16 - 8 - 22 - 26 - 8;
    const int sourceWidth = juce::jmin (90, usable / 4);
    const int destinationWidth = juce::jmin (150, (usable - sourceWidth - 6) / 3);
    g.drawText ("SOURCE", x0, 40, sourceWidth, 22, juce::Justification::centred);
    g.drawText ("AMOUNT", x0 + sourceWidth + 6, 40, usable - sourceWidth - destinationWidth - 90, 22, juce::Justification::centred);
    g.drawText ("INV", getWidth() - 8 - 4 - 26 - destinationWidth - 4 - 36, 40, 36, 22, juce::Justification::centred);
    g.drawText ("DESTINATION", getWidth() - 8 - 4 - 26 - destinationWidth, 40, destinationWidth, 22, juce::Justification::centred);
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
    for (auto& p : panels) { p->refresh(); p->repaint(); }
}
void MatrixPage::resized()
{
    view.setBounds (getLocalBounds());
    const int height = getHeight() - 10;
    for (int i = 0; i < (int) panels.size(); ++i) panels[(size_t) i]->setBounds (i * (panelWidth + panelGap), 0, panelWidth, height);
    holder.setSize (juce::jmax (getWidth(), (int) panels.size() * (panelWidth + panelGap) - panelGap), height);
}
