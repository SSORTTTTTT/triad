#pragma once
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "TriggerTests.h"
#include <cmath>
#include <iostream>

// 0.18.0 (Thomas): SOLO on each band, and LFOs on the band edges ("~ Link to LFO": move the band, open / close it, its
// low or high edge), so a band sweeps like a filter.
namespace edgeTests
{
namespace mod = triad::mod;
using D = TriadProcessor::Destination;
using triggerTests::addTone;
using triggerTests::rms;

inline std::vector<float> run (TriadProcessor& p, const std::vector<float>& signal, int block = 512)
{
    return triggerTests::play (p, signal, block, [] (size_t) {});
}
inline float db (float gain) { return juce::Decibels::gainToDecibels (gain, -150.0f); }
inline float uFor (float hz) { return std::log (hz / 20.0f) / std::log (1000.0f); }   // as the edge LFOs' grid

inline void splitterTests()
{
    // The splitter with the dividers moved fast and far (every 32 samples, by up to 4 octaves), at every slope and
    // sample rate: no NaN or blow-up, the dividers keep their order, and the bands still add up to the sound.
    juce::Random r (17);
    int checks = 0;
    float worstSweep = 0.0f, worstJump = 0.0f;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int order : { 1, 2, 4, 8, 12, 16 })
        {
            triad::Splitter split;
            split.prepare (rate, 512);
            const std::array<float, triad::maxCrossovers> cuts { 120.0f, 900.0f, 4000.0f, 9000.0f };
            std::array<int, triad::maxCrossovers> orders {};
            orders.fill (order);
            split.setBandCount (5, cuts.data(), orders.data());
            std::array<juce::AudioBuffer<float>, triad::maxBands> bands;
            for (auto& b : bands) b.setSize (2, 512);
            juce::AudioBuffer<float> input (2, 512);
            std::vector<float> moves ((512 / 32 + 2) * triad::maxCrossovers, 0.0f);
            double inEnergy[2] {}, outEnergy[2] {};   // [0] smooth sweeps, [1] jumps
            float peak = 0.0f;
            std::array<float, 4> jump {};
            for (int block = 0; block < 120; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < 512; ++s) input.setSample (ch, s, r.nextFloat() - 0.5f);
                // First a sweep (3 octaves each way, 4 times a second: a fast LFO), then jumps of up to 8 octaves every
                // 2.7 ms (a 1/128 square LFO across everything: far beyond what music asks).
                const bool jumping = block >= 60;
                for (int k = 0; k < 17; ++k)
                    for (int j = 0; j < 4; ++j)
                    {
                        const double t = (block * 512 + k * 32) / rate;
                        if (jumping && k % 4 == 0) jump[(size_t) j] = (r.nextFloat() - 0.5f) * 8.0f;
                        moves[(size_t) k * triad::maxCrossovers + (size_t) j] = jumping ? jump[(size_t) j]
                                                                                        : 3.0f * (float) std::sin (juce::MathConstants<double>::twoPi * 4.0 * t + j);
                    }
                split.process (input, 0, 512, bands, block < 110 ? moves.data() : nullptr, 32);
                std::vector<float> heard;
                for (int j = 0; j < 4; ++j) heard.push_back (split.getHeard (j));
                for (int j = 1; j < 4; ++j) require (heard[(size_t) j] >= heard[(size_t) j - 1] * 1.009f, "Moved dividers never cross");
                for (int j = 0; j < 4; ++j) require (heard[(size_t) j] >= triad::lowestCrossover * 0.999f && heard[(size_t) j] <= triad::crossoverCeiling (rate) * 1.001f, "Moved dividers stay in range");
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < 512; ++s)
                    {
                        float sum = 0.0f;
                        for (int b = 0; b < 5; ++b)
                        {
                            const float v = bands[(size_t) b].getSample (ch, s);
                            require (std::isfinite (v), "Moving dividers: no NaN / Inf");
                            peak = juce::jmax (peak, std::abs (v));
                            sum += v;
                        }
                        if ((block >= 4 && block < 60) || (block >= 64 && block < 110))
                        {
                            inEnergy[jumping ? 1 : 0] += (double) input.getSample (ch, s) * input.getSample (ch, s);
                            outEnergy[jumping ? 1 : 0] += (double) sum * sum;
                        }
                    }
            }
            require (peak < 4.0f, "Moving dividers: no band rings up (noise in at 0.5 peak)");
            const float sweepDiff = std::abs (db ((float) std::sqrt (outEnergy[0] / inEnergy[0]))), jumpDiff = std::abs (db ((float) std::sqrt (outEnergy[1] / inEnergy[1])));
            worstSweep = juce::jmax (worstSweep, sweepDiff);
            worstJump = juce::jmax (worstJump, jumpDiff);
            require (sweepDiff < 0.25f, "Dividers swept by an LFO: the bands still add up to the sound (within 0.25 dB)");
            require (jumpDiff < 4.0f, "Dividers jumping by octaves every 2.7 ms: the bands add up to the sound within 4 dB");
            ++checks;
        }
    // Let go: the dividers glide back to the knobs.
    {
        triad::Splitter split;
        split.prepare (48000.0, 512);
        const std::array<float, triad::maxCrossovers> cuts { 250.0f, 2500.0f };
        std::array<int, triad::maxCrossovers> orders {};
        orders.fill (4);
        split.setBandCount (3, cuts.data(), orders.data());
        std::array<juce::AudioBuffer<float>, triad::maxBands> bands;
        for (auto& b : bands) b.setSize (2, 512);
        juce::AudioBuffer<float> input (2, 512);
        input.clear();
        std::vector<float> moves ((512 / 32 + 2) * triad::maxCrossovers, 1.0f);   // one octave up
        split.process (input, 0, 512, bands, moves.data(), 32);
        require (std::abs (split.getHeard (0) / 500.0f - 1.0f) < 0.02f && std::abs (split.getHeard (1) / 5000.0f - 1.0f) < 0.02f,
                 "A move of one octave doubles the dividers' frequencies");
        split.process (input, 0, 512, bands);
        split.process (input, 0, 512, bands);
        require (! split.isMoving() && std::abs (split.getHeard (0) / 250.0f - 1.0f) < 0.02f, "Without moves the dividers go back to their knobs");
    }
    std::cout << "Band edges, splitter: " << checks << " slope / rate cases moved fast without NaN or crossing; the bands' sum within "
              << worstSweep << " dB while swept, " << worstJump << " dB while jumping: passed" << std::endl;
}

inline void soloTests()
{
    constexpr double rate = 48000.0;
    std::vector<float> signal ((size_t) rate);
    addTone (signal, 0, signal.size(), 100.0, 0.3f);
    addTone (signal, 0, signal.size(), 800.0, 0.3f);
    addTone (signal, 0, signal.size(), 6000.0, 0.3f);
    const size_t from = signal.size() / 2, to = signal.size();
    // Band 2 soloed: what comes out is band 2 alone, exactly as with bands 1 and 3 muted.
    TriadProcessor a, b;
    a.prepareToPlay (rate, 512);
    b.prepareToPlay (rate, 512);
    require (! a.isBandSoloed (1) && ! a.isAnyBandSoloed(), "SOLO starts off");
    a.setBandSoloed (1, true);
    require (a.isBandSoloed (1) && a.isAnyBandSoloed(), "SOLO switches on (as a gesture)");
    b.setBandMuted (0, true);
    b.setBandMuted (2, true);
    const auto soloed = run (a, signal), muted = run (b, signal);
    float most = 0.0f;
    for (size_t i = from; i < to; ++i) most = juce::jmax (most, std::abs (soloed[i] - muted[i]));
    require (most < 1.0e-5f, "SOLO on band 2 sounds exactly as muting bands 1 and 3");
    require (rms (soloed, from, to) > 0.1f, "...and band 2 is heard");
    // Two bands soloed: both heard. A soloed band that is muted stays silent.
    a.setBandSoloed (2, true);
    const auto two = run (a, signal);
    require (rms (two, from, to) > rms (soloed, from, to) * 1.2f, "Two bands soloed: both are heard");
    a.setBandSoloed (2, false);
    a.setBandMuted (1, true);
    const auto none = run (a, signal);
    require (rms (none, from, to) < 1.0e-4f, "A muted band stays silent when soloed (and the others are silent through SOLO)");
    a.setBandMuted (1, false);
    // Saved with the project; moving, adding and removing bands carry it along.
    juce::MemoryBlock state;
    a.getStateInformation (state);
    TriadProcessor c;
    c.prepareToPlay (rate, 512);
    c.setStateInformation (state.getData(), (int) state.getSize());
    require (c.isBandSoloed (1) && ! c.isBandSoloed (0) && ! c.isBandSoloed (2), "SOLO is saved with the project");
    c.moveBand (1, 2);
    require (c.isBandSoloed (2) && ! c.isBandSoloed (1), "A moved band keeps its SOLO");
    c.removeBand (0);
    require (c.isBandSoloed (1), "Removing a band below moves SOLO down with its band");
    c.addBand (100.0f);
    require (c.isBandSoloed (2) || c.isBandSoloed (1), "Splitting bands keeps SOLO on the band's halves");
    std::cout << "SOLO: band 2 alone = bands 1 and 3 muted (difference " << most << "), saved, follows moved bands: passed" << std::endl;
}

inline void edgeTests (const juce::File& screenshots)
{
    constexpr double rate = 48000.0;
    // Linking: an LFO in the band, a route to its edges, flat where they are (nothing moves until it is drawn).
    TriadProcessor p;
    p.prepareToPlay (rate, 512);
    const int lfoCount = p.getLfoCount (1);
    const int lfo = p.linkEdgesToLfo (1, D::moveEdges);
    require (lfo == lfoCount && p.getLfoCount (1) == lfoCount + 1, "Link to LFO: a new LFO in the band");
    require (p.getLfoInfo (1, lfo).edge == D::moveEdges && p.getLfoInfo (1, lfo).active, "...routed to the band's edges, switched on");
    require (p.linkEdgesToLfo (1, D::moveEdges) == lfo && p.getLfoCount (1) == lfoCount + 1, "Linking again opens the same LFO");
    require (p.findEdgeLfo (1, D::moveEdges) == lfo && p.findEdgeLfo (1, D::openEdges) < 0, "The menu finds the linked LFO");
    require (p.linkEdgesToLfo (0, D::lowEdge) < 0 && p.linkEdgesToLfo (2, D::highEdge) < 0, "The lowest band has no low edge, the highest no high edge");
    require (p.linkEdgesToLfo (TriadProcessor::preBand, D::moveEdges) < 0, "PRE has no edges");
    std::vector<float> silence ((size_t) rate / 2);
    run (p, silence);
    const auto knobs = p.effectiveCrossovers(), resting = p.heardCrossovers();
    require (p.areCrossoversMoving(), "An edge LFO moves the dividers as the sound plays");
    for (size_t j = 0; j < knobs.size(); ++j) require (std::abs (resting[j] / knobs[j] - 1.0f) < 0.02f, "A new edge LFO starts where the edges are");
    // Drawn at the top: band 2 moves up as far as it goes (its width kept); at the bottom, down.
    p.setLfoShape (1, lfo, mod::flat (1.0f), false);
    run (p, silence);
    auto up = p.heardCrossovers();
    const float width = uFor (knobs[1]) - uFor (knobs[0]);
    require (up[1] > 15000.0f && std::abs ((uFor (up[1]) - uFor (up[0])) - width) < 0.03f, "Move the band: at the top of the grid the band is at the top, as wide as before");
    p.setLfoShape (1, lfo, mod::flat (0.0f), false);
    run (p, silence);
    auto down = p.heardCrossovers();
    require (down[0] < 25.0f && std::abs ((uFor (down[1]) - uFor (down[0])) - width) < 0.03f, "...at the bottom it is at the bottom");
    // What the sound does: band 2 soloed, a 9 kHz tone (in band 3 to start with) comes through once band 2 moves up
    // (its middle on 9 kHz).
    std::vector<float> tone ((size_t) rate);
    addTone (tone, 0, tone.size(), 9000.0, 0.5f);
    p.setBandSoloed (1, true);
    p.setLfoShape (1, lfo, mod::flat ((uFor (9000.0f) - 0.5f * width) / (1.0f - width)), false);
    const auto through = run (p, tone);
    p.setLfoShape (1, lfo, mod::flat ((uFor (std::sqrt (knobs[0] * knobs[1])) - 0.5f * width) / (1.0f - width)), false);
    const auto blocked = run (p, tone);
    const float inDb = db (rms (tone, tone.size() / 2, tone.size()));
    const float passDb = db (rms (through, tone.size() / 2, tone.size())) - inDb, stopDb = db (rms (blocked, tone.size() / 2, tone.size())) - inDb;
    require (passDb > -3.0f && stopDb < -30.0f, "Band 2 moved onto 9 kHz lets the tone through; moved back, it does not");
    // Open / close: closed, a 300 Hz tone (inside band 2 to start with) is gone; open, it is there.
    const int open = p.linkEdgesToLfo (1, D::openEdges);
    p.removeRoute (1, p.getRoutesFrom (1, lfo).front());
    std::vector<float> low ((size_t) rate);
    addTone (low, 0, low.size(), 300.0, 0.5f);
    p.setLfoShape (1, open, mod::flat (0.0f), false);
    const auto closed = run (p, low);
    p.setLfoShape (1, open, mod::flat (1.0f), false);
    const auto opened = run (p, low);
    const float closedDb = db (rms (closed, low.size() / 2, low.size())) - db (rms (low, low.size() / 2, low.size()));
    const float openDb = db (rms (opened, low.size() / 2, low.size())) - db (rms (low, low.size() / 2, low.size()));
    require (closedDb < -24.0f && openDb > -3.0f, "Open / close: closed, the band lets nothing through; open, everything");
    // A fast LFO (a 1/32 sweep across everything) on every edge mode at once: no NaN, the output stays bounded.
    TriadProcessor f;
    f.prepareToPlay (rate, 256);
    f.addBand (6000.0f);
    for (int b = 0; b < f.getBandCount(); ++b)
        for (int e = 0; e < 4; ++e)
            if (const int l = f.linkEdgesToLfo (b, e); l >= 0)
            {
                f.setLfoShape (b, l, mod::Shape { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, false);
                f.setLfoLength (b, l, 4 + (b + e) % 3);   // 1/128, 1/64, 1/32: sweeps many times a second
            }
    std::vector<float> noise ((size_t) rate);
    juce::Random r (5);
    for (auto& v : noise) v = 0.5f * (r.nextFloat() - 0.5f);
    const auto fast = run (f, noise, 256);
    float peak = 0.0f;
    for (auto v : fast) { require (std::isfinite (v), "Fast edge LFOs: no NaN / Inf"); peak = juce::jmax (peak, std::abs (v)); }
    require (peak < 2.0f, "Fast edge LFOs on every edge: the output stays bounded");
    // The routes come back with the project.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    TriadProcessor q;
    q.prepareToPlay (rate, 512);
    q.setStateInformation (state.getData(), (int) state.getSize());
    require (q.findEdgeLfo (1, D::openEdges) == open && q.getLfoInfo (1, open).edge == D::openEdges, "Edge LFOs are saved with the project");
    // The frequency panel: where an LFO takes each divider, and the menus.
    // (band 2 open all the way: its low edge at 20 Hz, its high edge at the top)
    const auto sweep = q.crossoverSweep (0), sweepHigh = q.crossoverSweep (1);
    require (sweep.second > 0.0f && sweep.first < 25.0f && sweep.second < 25.0f, "The panel knows where an LFO takes a divider");
    require (sweepHigh.first > 15000.0f, "...on both of the band's edges");
    q.setLfoShape (1, open, mod::Shape { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, false);
    require (q.crossoverSweep (0).first < 25.0f && q.crossoverSweep (0).second > 600.0f, "...and how far it sweeps it");
    {
        // A band's high cut LFO moves its upper divider only: the one below shows no LFO.
        TriadProcessor r;
        r.prepareToPlay (rate, 512);
        r.linkEdgesToLfo (1, D::highEdge);
        require (r.crossoverSweep (1).second > 0.0f, "A high cut LFO shows on its divider");
        require (r.crossoverSweep (0).second <= 0.0f, "...and not on the band's other divider");
        r.linkEdgesToLfo (2, D::lowEdge);
        require (r.crossoverSweep (0).second <= 0.0f, "A low cut LFO of the band above does not show on the divider below it");
    }
    auto host = makeCrossoverHost (q);
    juce::PopupMenu menu;
    CrossoverDisplay::addLinkItems (menu, *host, 1, 200);
    int items = 0;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();) if (! it.getItem().isSectionHeader && it.getItem().itemID >= 200) ++items;
    require (items == 4, "A middle band's menu links its four edge moves to an LFO");
    juce::PopupMenu lowest;
    CrossoverDisplay::addLinkItems (lowest, *host, 0, 200);
    items = 0;
    for (juce::PopupMenu::MenuItemIterator it (lowest); it.next();) if (! it.getItem().isSectionHeader && it.getItem().itemID >= 200) ++items;
    require (items == 3, "The lowest band's menu has no low edge");
    // The window: linking from the panel opens the LFO tab on the new LFO.
    {
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (q.createEditor()));
        editor->setVisible (true);
        editor->linkEdgesToLfo (0, D::highEdge);
        require (editor->getTab() == TriadEditor::Tab::lfo && editor->getLfoPage().getBand() == 0
                 && q.getLfoInfo (0, editor->getLfoPage().getLfo()).edge == D::highEdge, "Link to LFO opens the band's LFO tab on it");
        themeTests::snapshot (screenshots, "Triad-edge-lfo.png", *editor);
        require (editor->getLfoPage().isEdgeNowShowing(), "A band-edge LFO shows BAND NOW in its routing card");
        editor->getLfoPage().select (0, 0);
        require (! editor->getLfoPage().isEdgeNowShowing(), "...and other LFOs do not");
        editor->linkEdgesToLfo (0, D::highEdge);
        require (editor->getLfoPage().isEdgeNowShowing(), "...back on the edge LFO, it shows again");
        editor->setTab (TriadEditor::Tab::bands);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        themeTests::snapshot (screenshots, "Triad-edges-moving.png", *editor);
        // The panel shows what is heard: a muted band, and every band but the soloed ones, lose their colour.
        for (int b = 0; b < q.getBandCount(); ++b) { q.setBandSoloed (b, false); q.setBandMuted (b, false); }
        require (host->isBandHeard (0) && host->isBandHeard (1) && host->isBandHeard (2), "Every band is heard to start with");
        q.setBandMuted (0, true);
        require (! host->isBandHeard (0) && host->isBandHeard (1), "A muted band is shown as not heard");
        q.setBandSoloed (2, true);
        require (! host->isBandHeard (0) && ! host->isBandHeard (1) && host->isBandHeard (2), "SOLO: only the soloed band is shown as heard");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        themeTests::snapshot (screenshots, "Triad-solo-band3.png", *editor);
        q.setBandSoloed (2, false);
        q.setBandMuted (0, false);
        require (host->isBandHeard (0) && host->isBandHeard (1) && host->isBandHeard (2), "...and all are heard again after");
    }
    std::cout << "Band edges: move (9 kHz through " << passDb << " dB, blocked " << stopDb << " dB), open / close (closed " << closedDb
              << " dB, open " << openDb << " dB), fast LFOs (peak " << peak << "), saved, menus: passed" << std::endl;
}

inline void run (const juce::File& screenshots)
{
    splitterTests();
    soloTests();
    edgeTests (screenshots);
}
}
