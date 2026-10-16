// Triad stress test: how Triad holds up with lots of hosted effects and constant editing.
//
//   TriadStress <Test Effect.vst3> <Crash Fixture.vst3> <Triad Scanner> [report.txt] [seed]
//
//  1. A big project: 10 bands x 20 slots (200 hosted effects, VST3 + Apple's Audio Units),
//     timed against the real-time budget, saved and reopened.
//  2. Thousands of random edits (bands, slots, effects, mixes, windows, project reloads,
//     sample-rate changes) while a separate thread plays audio through Triad, as a DAW does.
//  3. Many Triad instances at once, like one on every track of a project.
//  4. The plug-in list with dozens of plug-ins that crash or need checking.
// Any crash ends the program; NaN/Inf output or a broken invariant fails it.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginCatalog.h"
#include "../Source/PluginEditor.h"
#include <cmath>
#include <map>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
std::ostringstream report;
void say (const juce::String& line)
{
    std::cout << line << std::endl;
    report << line << "\n";
}
void require (bool ok, const juce::String& message)
{
    if (! ok) throw std::runtime_error (message.toStdString());
}
void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
juce::String ms (double v) { return juce::String (v, 2) + " ms"; }

struct Pool
{
    std::vector<juce::PluginDescription> effects;
    juce::StringArray names;
};

// Waits for asynchronous (AUv3) loads to land.
void settle (TriadProcessor& p)
{
    for (int i = 0; i < 100; ++i)
    {
        bool pending = false;
        for (int b = 0; b < p.getBandCount(); ++b)
            for (int s = 0; s < p.getSlotCount (b); ++s) pending |= p.getSlotInfo (b, s).pending;
        if (! pending) return;
        pump (20);
    }
}
int loadedCount (const TriadProcessor& p)
{
    int n = 0;
    for (int b = 0; b < p.getBandCount(); ++b)
        for (int s = 0; s < p.getSlotCount (b); ++s) n += p.getSlotInfo (b, s).loaded ? 1 : 0;
    return n;
}
void checkInvariants (const TriadProcessor& p, const juce::String& where)
{
    const int bands = p.getBandCount();
    require (bands >= 1 && bands <= triad::maxBands, "Band count in range after " + where);
    for (int b = 0; b < bands; ++b) require (p.getSlotCount (b) >= 1, "Every band keeps a slot after " + where);
    require (p.getLatencySamples() >= 0, "Latency is valid after " + where);
    const auto cuts = p.effectiveCrossovers();
    require ((int) cuts.size() == bands - 1, "One crossover between each pair of bands after " + where);
    for (size_t i = 1; i < cuts.size(); ++i) require (cuts[i] > cuts[i - 1], "Crossovers ascending after " + where);
}
void fillNoise (juce::AudioBuffer<float>& b, juce::Random& r, float level = 0.3f)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int s = 0; s < b.getNumSamples(); ++s) b.setSample (c, s, (r.nextFloat() * 2.0f - 1.0f) * level);
}
float peakOf (const juce::AudioBuffer<float>& b, bool& finite)
{
    float peak = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int s = 0; s < b.getNumSamples(); ++s)
        {
            const float v = b.getSample (c, s);
            if (! std::isfinite (v)) finite = false;
            else peak = juce::jmax (peak, std::abs (v));
        }
    return peak;
}

Pool buildPool (const juce::File& effectFile, const juce::File& scanner)
{
    Pool pool;
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> found;
    vst3.findAllTypesForFile (found, effectFile.getFullPathName());
    require (! found.isEmpty(), "Test effect found");
    pool.effects.push_back (*found[0]);
    pool.names.add (found[0]->name + " (VST3)");
   #if JUCE_MAC
    PluginCatalog::Options o;
    o.cacheFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadStressAU");
    o.scanner = scanner;
    PluginCatalog catalog (o);
    require (catalog.waitUntilIdle (120000), "Audio Unit list");
    const juce::StringArray skip { "AUNetSend", "AUSoundIsolation", "AURoundTripAAC", "AUSpeechSynthesis" };
    TriadProcessor probe;
    probe.prepareToPlay (48000, 256);
    juce::StringArray rejected;
    for (const auto& e : catalog.getEffects())
    {
        if (e.format != "AU" || ! e.manufacturer.containsIgnoreCase ("Apple") || skip.contains (e.name)) continue;
        const int slot = probe.getSlotCount (0);
        const auto error = probe.loadEffect (0, slot, e.description, false);
        settle (probe);
        if (error.isEmpty() && probe.getSlotInfo (0, slot).loaded)
        {
            pool.effects.push_back (e.description);
            pool.names.add (e.name + " (AU)");
        }
        else rejected.add (e.name + ": " + (error.isNotEmpty() ? error : probe.getSlotInfo (0, slot).status));
        if (slot < probe.getSlotCount (0)) probe.removeSlot (0, slot);
    }
    if (! rejected.isEmpty()) say ("  Apple AUs Triad declined (not stereo in/out, etc.): " + rejected.joinIntoString ("; "));
   #else
    juce::ignoreUnused (scanner);
   #endif
    say ("Effect pool: " + juce::String ((int) pool.effects.size()) + " different effects: " + pool.names.joinIntoString (", "));
    return pool;
}

// ---- 1. Big project ------------------------------------------------------------------------------
void bigProject (const Pool& pool)
{
    say ("\n== 1. Big project: 10 bands x 20 slots ==");
    constexpr double rate = 48000.0;
    constexpr int block = 256, slotsPerBand = 20;
    auto p = std::make_unique<TriadProcessor>();
    p->prepareToPlay (rate, block);
    while (p->addBand()) {}
    auto start = juce::Time::getMillisecondCounterHiRes();
    int index = 0;
    for (int b = 0; b < triad::maxBands; ++b)
        for (int s = 0; s < slotsPerBand; ++s)
        {
            const auto& d = pool.effects[(size_t) (index++ % (int) pool.effects.size())];
            require (p->loadEffect (b, s, d, false).isEmpty(), "Load " + d.name + " into band " + juce::String (b + 1));
        }
    settle (*p);
    const int loaded = loadedCount (*p);
    say ("Loaded " + juce::String (loaded) + " effects in " + ms (juce::Time::getMillisecondCounterHiRes() - start)
         + "; Triad latency " + juce::String (p->getLatencySamples()) + " samples");
    require (loaded == triad::maxBands * slotsPerBand, "All 200 effects running");
    juce::Random random (1);
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (rate * 20.0 / block);   // 20 seconds of audio
    double total = 0.0, worst = 0.0;
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < blocks; ++i)
    {
        fillNoise (audio, random);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        p->processBlock (audio, midi);
        const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
        total += dt; worst = juce::jmax (worst, dt);
        peak = juce::jmax (peak, peakOf (audio, finite));
    }
    const double budget = 1000.0 * block / rate;
    say ("20 s of audio through 200 effects: average " + ms (total / blocks) + " per " + juce::String (block) + "-sample block ("
         + juce::String (100.0 * total / blocks / budget, 1) + "% of real time), worst " + ms (worst) + ", peak level "
         + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
    require (finite, "Big project output is finite (no NaN/Inf)");
    start = juce::Time::getMillisecondCounterHiRes();
    juce::MemoryBlock state;
    p->getStateInformation (state);
    const auto saveTime = juce::Time::getMillisecondCounterHiRes() - start;
    start = juce::Time::getMillisecondCounterHiRes();
    TriadProcessor reopened;
    reopened.prepareToPlay (rate, block);
    reopened.setStateInformation (state.getData(), (int) state.getSize());
    settle (reopened);
    say ("Saved (" + juce::String ((int) (state.getSize() / 1024)) + " KB) in " + ms (saveTime) + ", reopened in "
         + ms (juce::Time::getMillisecondCounterHiRes() - start) + " with " + juce::String (loadedCount (reopened)) + " effects running");
    require (reopened.getBandCount() == triad::maxBands && loadedCount (reopened) == loaded, "Big project reopens complete");
    for (int b = 0; b < triad::maxBands; ++b) require (reopened.getSlotCount (b) == slotsPerBand, "Every band reopens with its slots");
    start = juce::Time::getMillisecondCounterHiRes();
    p.reset();
    say ("Closed the 200-effect project in " + ms (juce::Time::getMillisecondCounterHiRes() - start));
}

// ---- 2. Random edits while audio plays --------------------------------------------------------------
void editsWhilePlaying (const Pool& pool, juce::int64 seed, int operations)
{
    say ("\n== 2. " + juce::String (operations) + " random edits while audio plays on another thread (seed " + juce::String (seed) + ") ==");
    TriadProcessor p;
    std::atomic<double> rate { 48000.0 };
    std::atomic<int> block { 256 };
    p.prepareToPlay (rate, block);
    std::atomic<bool> stop { false }, pauseRequest { false }, paused { false };
    std::atomic<bool> nonFinite { false };
    std::atomic<int> blocks { 0 }, silent { 0 }, automation { 0 };
    std::atomic<double> worstBlock { 0.0 };
    std::thread audioThread ([&]
    {
        juce::Random r (seed + 1);
        juce::AudioBuffer<float> audio (2, 4096);
        juce::MidiBuffer midi;
        while (! stop.load())
        {
            if (pauseRequest.load()) { paused = true; std::this_thread::sleep_for (std::chrono::microseconds (200)); continue; }
            paused = false;
            // Hosts may pass any size up to the prepared one; occasionally a larger one too.
            const int n = r.nextInt (10) == 0 ? 1 + r.nextInt (4096) : 1 + r.nextInt (block.load());
            juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, n);
            fillNoise (view, r);
            // Host automation arrives on the audio thread.
            if (r.nextInt (8) == 0)
            {
                auto& params = p.getParameters();
                params[r.nextInt (params.size())]->setValue (r.nextFloat());
                ++automation;
            }
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            if (r.nextInt (50) == 0) p.processBlockBypassed (view, midi);
            else p.processBlock (view, midi);
            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
            if (dt > worstBlock.load()) worstBlock = dt;
            bool finite = true;
            const auto peak = peakOf (view, finite);
            if (! finite) nonFinite = true;
            if (peak == 0.0f) ++silent;
            ++blocks;
            std::this_thread::sleep_for (std::chrono::microseconds (500));
        }
    });
    auto pauseAudio = [&]
    {
        pauseRequest = true;
        while (! paused.load()) std::this_thread::sleep_for (std::chrono::microseconds (100));
    };
    auto resumeAudio = [&] { pauseRequest = false; };

    const bool haveDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Random r (seed);
    std::map<juce::String, int> counts;
    int peakEffects = 0, failedLoads = 0;
    constexpr int maxEffects = 120;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    try
    {
        for (int op = 0; op < operations; ++op)
        {
            const int bands = p.getBandCount();
            const int band = r.nextInt (bands);
            const int slots = p.getSlotCount (band);
            const int slot = r.nextInt (slots);
            const int effects = loadedCount (p);
            int choice = r.nextInt (100);
            if (effects >= maxEffects && choice < 30) choice = 30 + r.nextInt (70);   // full: edit or remove instead
            juce::String name;
            if (choice < 22)
            {
                name = "load effect (new slot)";
                const auto& d = pool.effects[(size_t) r.nextInt ((int) pool.effects.size())];
                if (p.loadEffect (band, slots, d, false).isNotEmpty()) ++failedLoads;
            }
            else if (choice < 30)
            {
                name = "replace effect";
                const auto& d = pool.effects[(size_t) r.nextInt ((int) pool.effects.size())];
                if (p.loadEffect (band, slot, d, false).isNotEmpty()) ++failedLoads;
            }
            else if (choice < 40) { name = "remove slot"; p.removeSlot (band, slot); }
            else if (choice < 44) { name = "remove effect, keep slot"; p.removeEffect (band, slot); }
            else if (choice < 50) { name = "add empty slot"; p.addSlot (band); }
            else if (choice < 56) { name = "move slot"; p.moveSlot (band, slot, r.nextBool() ? 1 : -1); }
            else if (choice < 62) { name = "bypass/enable slot"; p.setSlotEnabled (band, slot, r.nextBool()); }
            else if (choice < 70) { name = "slot mix"; p.setSlotMix (band, slot, r.nextFloat() * 100.0f); }
            else if (choice < 75) { name = "add band"; p.addBand (r.nextBool() ? -1.0f : 20.0f * std::pow (1000.0f, r.nextFloat())); }
            else if (choice < 80) { name = "remove band"; p.removeBand (band); }
            else if (choice < 84)
            {
                name = "drag crossover";
                if (auto* c = p.crossoverParameter (r.nextInt (juce::jmax (1, bands - 1))))
                {
                    c->beginChangeGesture(); c->setValueNotifyingHost (r.nextFloat()); c->endChangeGesture();
                }
            }
            else if (choice < 88)
            {
                name = "save and reload project";
                juce::MemoryBlock state;
                p.getStateInformation (state);
                p.setStateInformation (state.getData(), (int) state.getSize());
            }
            else if (choice < 90)
            {
                name = "sample rate / buffer change";
                pauseAudio();   // hosts stop processing around prepareToPlay
                p.releaseResources();
                const double rates[] { 44100.0, 48000.0, 88200.0, 96000.0 };
                const int sizes[] { 32, 64, 128, 256, 512, 1024 };
                rate = rates[r.nextInt (4)];
                block = sizes[r.nextInt (6)];
                p.prepareToPlay (rate, block);
                resumeAudio();
            }
            else if (choice < 94)
            {
                name = "open/close Triad window";
                if (! haveDisplay) name = "open/close Triad window (no display: skipped)";
                else if (editor == nullptr)
                {
                    editor.reset (p.createEditor());
                    editor->setVisible (true);
                    if (auto* e = dynamic_cast<TriadEditor*> (editor.get())) e->refreshNow();
                }
                else editor.reset();
            }
            else if (choice < 98)
            {
                name = "open effect window";
                if (haveDisplay && p.getSlotInfo (band, slot).loaded) p.showEffect (band, slot);
            }
            else { name = "close effect windows"; p.closeWindows(); }
            ++counts[name];
            settle (p);
            pump (r.nextInt (3));
            if (auto* e = dynamic_cast<TriadEditor*> (editor.get())) e->refreshNow();
            checkInvariants (p, name + " (operation " + juce::String (op + 1) + ")");
            require (! nonFinite.load(), "Audio stayed finite (failed after " + name + ", operation " + juce::String (op + 1) + ")");
            peakEffects = juce::jmax (peakEffects, loadedCount (p));
        }
    }
    catch (...)
    {
        stop = true; resumeAudio(); audioThread.join();
        throw;
    }
    // Let it play on a little with the final layout.
    pump (500);
    stop = true;
    resumeAudio();
    audioThread.join();
    editor.reset();
    p.closeWindows();
    require (! nonFinite.load(), "Audio stayed finite");
    say ("Finished in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) + " s: "
         + juce::String (blocks.load()) + " audio blocks processed alongside the edits (" + juce::String (automation.load())
         + " automation moves), worst block " + ms (worstBlock.load()) + ", up to " + juce::String (peakEffects) + " effects loaded at once");
    say ("Final layout: " + juce::String (p.getBandCount()) + (p.getBandCount() == 1 ? " band, " : " bands, ") + juce::String (loadedCount (p)) + " effects; NaN/Inf output: none");
    juce::StringArray lines;
    for (auto& [k, v] : counts) lines.add (k + " x" + juce::String (v));
    say ("Edits: " + lines.joinIntoString (", ") + (failedLoads > 0 ? "; loads refused " + juce::String (failedLoads) : juce::String()));
}

// ---- 3. Many instances ------------------------------------------------------------------------------
void manyInstances (const Pool& pool)
{
    say ("\n== 3. Many Triad instances (one per track) ==");
    constexpr int instances = 24, slotsPerBand = 4;
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    std::vector<std::unique_ptr<TriadProcessor>> triads;
    auto start = juce::Time::getMillisecondCounterHiRes();
    int index = 0;
    for (int i = 0; i < instances; ++i)
    {
        triads.push_back (std::make_unique<TriadProcessor>());
        auto& p = *triads.back();
        p.prepareToPlay (rate, block);
        for (int b = 0; b < 3; ++b)
            for (int s = 0; s < slotsPerBand; ++s)
                require (p.loadEffect (b, s, pool.effects[(size_t) (index++ % (int) pool.effects.size())], false).isEmpty(), "Load into instance");
        settle (p);
    }
    int total = 0;
    for (auto& t : triads) total += loadedCount (*t);
    say (juce::String (instances) + " instances with " + juce::String (total) + " hosted effects created in "
         + ms (juce::Time::getMillisecondCounterHiRes() - start));
    juce::Random random (3);
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (rate * 10.0 / block);
    double totalTime = 0.0, worst = 0.0;
    bool finite = true;
    for (int i = 0; i < blocks; ++i)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (auto& t : triads)
        {
            fillNoise (audio, random);
            t->processBlock (audio, midi);
            peakOf (audio, finite);
        }
        const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
        totalTime += dt; worst = juce::jmax (worst, dt);
    }
    const double budget = 1000.0 * block / rate;
    say ("10 s of audio through all of them: average " + ms (totalTime / blocks) + " per block for the whole project ("
         + juce::String (100.0 * totalTime / blocks / budget, 1) + "% of one core's real-time budget), worst " + ms (worst));
    require (finite, "Many-instance output is finite");
    start = juce::Time::getMillisecondCounterHiRes();
    triads.clear();
    say ("All instances closed in " + ms (juce::Time::getMillisecondCounterHiRes() - start));
    // Quick open/close churn, like a DAW loading and unloading a project repeatedly.
    start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 40; ++i)
    {
        TriadProcessor p;
        p.prepareToPlay (44100, 256);
        for (int s = 0; s < 3; ++s) p.loadEffect (i % 3, s, pool.effects[(size_t) ((i + s) % (int) pool.effects.size())], false);
        settle (p);
        juce::AudioBuffer<float> a (2, 256);
        fillNoise (a, random);
        p.processBlock (a, midi);
    }
    say ("40 create/play/destroy cycles in " + ms (juce::Time::getMillisecondCounterHiRes() - start));
}

// ---- 4. Plug-in list with crashing plug-ins -------------------------------------------------------
void pluginList (const juce::File& effect, const juce::File& crashFixture, const juce::File& scanner)
{
    say ("\n== 4. Plug-in list: 60 plug-ins, 20 of which crash when loaded ==");
    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadStressList");
    temp.deleteRecursively();
    auto folder = temp.getChildFile ("VST3");
    auto copy = [] (const juce::File& source, const juce::File& dest, bool dropModuleInfo)
    {
        dest.getParentDirectory().createDirectory();
        require (source.copyDirectoryTo (dest), "Copy fixture");
       #if JUCE_LINUX || JUCE_BSD
        for (auto& arch : dest.getChildFile ("Contents").findChildFiles (juce::File::findDirectories, false, "*-linux"))
            for (auto& so : arch.findChildFiles (juce::File::findFiles, false, "*.so"))
                so.moveFileTo (arch.getChildFile (dest.getFileNameWithoutExtension() + ".so"));
       #endif
        if (dropModuleInfo)
            for (auto name : { "Contents/Resources/moduleinfo.json", "Contents/moduleinfo.json" }) dest.getChildFile (name).deleteFile();
    };
    for (int i = 0; i < 20; ++i)
    {
        copy (effect, folder.getChildFile ("Listed " + juce::String (i) + ".vst3"), false);
        copy (effect, folder.getChildFile ("Unlisted/Effect " + juce::String (i) + ".vst3"), true);
        copy (crashFixture, folder.getChildFile ("Broken/Crash " + juce::String (i) + ".vst3"), false);
    }
    PluginCatalog::Options o;
    o.vst3Folders.add (folder);
    o.cacheFolder = temp.getChildFile ("cache");
    o.scanner = scanner;
    o.includeAudioUnits = false;
    auto start = juce::Time::getMillisecondCounterHiRes();
    PluginCatalog::Counts c;
    {
        PluginCatalog catalog (o);
        require (catalog.waitUntilIdle (600000), "List finishes");
        c = catalog.getCounts();
    }
    say ("First scan: " + juce::String (c.bundles) + " plug-ins in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, 1)
         + " s: " + juce::String (c.fromModuleInfo) + " read from disk, " + juce::String (c.fromScanner) + " checked by Triad Scanner, "
         + juce::String (c.crashed) + " crashed the scanner (Triad unaffected)");
    require (c.bundles == 60 && c.fromModuleInfo == 20 && c.fromScanner == 20 && c.crashed == 20, "Every plug-in classified");
    start = juce::Time::getMillisecondCounterHiRes();
    {
        PluginCatalog catalog (o);
        require (catalog.waitUntilIdle (60000), "Cached list finishes");
        require (catalog.getCounts().crashed == 20, "Crashes remembered");
    }
    say ("Next session (from the cache): " + ms (juce::Time::getMillisecondCounterHiRes() - start));
    // Closing Triad in the middle of a scan stops the helper straight away.
    start = juce::Time::getMillisecondCounterHiRes();
    {
        PluginCatalog catalog (o);
        catalog.rescanAll();
        pump (300);
    }
    say ("Closed during a full rescan in " + ms (juce::Time::getMillisecondCounterHiRes() - start));
    temp.deleteRecursively();
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File reportFile;
    try
    {
        require (argc >= 4, "Usage: TriadStress <Test Effect.vst3> <Crash Fixture.vst3> <Triad Scanner> [report.txt] [seed]");
        const juce::File effect (juce::String::fromUTF8 (argv[1]));
        const juce::File crashFixture (juce::String::fromUTF8 (argv[2]));
        const juce::File scanner (juce::String::fromUTF8 (argv[3]));
        if (argc >= 5) reportFile = juce::File (juce::String::fromUTF8 (argv[4]));
        const juce::int64 seed = argc >= 6 ? juce::String (argv[5]).getLargeIntValue() : 20261015;
        say ("Triad stress test, Triad " TRIAD_VERSION " (" + juce::SystemStats::getOperatingSystemName() + ", "
             + juce::SystemStats::getCpuModel() + ", " + juce::String (juce::SystemStats::getNumCpus()) + " cores)");
        const auto pool = buildPool (effect, scanner);
        bigProject (pool);
        editsWhilePlaying (pool, seed, 3000);
        manyInstances (pool);
        pluginList (effect, crashFixture, scanner);
        say ("\nPASS: no crashes, no NaN/Inf output, all checks held");
        if (reportFile != juce::File()) reportFile.replaceWithText (report.str());
        return 0;
    }
    catch (const std::exception& e)
    {
        say (juce::String ("\nFAIL: ") + e.what());
        if (reportFile != juce::File()) reportFile.replaceWithText (report.str());
        return 1;
    }
}
