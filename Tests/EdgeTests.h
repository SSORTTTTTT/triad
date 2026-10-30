#pragma once
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/TriadTime.h"
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
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();) if (! it.getItem().isSectionHeader && it.getItem().itemID >= 200 && it.getItem().itemID < 204) ++items;
    require (items == 4, "A middle band's menu links its four edge moves to an LFO");
    juce::PopupMenu lowest;
    CrossoverDisplay::addLinkItems (lowest, *host, 0, 200);
    items = 0;
    for (juce::PopupMenu::MenuItemIterator it (lowest); it.next();) if (! it.getItem().isSectionHeader && it.getItem().itemID >= 200 && it.getItem().itemID < 204) ++items;
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
        // 0.19.0 (Thomas: "you can't click anything because it's moving all the time"): while LFOs move the edges, the
        // dividers stay where they are set; the movement shows behind them (Ghost movement, on or off in the gear menu).
        {
            std::vector<float> noise ((size_t) rate / 4);
            juce::Random rr (9);
            for (auto& v : noise) v = 0.3f * (rr.nextFloat() - 0.5f);
            run (q, noise, 256);
            auto& display = editor->getDisplay();
            display.refreshIfChanged();
            const auto set = q.effectiveCrossovers(), heard = q.heardCrossovers();
            bool moved = false;
            for (size_t j = 0; j < set.size() && j < heard.size(); ++j) moved = moved || std::abs (heard[j] - set[j]) > 1.0f;
            require (moved, "The LFOs move the edges while audio plays");
            require (host->heardCrossovers() == heard && host->effectiveCrossovers() == set, "The panel knows where the edges are set and where they are now");
            for (int j = 0; j < display.getNumHandles() && j < (int) set.size(); ++j)
                require (std::abs ((float) display.getHandle (j)->getBounds().getCentreX() - display.xForFrequency (set[(size_t) j])) < 2.0f,
                         "The dividers stay where they are set while LFOs move the edges");
            CrossoverDisplay::ghostMovement = true;
            themeTests::snapshot (screenshots, "Triad-ghost-movement.png", *editor);
            CrossoverDisplay::ghostMovement = false;
            themeTests::snapshot (screenshots, "Triad-ghost-off.png", *editor);
            juce::SharedResourcePointer<TriadSettings> settings;
            const bool was = settings->getGhostMovement();
            settings->setGhostMovement (! was);
            require (settings->getGhostMovement() == ! was, "Ghost movement on / off is kept in the settings");
            settings->setGhostMovement (was);
            CrossoverDisplay::ghostMovement = was;
        }
    }
    std::cout << "Band edges: move (9 kHz through " << passDb << " dB, blocked " << stopDb << " dB), open / close (closed " << closedDb
              << " dB, open " << openDb << " dB), fast LFOs (peak " << peak << "), saved, menus: passed" << std::endl;
}

// 0.19.0 (Thomas: "lock band travel"): LOCK keeps a band-edge LFO inside the band as set on the BANDS tab.
inline void lockTests (const juce::File& screenshots)
{
    constexpr double rate = 48000.0;
    std::vector<float> silence ((size_t) rate / 2);
    TriadProcessor p;
    p.prepareToPlay (rate, 512);
    const auto set = p.effectiveCrossovers();   // 250 Hz, 2.5 kHz
    require (p.getBandEdgeLock (2) == -1, "A band with no edge LFO has nothing to lock");
    const int lfo = p.linkEdgesToLfo (2, D::lowEdge);   // band 3's low cut
    const int route = p.getRoutesFrom (2, lfo).front();
    require (p.getBandEdgeLock (2) == 0 && ! p.isEdgeLfoLocked (2, lfo), "A new edge LFO is not locked (crossing over is the default)");
    // Unlocked, the bottom of the grid takes band 3's low cut far below its band (squeezing band 2).
    p.setLfoShape (2, lfo, mod::flat (0.0f), false);
    run (p, silence);
    require (p.heardCrossovers()[1] < set[1] * 0.5f, "Unlocked: the low cut can go below the band");
    // Locking keeps the edges where they are: the line is redrawn for the band's range.
    p.setLfoShape (2, lfo, mod::flat (uFor (6000.0f)), false);
    run (p, silence);
    const float before = p.heardCrossovers()[1];
    p.setRouteLocked (2, route, true);
    require (p.isEdgeLfoLocked (2, lfo) && p.getRouteInfo (2, route).locked && p.getBandEdgeLock (2) == 1, "LOCK on");
    run (p, silence);
    require (std::abs (p.heardCrossovers()[1] / before - 1.0f) < 0.03f, "Locking leaves the edge where it was");
    require (std::abs (p.getLfoInfo (2, lfo).shape.front().y - (uFor (6000.0f) - uFor (set[1])) / (1.0f - uFor (set[1]))) < 0.01f,
             "...its line now reads in the band's range");
    // Locked: whatever is drawn (and inverted), the low cut stays inside band 3 (2.5 kHz up); band 2's own low edge
    // is never touched.
    juce::Random r (31);
    float lowest = 1.0e9f, highest = 0.0f;
    for (int round = 0; round < 12; ++round)
    {
        mod::Shape shape;
        const int points = 2 + r.nextInt (8);
        for (int i = 0; i < points; ++i) shape.push_back ({ i == 0 ? 0.0f : i == points - 1 ? 1.0f : r.nextFloat(), r.nextFloat(), (r.nextFloat() - 0.5f) * 8.0f });
        p.setLfoShape (2, lfo, shape, false);
        p.setRouteInvert (2, route, r.nextBool());
        p.setLfoLength (2, lfo, 4 + r.nextInt (3));   // fast: many cycles in each run
        std::vector<float> noise ((size_t) rate / 8);
        for (auto& v : noise) v = 0.2f * (r.nextFloat() - 0.5f);
        const int blocks = (int) noise.size() / 256;
        for (int k = 0; k < blocks; ++k)
        {
            std::vector<float> piece (noise.begin() + k * 256, noise.begin() + (k + 1) * 256);
            run (p, piece, 256);
            const auto heard = p.heardCrossovers();
            lowest = juce::jmin (lowest, heard[1]); highest = juce::jmax (highest, heard[1]);
            require (std::abs (heard[0] / set[0] - 1.0f) < 0.01f, "Locked: band 2's own low edge does not move");
        }
    }
    require (lowest > set[1] * 0.97f, "Locked: the low cut never goes below the band as set");
    require (highest > set[1] * 2.0f, "...and still moves inside it");
    const auto sweep = p.crossoverSweep (1);
    require (sweep.first > set[1] * 0.97f, "The range lane shows the locked range");
    // Moving the divider on the BANDS tab moves the locked range with it.
    if (auto* c = p.crossoverParameter (1)) c->setValueNotifyingHost (c->convertTo0to1 (4000.0f));
    p.setLfoShape (2, lfo, mod::flat (0.0f), false);
    p.setRouteInvert (2, route, false);
    run (p, silence);
    require (std::abs (p.heardCrossovers()[1] / 4000.0f - 1.0f) < 0.03f, "The locked range follows the divider (bottom of the grid = the band as set)");
    // Saved with the project; Sweep the band cannot be locked; the band menu locks and unlocks them all.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    TriadProcessor q;
    q.prepareToPlay (rate, 512);
    q.setStateInformation (state.getData(), (int) state.getSize());
    require (q.isEdgeLfoLocked (2, lfo), "LOCK is saved with the project");
    const int sweepLfo = q.linkEdgesToLfo (1, D::moveEdges);
    q.setRouteLocked (1, q.getRoutesFrom (1, sweepLfo).front(), true);
    require (! q.isEdgeLfoLocked (1, sweepLfo) && q.getBandEdgeLock (1) == -1, "Sweep the band cannot be locked");
    q.linkEdgesToLfo (1, D::highEdge);
    q.setBandEdgesLocked (1, true);
    require (q.getBandEdgeLock (1) == 1, "The band menu locks the band's LFOs");
    q.setBandEdgesLocked (1, false);
    require (q.getBandEdgeLock (1) == 0, "...and unlocks them");
    // The LFO tab of a locked LFO: its scale is the band's range, with LOCK lit on its card.
    {
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
        editor->setVisible (true);
        editor->setTab (TriadEditor::Tab::lfo);
        editor->getLfoPage().select (2, lfo);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        themeTests::snapshot (screenshots, "Triad-lock.png", *editor);
    }
    std::cout << "LOCK: the low cut stayed inside band 3 (" << lowest << " Hz up to " << highest << " Hz, band from " << set[1]
              << " Hz), locking keeps the edge, follows the divider, saved, menus: passed" << std::endl;
}

// 0.20.0 (Thomas, testing v18): Reset band state, an LFO switched on / off as a whole, Reset wave, SOLO on a band alone,
// the SLOPE of the edges an LFO moves, and a more detailed spectrum.
inline void v19Tests (const juce::File& screenshots)
{
    constexpr double rate = 48000.0;
    std::vector<float> silence ((size_t) rate / 2);
    TriadProcessor p;
    p.prepareToPlay (rate, 512);
    const auto set = p.effectiveCrossovers();
    // Reset band state: band 2 as a new band (its effect, its LFOs and band-edge LFOs gone, mix 100%, 0 dB).
    const int freshSlots = p.getSlotCount (1);
    require (p.loadEffect (1, 0, TriadTime::makeDescription(), false).isEmpty(), "An effect in band 2");
    p.addSlot (1);
    const auto setPlain = [&] (const juce::String& id, float v) { auto* par = p.parameters.getParameter (id); par->setValueNotifyingHost (par->convertTo0to1 (v)); };
    const auto plain = [&] (const juce::String& id) { return p.parameters.getRawParameterValue (id)->load(); };
    setPlain (TriadProcessor::bandMixID (1), 40.0f);
    setPlain (TriadProcessor::bandGainID (1), -6.0f);
    const int sweep = p.linkEdgesToLfo (1, D::moveEdges);
    p.setLfoShape (1, sweep, mod::flat (0.9f), false);
    p.setBandMuted (1, true);
    require (p.resetBandState (1).isEmpty(), "Reset band state");
    run (p, silence);
    require (p.getSlotCount (1) == freshSlots && ! p.getSlotInfo (1, 0).loaded, "...the band's effects are gone");
    require (p.findEdgeLfo (1, D::moveEdges) < 0, "...and its band-edge LFO");
    require (std::abs (plain (TriadProcessor::bandMixID (1)) - 100.0f) < 0.01f && std::abs (plain (TriadProcessor::bandGainID (1))) < 0.01f,
             "...its mix and volume as new");
    const auto heard = p.heardCrossovers();
    require (std::abs (heard[0] / set[0] - 1.0f) < 0.01f && std::abs (heard[1] / set[1] - 1.0f) < 0.01f && p.isBandMuted (1), "...its crossovers and MUTE stay");
    p.setBandMuted (1, false);
    // An LFO on / off as a whole, from the dot on its tab.
    const int cut = p.linkEdgesToLfo (2, D::lowEdge);
    p.setLfoShape (2, cut, mod::flat (uFor (8000.0f)), false);
    run (p, silence);
    require (p.isLfoOn (2, cut) && p.heardCrossovers()[1] > set[1] * 2.0f, "A drawn band-edge LFO is on and moves its edge");
    p.setLfoOn (2, cut, false);
    run (p, silence);
    require (! p.isLfoOn (2, cut) && std::abs (p.heardCrossovers()[1] / set[1] - 1.0f) < 0.02f, "Off: the edge goes back to where it is set");
    p.setLfoOn (2, cut, true);
    run (p, silence);
    require (p.isLfoOn (2, cut) && p.heardCrossovers()[1] > set[1] * 2.0f, "On again: it moves again");
    // Reset wave: flat where the edges are set, nothing moves (unlocked, locked, inverted).
    const auto atRest = [&]
    {
        run (p, silence);
        return std::abs (p.heardCrossovers()[1] / set[1] - 1.0f) < 0.02f;
    };
    p.resetLfoShape (2, cut);
    require (atRest(), "Reset wave: the edge back where it is set");
    const int route = p.getRoutesFrom (2, cut).front();
    p.setLfoShape (2, cut, mod::flat (0.9f), false);
    p.setRouteLocked (2, route, true);
    p.resetLfoShape (2, cut);
    require (atRest(), "...also when locked");
    p.setRouteInvert (2, route, true);
    p.setLfoShape (2, cut, mod::flat (0.2f), false);
    p.resetLfoShape (2, cut);
    require (atRest(), "...and inverted");
    p.setRouteInvert (2, route, false);
    p.setRouteLocked (2, route, false);
    // SOLO: a click solos the band alone (SOLO on band 1, then on band 2: only band 2); Cmd-click adds.
    p.soloOnly (0);
    p.soloOnly (1);
    require (! p.isBandSoloed (0) && p.isBandSoloed (1) && ! p.isBandSoloed (2), "SOLO on another band: only that band is soloed");
    p.setBandSoloed (2, true);
    require (p.isBandSoloed (1) && p.isBandSoloed (2), "Cmd-click: added to the bands soloed");
    p.soloOnly (-1);
    require (! p.isAnyBandSoloed(), "...and none after");
    // The window: SOLO buttons, the LFO tab's SLOPE and Reset wave.
    {
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
        editor->setVisible (true);
        const auto clickSolo = [&] (int band)
        {
            auto* strip = editor->getStrip (band);
            require (strip != nullptr, "A band strip");
            juce::TextButton* solo = nullptr;
            for (auto* c : strip->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && (b->getButtonText() == "SOLO" || b->getButtonText() == "S")) solo = b;
            require (solo != nullptr, "A SOLO button");
            solo->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        };
        clickSolo (0);
        clickSolo (2);
        require (! p.isBandSoloed (0) && p.isBandSoloed (2), "Clicking SOLO on band 3 after band 1: band 3 alone");
        clickSolo (2);
        require (! p.isAnyBandSoloed(), "...and clicking it again: none");
        editor->setTab (TriadEditor::Tab::lfo);
        auto& page = editor->getLfoPage();
        page.select (2, cut);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        require (page.movedDividers() == std::vector<int> { 1 } && page.slopeButton.isVisible(), "SLOPE: band 3's low cut is the 2.5 kHz divider");
        require (page.slopeButton.getButtonText().contains ("24"), "...at 24 dB/oct");
        p.setCrossoverSlope (1, 2);
        page.refresh();
        require (page.slopeButton.getButtonText().contains ("12"), "...it follows the divider's slope (12 dB/oct)");
        p.setLfoShape (2, cut, mod::flat (0.9f), false);
        page.getEditor().reload();
        require (page.getEditor().getHost() != nullptr && page.getEditor().getHost()->canResetShape(), "Reset wave in the wave menu");
        page.getEditor().getHost()->resetShape();
        page.getEditor().reload();
        require (atRest(), "Reset wave from the LFO tab");
        page.select (1, 0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        require (page.movedDividers().empty() && ! page.slopeButton.isVisible(), "No SLOPE for an LFO that does not move the edges");
        themeTests::snapshot (screenshots, "Triad-v19-lfo.png", *editor);
    }
    require (triad::SpectrumTap::size == 16384, "Room for a spectrum twice as detailed (8192 points)");
    std::cout << "0.20.0: Reset band state, LFO on / off, Reset wave (locked, inverted), SOLO alone (and Cmd-click), SLOPE, spectrum: passed" << std::endl;
}

// 0.21.0 (Thomas, testing v19): Triad Time opens its own window (the installed copy is loaded as the built-in), and a
// band added then an empty band removed puts the dividers back.
inline void v191Tests()
{
    constexpr double rate = 48000.0;
    TriadProcessor p;
    p.prepareToPlay (rate, 512);
    // Triad Time installed as a VST3 or an AU: the built-in loads, with the same settings.
    for (const auto* format : { "VST3", "AudioUnit" })
    {
        juce::PluginDescription installed;
        installed.name = "Triad Time";
        installed.pluginFormatName = format;
        installed.manufacturerName = "Thomas Ross";
        installed.fileOrIdentifier = "/Library/Audio/Plug-Ins/Triad Time";
        require (p.loadEffect (0, 0, installed, false).isEmpty(), "Triad Time (installed) loads");
        require (p.getSlotInfo (0, 0).loaded && p.getSlotInfo (0, 0).format == TriadTime::formatName, "...as Triad's built-in Triad Time (its own window)");
        p.removeSlot (0, 0);
    }
    TriadTime original;
    original.getParameters()[2]->setValueNotifyingHost (0.73f);
    juce::MemoryBlock own;
    original.getStateInformation (own);
    // A VST3's kept state: Triad Time's own state, plus JUCE's private data after it, in the IComponent chunk.
    juce::MemoryBlock chunk (own);
    const char trailer[] = "\0\0\0\0\0\0\0\0JUCEPrivateData";
    chunk.append (trailer, sizeof (trailer));
    juce::XmlElement vst3 ("VST3PluginState");
    vst3.createNewChildElement ("IComponent")->addTextElement (chunk.toBase64Encoding());
    juce::MemoryBlock vst3State;
    juce::AudioProcessor::copyXmlToBinary (vst3, vst3State);
    // An AU's: its class info (a binary property list) with Triad Time's state inside as it is.
    juce::MemoryBlock auState ("bplist00\xd4\x01\x02\x03jucePluginState", 27);
    auState.append (own.getData(), own.getSize());
    auState.append ("\x08\x0f\x1f", 3);
    for (const auto& kept : { vst3State, auState })
    {
        TriadTime restored;
        const auto converted = TriadProcessor::triadTimeStateFromInstalled (kept);
        restored.setStateInformation (converted.getData(), (int) converted.getSize());
        require (std::abs (restored.getParameters()[2]->getValue() - original.getParameters()[2]->getValue()) < 0.001f, "The installed Triad Time's settings carry over");
    }
    // A band added (+ BAND), then the empty top band removed: the dividers back where they were.
    TriadProcessor q;
    q.prepareToPlay (rate, 512);
    const auto before = q.effectiveCrossovers();
    require (q.addBand() && q.getBandCount() == 4, "+ BAND");
    require (q.isBandEmpty (3) && q.removeBand (3) && q.getBandCount() == 3, "Remove the empty top band");
    auto after = q.effectiveCrossovers();
    require (after.size() == before.size() && std::abs (after[0] / before[0] - 1.0f) < 0.001f && std::abs (after[1] / before[1] - 1.0f) < 0.001f,
             "...the dividers are back where they were before the band was added");
    // Not when the band removed has an effect, or the dividers were moved after adding.
    require (q.addBand(), "+ BAND again");
    require (q.loadEffect (3, 0, TriadTime::makeDescription(), false).isEmpty() && q.removeBand (3), "Remove a band with an effect");
    after = q.effectiveCrossovers();
    require (after.size() == 2 && std::abs (after[1] / before[1] - 1.0f) > 0.01f, "...it goes as it always did (its range joins the band below)");
    TriadProcessor r;
    r.prepareToPlay (rate, 512);
    r.addBand();
    if (auto* c = r.crossoverParameter (2)) c->setValueNotifyingHost (c->convertTo0to1 (9000.0f));
    const auto moved = r.effectiveCrossovers();
    r.removeBand (3);
    require (std::abs (r.effectiveCrossovers()[0] / moved[0] - 1.0f) < 0.001f, "...and when the dividers were moved after adding");
    std::cout << "0.21.0: Triad Time loads as the built-in (VST3 / AU settings carried over), + BAND then removing an empty band restores the dividers: passed" << std::endl;
}

// 0.21.0 (Thomas, testing v19): the range lane's ends dragged on the BANDS tab change where an LFO takes a divider (its
// wave stretched to match), SPECTRUM on every LFO's grid, and FULL SPECTRUM beside it.
inline void v21Tests (const juce::File& screenshots)
{
    constexpr double rate = 48000.0;
    TriadProcessor p;
    p.prepareToPlay (rate, 512);
    const auto set = p.effectiveCrossovers();   // 250 Hz, 2.5 kHz
    const auto near = [] (float a, float b, float tolerance) { return std::abs (a / b - 1.0f) < tolerance; };
    require (! p.beginSweepDrag (1, true), "No LFO on a divider: its range cannot be dragged");
    // Band 2's high cut, between 158 Hz and 5 kHz over the cycle.
    const int lfo = p.linkEdgesToLfo (1, D::highEdge);
    const int route = p.getRoutesFrom (1, lfo).front();
    const mod::Shape wave { { 0.0f, 0.3f, 0.0f }, { 0.5f, 0.8f, 2.0f }, { 1.0f, 0.3f, 0.0f } };
    p.setLfoShape (1, lfo, wave, true);
    p.commitHistory();
    const auto before = p.crossoverSweep (1);
    require (near (before.first, 20.0f * std::pow (1000.0f, 0.3f), 0.02f) && near (before.second, 20.0f * std::pow (1000.0f, 0.8f), 0.02f), "The range: 158 Hz to 5 kHz");
    require (! p.beginSweepDrag (0, true), "...the divider below has none");
    // The top end dragged to 10 kHz: the wave's highest point moves there, its lowest stays, the shape is kept.
    require (p.beginSweepDrag (1, true), "Grab the range's top end");
    p.dragSweepTo (7000.0f);
    p.dragSweepTo (10000.0f);
    p.endSweepDrag();
    p.commitHistory();   // (the editor's timer keeps undo steps)
    auto now = p.crossoverSweep (1);
    require (near (now.second, 10000.0f, 0.02f) && near (now.first, before.first, 0.01f), "Dragged to 10 kHz: the top moves there, the bottom stays");
    const auto shape = p.getLfoInfo (1, lfo).shape;
    require (shape.size() == 3 && std::abs (shape[0].y - 0.3f) < 0.001f && std::abs (shape[1].y - uFor (10000.0f)) < 0.01f && std::abs (shape[1].curve - 2.0f) < 0.001f,
             "...the LFO's wave: its peak moved, its curve and its low points kept");
    require (p.undo() && near (p.crossoverSweep (1).second, before.second, 0.01f), "One undo step puts it back");
    // The bottom end: up to 1 kHz; never past the top (it stops there, flat).
    require (p.beginSweepDrag (1, false), "Grab the bottom end");
    p.dragSweepTo (1000.0f);
    now = p.crossoverSweep (1);
    require (near (now.first, 1000.0f, 0.02f) && near (now.second, before.second, 0.01f), "Dragged to 1 kHz: the bottom moves there, the top stays");
    p.dragSweepTo (15000.0f);
    now = p.crossoverSweep (1);
    require (near (now.first, now.second, 0.01f) && near (now.second, before.second, 0.01f), "...dragged past the top it stops there");
    p.dragSweepTo (1000.0f);
    p.endSweepDrag();
    // Inverted: the ends still follow the mouse.
    p.setRouteInvert (1, route, true);
    const auto inverted = p.crossoverSweep (1);
    require (p.beginSweepDrag (1, true), "Inverted: grab the top end");
    p.dragSweepTo (12000.0f);
    p.endSweepDrag();
    now = p.crossoverSweep (1);
    require (near (now.second, 12000.0f, 0.02f) && near (now.first, inverted.first, 0.01f), "...dragged to 12 kHz: the top moves there, the bottom stays");
    p.setRouteInvert (1, route, false);
    // Locked: the range stays inside the band as set.
    p.setLfoShape (1, lfo, wave, true);
    p.setRouteLocked (1, route, true);
    require (p.beginSweepDrag (1, true), "Locked: grab the top end");
    p.dragSweepTo (15000.0f);
    p.endSweepDrag();
    now = p.crossoverSweep (1);
    require (now.second <= set[1] * 1.01f && now.first >= set[0] * 0.99f, "...it stays inside the band");
    p.setRouteLocked (1, route, false);
    p.setLfoShape (1, lfo, wave, true);

    // The BANDS tab: the lane's ends, grabbed and dragged with the mouse.
    std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
    editor->setVisible (true);
    std::vector<float> noise ((size_t) rate / 2);
    juce::Random rr (21);
    for (auto& v : noise) v = 0.3f * (rr.nextFloat() - 0.5f);
    run (p, noise, 256);
    auto& display = editor->getDisplay();
    display.refresh();
    const auto lanes = display.laneRanges();
    require (lanes.size() == 1 && lanes.front().divider == 1, "The lane shows divider 2's range");
    const auto& lane = lanes.front();
    require (std::abs (lane.x2 - display.xForFrequency (before.second)) < 1.0f && lane.y > display.plotBounds().getBottom(), "...from where it goes to where it goes, under the spectrum");
    const auto top = display.laneEndAt ({ lane.x2 + 3.0f, lane.y + 2.0f });
    require (top.divider == 1 && top.high && display.laneEndAt ({ lane.x1 - 2.0f, lane.y }).divider == 1 && ! display.laneEndAt ({ lane.x1 - 2.0f, lane.y }).high,
             "Its two ends can be grabbed");
    require (display.laneEndAt ({ 0.5f * (lane.x1 + lane.x2), lane.y }).divider < 0 && display.laneEndAt ({ lane.x2, display.plotBounds().getCentreY() }).divider < 0,
             "...not its middle, nor the spectrum above");
    for (int j = 0; j < display.getNumHandles(); ++j)
    {
        auto* handle = display.getHandle (j);
        const auto local = juce::Point<float> (lane.x2, lane.y) - handle->getPosition().toFloat();
        if (handle->getLocalBounds().toFloat().contains (local))
            require (! handle->hitTest ((int) local.x, (int) local.y), "A divider's handle leaves the range's end to the lane");
    }
    const auto mouse = [&display] (juce::Point<float> at, bool dragging)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                 &display, &display, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, dragging);
    };
    const float to = display.xForFrequency (8000.0f);
    display.mouseMove (mouse ({ lane.x2, lane.y }, false));
    require (display.getTooltip().contains ("LFO " + juce::String (lfo + 1)), "Its tooltip says which LFO it is");
    display.mouseDown (mouse ({ lane.x2, lane.y }, false));
    require (display.getLaneDrag().divider == 1 && display.getLaneDrag().high, "Mouse down on the top end grabs it");
    display.mouseDrag (mouse ({ to, lane.y }, true));
    themeTests::snapshot (screenshots, "Triad-v21-lane-drag.png", *editor);
    display.mouseUp (mouse ({ to, lane.y }, false));
    require (near (p.crossoverSweep (1).second, 8000.0f, 0.03f) && display.getLaneDrag().divider < 0, "...dragged to 8 kHz with the mouse: the LFO now takes the divider there");
    require (near (display.laneRanges().front().x2, to, 0.01f), "...and the lane shows it");
    display.mouseMove (mouse ({ lane.x1, lane.y }, false));
    themeTests::snapshot (screenshots, "Triad-v21-lane.png", *editor);

    // The LFO tab: SPECTRUM and FULL SPECTRUM on every LFO's grid.
    juce::SharedResourcePointer<TriadSettings> settings;
    const auto songWas = settings->getLfoBackdrop (false);
    const bool fullWas = settings->getLfoFullSpectrum();
    settings->setLfoFullSpectrum (false);
    editor->setTab (TriadEditor::Tab::lfo);
    auto& page = editor->getLfoPage();
    require (p.getLfoInfo (1, lfo).shape != wave, "(the wave was changed on the BANDS tab)");
    page.select (1, lfo);
    require (page.getEditorShape() == p.getLfoInfo (1, lfo).shape, "The LFO tab shows the wave as dragged on the BANDS tab");
    require (page.spectrumButton.isVisible() && page.fullSpectrumButton.isVisible(), "A band-edge LFO: SPECTRUM and FULL SPECTRUM");
    run (p, noise, 256);
    themeTests::snapshot (screenshots, "Triad-v21-edge-spectrum.png", *editor);
    page.select (1, 0);   // the band's own LFO (its mix), not on the edges
    require (! page.showsEdges() && page.spectrumButton.isVisible() && page.fullSpectrumButton.isVisible(), "An LFO on a knob: SPECTRUM and FULL SPECTRUM too");
    settings->setLfoBackdrop (false, TriadSettings::Backdrop::off);
    page.refresh();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
    page.refresh();
    require (! page.spectrumButton.getToggleState(), "...SPECTRUM off with nothing behind the grid");
    page.spectrumButton.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    require (page.spectrumButton.getToggleState() && settings->getLfoBackdrop (false) == TriadSettings::Backdrop::spectrum, "SPECTRUM on: the spectrum behind the grid (kept)");
    const auto bandView = page.spectrumView (uFor (set[0]), uFor (set[1]));
    require (near (bandView.first, set[0], 0.01f) && near (bandView.second, set[1], 0.01f), "...across the grid: band 2 as set");
    run (p, noise, 256);
    themeTests::snapshot (screenshots, "Triad-v21-spectrum.png", *editor);
    page.fullSpectrumButton.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    require (settings->getLfoFullSpectrum() && page.fullSpectrumButton.getToggleState(), "FULL SPECTRUM on (kept)");
    const auto full = page.spectrumView (uFor (set[0]), uFor (set[1]));
    require (near (full.first, 20.0f, 0.01f) && near (full.second, 20000.0f, 0.01f), "...20 Hz to 20 kHz");
    run (p, noise, 256);
    themeTests::snapshot (screenshots, "Triad-v21-full-spectrum.png", *editor);
    page.spectrumButton.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    require (settings->getLfoBackdrop (false) == TriadSettings::Backdrop::off, "SPECTRUM off: back to what was behind the grid before");
    const auto narrow = page.spectrumView (uFor (1000.0f), uFor (1100.0f));
    settings->setLfoFullSpectrum (false);
    page.refresh();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
    const auto zoomed = page.spectrumView (uFor (1000.0f), uFor (1100.0f));
    require (near (narrow.first, 20.0f, 0.01f) && zoomed.second / zoomed.first > 1.99f, "A narrow range is shown at least an octave wide");
    settings->setLfoBackdrop (false, songWas);
    settings->setLfoFullSpectrum (fullWas);
    std::cout << "0.21.0: the range lane's ends drag where an LFO takes a divider (top, bottom, inverted, locked, undo, mouse), SPECTRUM and FULL SPECTRUM on every LFO: passed" << std::endl;
}

inline void run (const juce::File& screenshots)
{
    splitterTests();
    soloTests();
    edgeTests (screenshots);
    lockTests (screenshots);
    v19Tests (screenshots);
    v191Tests();
    v21Tests (screenshots);
}
}
