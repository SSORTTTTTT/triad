#pragma once
#include "../Source/TriadSplit.h"
#include "../Source/CrossoverDisplay.h"
#include <cmath>
#include <iostream>

// Triad Split: Triad's band split as a plug-in of its own (split mode), its link groups and band outputs.
namespace splitTests
{
namespace L = triad::link;

struct Outputs
{
    std::vector<float> main, right;
    std::array<std::vector<float>, triad::maxBands> bands;   // the left channel of each band output
};

// Plays `input` (the same on both channels) through `p` in blocks; returns the main output and every band output.
inline Outputs run (TriadSplit& p, const std::vector<float>& input, int block, const std::function<void (int)>& beforeBlock = {})
{
    Outputs o;
    o.main.resize (input.size());
    o.right.resize (input.size());
    for (auto& b : o.bands) b.resize (input.size());
    const int channels = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> audio (channels, block);
    juce::MidiBuffer midi;
    int index = 0;
    for (size_t start = 0; start < input.size(); start += (size_t) block, ++index)
    {
        if (beforeBlock) beforeBlock (index);
        const int n = (int) juce::jmin ((size_t) block, input.size() - start);
        audio.setSize (channels, n, false, false, true);
        audio.clear();
        for (int c = 0; c < juce::jmin (2, p.getTotalNumInputChannels()); ++c)
            for (int s = 0; s < n; ++s) audio.setSample (c, s, input[start + (size_t) s]);
        p.processBlock (audio, midi);
        const int mainChannels = p.getChannelCountOfBus (false, 0);
        for (int s = 0; s < n; ++s)
        {
            o.main[start + (size_t) s] = audio.getSample (0, s);
            o.right[start + (size_t) s] = audio.getSample (juce::jmin (1, mainChannels - 1), s);
        }
        for (int b = 0; b < triad::maxBands && b + 1 < p.getBusCount (false); ++b)
        {
            if (p.getChannelCountOfBus (false, b + 1) == 0) continue;
            const int channel = p.getChannelIndexInProcessBlockBuffer (false, b + 1, 0);
            for (int s = 0; s < n; ++s) o.bands[(size_t) b][start + (size_t) s] = audio.getSample (channel, s);
        }
    }
    return o;
}
inline std::vector<float> noise (size_t n, int seed = 3)
{
    juce::Random r (seed);
    std::vector<float> v (n);
    for (auto& x : v) x = (r.nextFloat() * 2.0f - 1.0f) * 0.4f;
    return v;
}
inline std::vector<float> sine (size_t n, double hz, double rate)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / rate);
    return v;
}
inline float rms (const std::vector<float>& v, size_t from)
{
    double e = 0.0;
    for (size_t i = from; i < v.size(); ++i) e += (double) v[i] * v[i];
    return (float) std::sqrt (e / (double) juce::jmax ((size_t) 1, v.size() - from));
}
inline float maxDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    float worst = 0.0f;
    for (size_t i = from; i < a.size() && i < b.size(); ++i) worst = juce::jmax (worst, std::abs (a[i] - b[i]));
    return worst;
}
inline void setParameter (juce::RangedAudioParameter* p, float value)
{
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (value));
    p->endChangeGesture();
}
inline std::unique_ptr<TriadSplit> make (int group, int play, double rate = 48000.0, int block = 512)
{
    auto s = std::make_unique<TriadSplit>();
    s->setLinkGroup (group);
    s->setPlay (play);
    s->prepareToPlay (rate, block);
    return s;
}

inline void run (const juce::File& screenshotFolder)
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    // ---- What a new Triad Split is ------------------------------------------------------------------------------
    {
        TriadSplit s;
        require (s.getBandCount() == 3 && s.getPlay() == TriadSplit::playAll && s.getLinkGroup() == TriadSplit::defaultGroup,
                 "A new Triad Split: 3 bands, plays ALL, in link group A");
        const auto cuts = s.effectiveCrossovers();
        require (cuts.size() == 2 && std::abs (cuts[0] - 250.0f) < 0.01f && std::abs (cuts[1] - 2500.0f) < 0.01f,
                 "...with Triad's dividers (250 Hz, 2.5 kHz)");
        require (s.getBusCount (false) == 1 + triad::maxBands && s.getTotalNumOutputChannels() == 2 * (1 + triad::maxBands),
                 "...a main output and one output per band (Band 1 to Band 10)");
        require (s.getBus (false, 3)->getName() == "Band 3", "...named Band 1, Band 2...");
        require (s.getLatencySamples() == 0, "...and no latency");
        require (s.getLinkedCount() == 1, "Alone in its group");
    }
    require (L::block().groups[0].members.load() == 0, "A Triad Split leaves its group when it is deleted");

    // ---- The bands add back up; each one hears only its range ------------------------------------------------------
    {
        const auto in = noise (96000);
        auto all = make (2, TriadSplit::playAll);
        auto low = make (2, 1), mid = make (2, 2), high = make (2, 3);
        require (all->getLinkedCount() == 4, "Four Triad Splits in group B");
        const auto a = run (*all, in, block), o1 = run (*low, in, block), o2 = run (*mid, in, block), o3 = run (*high, in, block);
        std::vector<float> sum (in.size());
        for (size_t i = 0; i < in.size(); ++i) sum[i] = o1.main[i] + o2.main[i] + o3.main[i];
        const auto worst = maxDifference (sum, a.main, 0);
        std::cout << "Triad Split: bands 1 + 2 + 3 on three tracks vs ALL: max difference " << worst << std::endl;
        require (worst < 1.0e-5f, "Three Triad Splits playing band 1, 2 and 3 add up exactly to one playing ALL");
        std::vector<float> outputs (in.size());
        for (size_t i = 0; i < in.size(); ++i) outputs[i] = a.bands[0][i] + a.bands[1][i] + a.bands[2][i];
        require (maxDifference (outputs, a.main, 0) < 1.0e-5f, "The band outputs add up to ALL too");
        require (maxDifference (a.bands[1], o2.main, 0) < 1.0e-6f, "Output Band 2 is band 2, whatever PLAY is");
        require (juce::exactlyEqual (rms (a.bands[4], 0), 0.0f), "Outputs for bands that do not exist are silent");
        require (rms (a.main, 4800) > 0.2f * rms (in, 4800), "ALL plays the sound");
        // The same crossovers as Triad: ALL is Triad's all-pass sum (Triad with empty bands).
        {
            TriadProcessor triadProcessor;
            triadProcessor.prepareToPlay (rate, block);
            juce::AudioBuffer<float> audio (2, block);
            juce::MidiBuffer midi;
            std::vector<float> triadOut (in.size());
            for (size_t start = 0; start < in.size(); start += block)
            {
                const int n = (int) juce::jmin ((size_t) block, in.size() - start);
                audio.setSize (2, n, false, false, true);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) audio.setSample (c, i, in[start + (size_t) i]);
                triadProcessor.processBlock (audio, midi);
                for (int i = 0; i < n; ++i) triadOut[start + (size_t) i] = audio.getSample (0, i);
            }
            const auto difference = maxDifference (triadOut, a.main, 9600);
            std::cout << "Triad Split ALL vs Triad (3 empty bands): max difference " << difference << std::endl;
            require (difference < 1.0e-4f, "Triad Split splits exactly like Triad");
        }
        // A sine in each band's range: only that band carries it.
        for (const auto& [hz, band] : { std::pair<double, int> { 80.0, 0 }, { 800.0, 1 }, { 8000.0, 2 } })
        {
            const auto tone = sine (48000, hz, rate);
            const auto t = run (*all, tone, block);
            const float level = rms (tone, 24000);
            for (int b = 0; b < 3; ++b)
            {
                const float db = juce::Decibels::gainToDecibels (rms (t.bands[(size_t) b], 24000) / level, -200.0f);
                if (b == band) require (db > -1.0f, "A tone comes out of its own band at full level");
                else require (db < -24.0f, "...and is far down in the other bands");
            }
        }
    }

    // ---- Linked: move a divider on one, the others follow ------------------------------------------------------------
    {
        auto a = make (3, 1), b = make (3, 2), other = make (4, 2), alone = make (0, 2);
        const auto in = noise (4096, 9);
        setParameter (a->crossoverParameter (0), 400.0f);
        run (*a, in, block);       // a's audio sends the change to the group...
        run (*b, in, block);       // ...and b's audio uses it right away
        require (std::abs (b->getAppliedCrossover (0) - 400.0f) < 0.01f, "A divider moved on one Triad Split moves the sound of every one linked to it");
        require (std::abs (other->getAppliedCrossover (0) - 250.0f) < 0.01f && std::abs (alone->getAppliedCrossover (0) - 250.0f) < 0.01f,
                 "...not in another group, nor when not linked");
        b->syncLink();
        require (std::abs (b->effectiveCrossovers()[0] - 400.0f) < 0.01f, "...and its window and saved state follow");
        // Without audio running (the DAW stopped): the windows still follow each other.
        setParameter (b->slopeParameter (1), 8.0f);
        b->syncLink();
        a->syncLink();
        require (a->getCrossoverSlope (1) == 8, "A slope changed on one follows on the others, with no audio running");
        // Adding a band from one window: every one gets 4 bands.
        require (a->addBand (1000.0f), "+ BAND");
        b->syncLink();
        require (b->getBandCount() == 4 && std::abs (b->effectiveCrossovers()[1] - 1000.0f) < 0.5f, "Bands added on one are added on all linked");
        // Off: keeps its bands from then on.
        b->setLinkGroup (0);
        setParameter (a->crossoverParameter (0), 300.0f);
        a->syncLink();
        b->syncLink();
        require (std::abs (b->effectiveCrossovers()[0] - 400.0f) < 0.01f && b->getLinkedCount() == 0, "LINK OFF: no longer follows");
        // Joining a group that has members: takes its bands.
        b->setLinkGroup (3);
        require (b->getBandCount() == 4 && std::abs (b->effectiveCrossovers()[0] - 300.0f) < 0.01f && b->getLinkedCount() == 2,
                 "Joining a group: takes the group's bands");
        // A new Triad Split in a group with members takes its bands too.
        auto c = std::make_unique<TriadSplit>();
        c->setLinkGroup (3);
        require (c->getBandCount() == 4 && c->getCrossoverSlope (2) == 8 && c->getLinkedCount() == 3, "A new Triad Split joining takes the group's bands");
        // Saved and reopened (in a project with the others): the same group, and the group's bands.
        juce::MemoryBlock state;
        c->setPlay (3);
        c->getStateInformation (state);
        c.reset();
        auto d = std::make_unique<TriadSplit>();
        d->setStateInformation (state.getData(), (int) state.getSize());
        require (d->getLinkGroup() == 3 && d->getPlay() == 3 && d->getBandCount() == 4 && d->getLinkedCount() == 3, "Saved and reopened: group, PLAY and bands");
        // Reopened alone (its group empty): it brings its own bands.
        a.reset(); b.reset(); d.reset();
        require (L::group (3)->members.load() == 0, "Everyone left group C");
        auto e = std::make_unique<TriadSplit>();
        e->setStateInformation (state.getData(), (int) state.getSize());
        require (e->getBandCount() == 4 && std::abs (e->effectiveCrossovers()[0] - 300.0f) < 0.01f && e->getLinkGroup() == 3, "Reopened alone: its own bands");
    }

    // ---- Adding or removing a band moves every member's PLAY with the bands ----------------------------------------------
    {
        auto one = make (5, 1), two = make (5, 2), three = make (5, 3);
        require (one->getPlayers (0) == 1 && one->getPlayers (1) == 1 && one->getPlayers (2) == 1, "Each band is on one track");
        const auto in = noise (24000, 4);
        run (*one, in, block); run (*two, in, block); run (*three, in, block);
        require (one->addBand (1000.0f), "Split band 2 at 1 kHz");   // bands: <250, 250-1k, 1k-2.5k (new), >2.5k
        // Band 3's track: its audio hears the new bands first; it waits, silent, until its PLAY has moved.
        // (Its timer, 60 times a second, moves it along: here after 6 blocks, 64 ms.)
        const auto held = run (*three, in, block, [&] (int index) { if (index == 6) three->syncLink(); });
        float before = 0.0f;
        for (size_t i = 512; i < 6 * (size_t) block; ++i) before = juce::jmax (before, std::abs (held.main[i]));
        require (juce::exactlyEqual (before, 0.0f), "A linked track stays silent while its band moves");
        require (three->getPlay() == 4, "...then plays its band, now band 4");
        require (rms (held.main, 8 * (size_t) block) > 0.01f, "...and is heard again");
        two->syncLink();
        require (two->getPlay() == 2 && one->getPlay() == 1, "Bands below the split keep their number");
        two->syncLink(); three->syncLink();
        require (one->getPlayers (2) == 0 && one->getPlayers (3) == 1, "The new band 3 is on no track yet");
        // Remove band 2: band 2's track has nothing to play; the bands above move down.
        require (one->removeBand (1), "Remove band 2");
        two->syncLink(); three->syncLink();
        require (two->getPlay() == TriadSplit::playNone && three->getPlay() == 3 && one->getPlay() == 1,
                 "Removing a band: its track plays NONE, the bands above keep playing their sound");
        // No sudden jump anywhere in a band change (it fades out and in).
        auto free = make (0, TriadSplit::playAll);
        const auto tone = sine (24000, 300.0, rate);
        const auto faded = run (*free, tone, 256, [&] (int index) { if (index == 40) setParameter (free->parameters.getParameter ("bands"), 5.0f); });
        float jump = 0.0f;
        for (size_t i = 1; i < faded.main.size(); ++i) jump = juce::jmax (jump, std::abs (faded.main[i] - faded.main[i - 1]));
        require (free->getBandCount() == 5 && free->getAppliedBandCount() == 5 && jump < 0.05f, "A change in the number of bands fades: no click");
    }

    // ---- Mono, and the band outputs off ---------------------------------------------------------------------------------
    {
        TriadSplit s;
        s.setLinkGroup (0);
        auto layout = s.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        for (int b = 1; b < layout.outputBuses.size(); ++b) layout.outputBuses.getReference (b) = juce::AudioChannelSet::disabled();
        require (s.setBusesLayout (layout), "Mono, with the band outputs off");
        layout.outputBuses.getReference (2) = juce::AudioChannelSet::stereo();
        require (s.checkBusesLayoutSupported (layout), "A band output can be stereo with a mono main output (and the other way)");
        layout.outputBuses.getReference (2) = juce::AudioChannelSet::quadraphonic();
        require (! s.checkBusesLayoutSupported (layout), "Band outputs are mono or stereo");
        layout.outputBuses.getReference (2) = juce::AudioChannelSet::disabled();
        s.setPlay (2);
        s.prepareToPlay (rate, block);
        const auto o = run (s, sine (24000, 800.0, rate), block);
        require (rms (o.main, 12000) > 0.3f, "Mono plays its band");
    }

    // ---- The process-wide block (macOS: the VST3 and the AU find the same groups) -----------------------------------------
    {
        static int constructed = 0;
        auto* a = triadProcessWideBlock ("audio.thomasross.triadsplit.test", 64, [] (void*) { ++constructed; });
        auto* b = triadProcessWideBlock ("audio.thomasross.triadsplit.test", 64, [] (void*) { ++constructed; });
        auto* c = triadProcessWideBlock ("audio.thomasross.triadsplit.test2", 64, [] (void*) { ++constructed; });
        require (a == b && a != c && constructed == 2, "A shared block is found again by its name");
    }

    // ---- The window ---------------------------------------------------------------------------------------------------
    {
        auto s = make (6, 2);
        auto t = make (6, 1);
        std::unique_ptr<juce::AudioProcessorEditor> editor (s->createEditor());
        CrossoverDisplay* display = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* d = dynamic_cast<CrossoverDisplay*> (child)) display = d;
        require (display != nullptr && display->getNumHandles() == 2, "Triad Split's window: Triad's band display, one divider per crossover");
        require (! display->getPreButton().isVisible() && ! display->getLiveButton().isVisible(), "...without Triad's PRE / POST / LIVE");
        display->setCrossoverValue (1, 3000.0f);
        s->syncLink();   // (its timer, 60 times a second)
        t->syncLink();
        require (std::abs (t->effectiveCrossovers()[1] - 3000.0f) < 0.5f, "Dragging a divider in the window moves it on the linked ones");
        display->getAddButton().onClick();
        require (s->getBandCount() == 4, "+ BAND in the window");
        s->removeBand (3);
        t->syncLink();
        juce::Button* bandOne = nullptr;
        juce::Button* all = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* b = dynamic_cast<juce::Button*> (child))
            {
                if (b->getButtonText() == "BAND 1") bandOne = b;
                if (b->getButtonText() == "ALL") all = b;
            }
        require (bandOne != nullptr && all != nullptr, "PLAY buttons");
        bandOne->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        require (s->getPlay() == 1, "Clicking a PLAY button chooses the band");
        all->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        s->setPlay (2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);   // the window catches up (its timer)
        if (screenshotFolder != juce::File())
        {
            const auto file = screenshotFolder.getChildFile ("Triad-Split-preview.png");
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
            {
                stream.setPosition (0);
                stream.truncate();
                juce::PNGImageFormat png;
                png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f), stream);
                std::cout << "Screenshot: " << file.getFullPathName() << std::endl;
            }
        }
    }
    for (int g = 1; g <= L::numGroups; ++g)
        if (g != 3) require (L::group (g)->members.load() == 0, "Every Triad Split left its group");
    std::cout << "Triad Split: OK" << std::endl;
}

inline juce::AudioProcessorParameter* parameterNamed (juce::AudioProcessor& p, const juce::String& name)
{
    for (auto* q : p.getParameters()) if (q->getName (64) == name) return q;
    return nullptr;
}

// The installed plug-ins (macOS): the VST3 and the AU are two separate binaries, and the tests' own Triad Split a
// third; in one DAW, all of them in group A must share their bands. (Each plug-in's own timer does the linking,
// so the message loop has to run.)
inline void runHosted (const juce::File& vst3File)
{
   #if JUCE_MAC && JUCE_PLUGINHOST_AU
    juce::AudioPluginFormatManager manager;
    manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    manager.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
    juce::OwnedArray<juce::PluginDescription> vst3Types, auTypes;
    manager.getFormat (0)->findAllTypesForFile (vst3Types, vst3File.getFullPathName());
    manager.getFormat (1)->findAllTypesForFile (auTypes, "AudioUnit:Effects/aufx,TrSp,Tsrt");
    require (! vst3Types.isEmpty() && ! auTypes.isEmpty(), "The installed Triad Split VST3 and AU are found");
    juce::String error;
    auto first = manager.createPluginInstance (*vst3Types[0], 48000, 512, error);
    auto second = manager.createPluginInstance (*vst3Types[0], 48000, 512, error);
    auto au = manager.createPluginInstance (*auTypes[0], 48000, 512, error);
    require (first != nullptr && second != nullptr && au != nullptr, "Load two Triad Split VST3s and a Triad Split AU");
    require (first->getBusCount (false) == 1 + triad::maxBands && au->getBusCount (false) == 1 + triad::maxBands,
             "The DAW sees the band outputs (VST3 and AU)");
    float normalised = 0.0f;
    {
        TriadSplit reference;
        reference.setLinkGroup (0);
        normalised = reference.crossoverParameter (0)->convertTo0to1 (400.0f);
    }
    auto* crossover = parameterNamed (*first, "Crossover 1");
    require (crossover != nullptr, "The VST3's divider is a parameter");
    crossover->beginChangeGesture();
    crossover->setValueNotifyingHost (normalised);
    crossover->endChangeGesture();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    auto follows = [&] (juce::AudioProcessor& q)
    {
        auto* c = parameterNamed (q, "Crossover 1");
        return c != nullptr && std::abs (c->getValue() - normalised) < 1.0e-3f;
    };
    require (follows (*second), "Two Triad Split VST3s link: a divider moved on one moves on the other");
    require (follows (*au), "The Triad Split AU links with the VST3s (separate binaries, one DAW)");
    TriadSplit local;   // joins group A: takes its bands
    require (std::abs (local.effectiveCrossovers()[0] - 400.0f) < 0.05f && local.getLinkedCount() == 4,
             "...and with any other build of Triad Split in the same DAW");
    std::cout << "Triad Split: the installed VST3 and AU link with each other: OK" << std::endl;
   #else
    juce::ignoreUnused (vst3File);
   #endif
}
}
