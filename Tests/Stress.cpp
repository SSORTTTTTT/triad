// Triad stress test: how Triad holds up with lots of hosted effects and constant editing.
//
//   TriadStress <Test Effect.vst3> <Crash Fixture.vst3> <Triad Scanner> [report.txt] [seed]
//
//  1. A big project: 10 bands x 20 slots (200 hosted effects, VST3 + Apple's Audio Units),
//     timed against the real-time budget, saved and reopened.
//  2. Thousands of random edits (bands, slots, effects, mixes, LFO drawing, the matrix, moving
//     bands, chain presets, windows, project reloads, sample-rate changes) while a separate thread
//     plays audio through Triad with a moving song position, as a DAW does.
//  3. Many Triad instances at once, like one on every track of a project.
//  4. The plug-in list with dozens of plug-ins that crash or need checking.
// Any crash ends the program; NaN/Inf output or a broken invariant fails it.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginCatalog.h"
#include "../Source/PluginEditor.h"
#include "../Source/Generator.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#if ! JUCE_WINDOWS
 #include <unistd.h>
 #include <csignal>
 #include <cstring>
 #include <execinfo.h>
#endif

#if JUCE_MAC
std::string triadStressOpenGLRenderer();      // StressMac.mm
bool triadStressUsesOpenGL (void* nativeHandle);
#endif

namespace
{
std::ostringstream report;
juce::File reportPath;   // rewritten after every line, so a run that is cut short still leaves its results
std::recursive_mutex sayLock;
void say (const juce::String& line)
{
    const std::lock_guard<std::recursive_mutex> lock (sayLock);
    std::cout << line << std::endl;
    report << line << "\n";
    if (reportPath != juce::File()) reportPath.replaceWithText (report.str());
}
void require (bool ok, const juce::String& message)
{
    if (! ok) throw std::runtime_error (message.toStdString());
}
void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
juce::String ms (double v) { return juce::String (v, 2) + " ms"; }

// Watches the run from its own thread. If one step takes far too long (a hang, or a plug-in waiting
// on a dialog nobody can see), or the audio thread stops finishing blocks, it says where, saves the
// call stack of every thread (macOS `sample`), writes the report and ends the run with a failure.
class Watchdog
{
public:
    Watchdog() : thread ([this] { run(); }) {}
    ~Watchdog() { quit = true; thread.join(); }
    void step (const juce::String& what, double limitSeconds)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        current = what; since = now(); limit = limitSeconds; detail.clear();
    }
    // What the current step is waiting for (does not restart its clock).
    void note (const juce::String& what)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        detail = what;
    }
    void watchAudio (const std::atomic<int>* blocks, const std::atomic<bool>* paused)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        audioBlocks = blocks; audioPaused = paused; lastBlocks = -1; lastProgress = now();
    }
private:
    static double now() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    void run()
    {
        while (! quit.load())
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (250));
            juce::String problem;
            {
                const std::lock_guard<std::mutex> lock (mutex);
                if (current.isNotEmpty() && now() - since > limit)
                    problem = "stuck for " + juce::String (juce::roundToInt (now() - since)) + " s in: " + current
                            + (detail.isNotEmpty() ? " (" + detail + ")" : juce::String());
                else if (audioBlocks != nullptr)
                {
                    const int b = audioBlocks->load();
                    if (b != lastBlocks || audioPaused->load()) { lastBlocks = b; lastProgress = now(); }
                    else if (now() - lastProgress > 60.0)
                        problem = "the audio thread has not finished a block for " + juce::String (juce::roundToInt (now() - lastProgress))
                                + " s (main thread: " + current + ")";
                }
            }
            if (problem.isNotEmpty()) fail (problem);
        }
    }
    [[noreturn]] static void fail (const juce::String& problem)
    {
        say ("\nFAIL (watchdog): " + problem);
       #if JUCE_MAC
        const auto file = reportPath != juce::File() ? reportPath.getSiblingFile (reportPath.getFileNameWithoutExtension() + "-hang-sample.txt")
                                                     : juce::File::getCurrentWorkingDirectory().getChildFile ("stress-hang-sample.txt");
        const auto command = "/usr/bin/sample " + juce::String ((int) getpid()) + " 3 -mayDie -file '" + file.getFullPathName() + "' >/dev/null 2>&1";
        if (std::system (command.toRawUTF8()) != 0 || ! file.existsAsFile())
            std::system (("sudo -n " + command).toRawUTF8());
        if (file.existsAsFile())
        {
            say ("Call stacks of every thread saved to " + file.getFileName());
            // The main thread and the audio thread, in the log too.
            juce::StringArray lines;
            lines.addLines (file.loadFileAsString());
            for (const char* thread : { "com.apple.main-thread", "TriadStress audio" })
            {
                int first = -1;
                for (int i = 0; i < lines.size() && first < 0; ++i) if (lines[i].contains (thread)) first = i;
                if (first < 0) continue;
                std::cout << "---- " << thread << " ----" << std::endl;
                for (int i = first; i < juce::jmin (lines.size(), first + 250); ++i)
                {
                    if (i > first && lines[i].trimStart().startsWith ("Thread_")) break;
                    std::cout << lines[i].substring (0, 220) << std::endl;
                }
            }
        }
        else say ("Could not sample the call stacks");
       #endif
        std::cout.flush();
        std::_Exit (3);
    }
    std::mutex mutex;
    juce::String current, detail;
    double since = 0.0, limit = 0.0, lastProgress = 0.0;
    const std::atomic<int>* audioBlocks = nullptr;
    const std::atomic<bool>* audioPaused = nullptr;
    int lastBlocks = -1;
    std::atomic<bool> quit { false };
    std::thread thread;
};
Watchdog* watchdog = nullptr;
bool verbose = false;   // print every step (real plug-ins: so the log shows what led up to a problem)

// If the run crashes, the crash handler prints the step it was in and the four before it.
char stepTrail[5][400] {};
std::atomic<int> stepCount { 0 };
#if ! JUCE_WINDOWS
void writeText (const char* text) { const auto ignored = ::write (2, text, std::strlen (text)); juce::ignoreUnused (ignored); }
void onCrash (int signal)
{
    writeText ("\n*** CRASH (");
    writeText (signal == SIGSEGV ? "SIGSEGV" : signal == SIGBUS ? "SIGBUS" : signal == SIGABRT ? "SIGABRT" : signal == SIGILL ? "SIGILL" : signal == SIGFPE ? "SIGFPE" : "signal");
    writeText (") while the stress test was in:\n");
    const int n = stepCount.load();
    for (int i = juce::jmax (0, n - 5); i < n; ++i)
    {
        writeText (i == n - 1 ? "  now:    " : "  before: ");
        writeText (stepTrail[i % 5]);
        writeText ("\n");
    }
    writeText ("Call stack of the crashing thread:\n");
    void* frames[128];
    backtrace_symbols_fd (frames, backtrace (frames, 128), 2);
    std::signal (signal, SIG_DFL);
    std::raise (signal);
}
void installCrashHandler()
{
    for (int signal : { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE }) std::signal (signal, onCrash);
}
#else
void installCrashHandler() {}
#endif

// What the run is doing now, and how long that may take before the watchdog calls it a hang.
void step (const juce::String& what, double limitSeconds = 120.0)
{
    installCrashHandler();   // again: a hosted plug-in may have installed its own crash handler meanwhile
    if (what.isNotEmpty())
    {
        const int n = stepCount.load();
        what.copyToUTF8 (stepTrail[n % 5], sizeof (stepTrail[0]));
        stepCount = n + 1;
        if (verbose) std::cout << "  > " << what << std::endl;
    }
    // TRIAD_STRESS_STEP_LIMIT (seconds) shortens every limit, to check the watchdog itself.
    static const double cap = [] { const char* v = std::getenv ("TRIAD_STRESS_STEP_LIMIT"); return v != nullptr ? std::atof (v) : 0.0; }();
    if (cap > 0.0) limitSeconds = juce::jmin (limitSeconds, cap);
    if (watchdog != nullptr) watchdog->step (what, limitSeconds);
}

// A DAW transport: playing at 128 BPM in 4/4, so the LFOs follow a song position.
struct Transport final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 128.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeInSamples ((juce::int64) (ppq * 60.0 / bpm * 48000.0));
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
    void advance (int samples, double sampleRate) { if (playing) ppq += samples * bpm / 60.0 / sampleRate; }
};

// Switches on every LFO in every band (each knob gets a moving shape, at musical lengths) and
// sends each band's LFO to its volume too: the heaviest modulation load Triad can have.
void activateAllLfos (TriadProcessor& p, juce::Random& r)
{
    namespace mod = triad::mod;
    std::vector<int> musical;
    for (int i = 0; i < (int) mod::lengths().size(); ++i)
        if ((mod::lengths()[(size_t) i].column < 3 && mod::lengths()[(size_t) i].beats >= 0.25)
            || (mod::lengths()[(size_t) i].bars > 0 && mod::lengths()[(size_t) i].bars <= 4))
            musical.push_back (i);
    for (int b = 0; b < p.getBandCount(); ++b)
    {
        for (int i = 0; i < p.getLfoCount (b); ++i)
        {
            auto shape = mod::preset (1 + r.nextInt (mod::numPresets() - 1));
            if (r.nextBool()) shape = mod::repeated (shape, 2);
            p.setLfoShape (b, i, shape, false);
            p.setLfoLength (b, i, musical[(size_t) r.nextInt ((int) musical.size())]);
        }
        p.addRoute (b, p.getBandLfo (b), { TriadProcessor::Destination::bandVolume, 0 });
    }
}
int activeRoutes (const TriadProcessor& p)
{
    int n = 0;
    for (int b = 0; b < p.getBandCount(); ++b)
        for (int r = 0; r < p.getRouteCount (b); ++r) n += p.getRouteInfo (b, r).muted ? 0 : 1;
    return n;
}

struct Pool
{
    std::vector<juce::PluginDescription> effects;
    juce::StringArray names;
    std::set<juce::String> openGlWindows;   // "format|name" of effects whose windows draw with OpenGL
};
juce::String windowKey (const TriadProcessor::SlotInfo& info) { return info.format + "|" + info.name; }

// Waits for asynchronous (AUv3) loads to land.
void settle (TriadProcessor& p)
{
    for (int i = 0; i < 100; ++i)
    {
        int pending = 0;
        for (int b = 0; b < p.getBandCount(); ++b)
            for (int s = 0; s < p.getSlotCount (b); ++s) pending += p.getSlotInfo (b, s).pending ? 1 : 0;
        if (pending == 0) return;
        if (watchdog != nullptr) watchdog->note ("waiting for " + juce::String (pending) + " effects still loading, pass " + juce::String (i + 1) + " of 100");
        pump (20);
    }
}

// ---- Effect windows -------------------------------------------------------------------------------
// Triad's effect windows are titled "Band N · Slot M — Effect".
bool isEffectWindowTitle (const juce::String& title) { return title.startsWith ("Band ") && title.contains (juce::String::fromUTF8 (" \xc2\xb7 Slot ")); }
juce::TopLevelWindow* effectWindow (int band, int slot)
{
    const auto prefix = "Band " + juce::String (band + 1) + juce::String::fromUTF8 (" \xc2\xb7 Slot ") + juce::String (slot + 1) + juce::String::fromUTF8 (" \xe2\x80\x94");
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        if (auto* w = juce::TopLevelWindow::getTopLevelWindow (i); w != nullptr && w->isVisible() && w->getName().startsWith (prefix)) return w;
    return nullptr;
}
int openEffectWindows()
{
    int n = 0;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        if (auto* w = juce::TopLevelWindow::getTopLevelWindow (i); w != nullptr && w->isVisible() && isEffectWindowTitle (w->getName())) ++n;
    return n;
}
// Whether OpenGL is drawn by a graphics card here. CI Macs are virtual machines without one, where
// OpenGL plug-in windows are drawn in software, so slowly that one window can stall the test.
juce::String openGlRenderer()
{
   #if JUCE_MAC
    static const juce::String name (triadStressOpenGLRenderer());
    return name;
   #else
    return "unknown";
   #endif
}
bool hardwareGraphics()
{
    const auto r = openGlRenderer();
    return r.isNotEmpty() && ! r.containsIgnoreCase ("software") && ! r.containsIgnoreCase ("paravirtual");
}
bool usesOpenGL (juce::Component* window)
{
   #if JUCE_MAC
    if (auto* peer = window != nullptr ? window->getPeer() : nullptr) return triadStressUsesOpenGL (peer->getNativeHandle());
   #else
    juce::ignoreUnused (window);
   #endif
    return false;
}
// Effects that produced invalid audio (NaN/Inf), which Triad silenced (then restarted or bypassed).
std::map<juce::String, int> invalidEffects;
void noteInvalidAudio (const TriadProcessor& p)
{
    for (int b = 0; b < p.getBandCount(); ++b)
        for (int s = 0; s < p.getSlotCount (b); ++s)
        {
            const auto info = p.getSlotInfo (b, s);
            if (info.invalidAudio > 0)
            {
                auto& worst = invalidEffects[info.name + " (" + info.format + ")"];
                worst = juce::jmax (worst, info.invalidAudio);
            }
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

// Every effect installed on this computer (third-party VST3 and AU, and Apple's), each checked by
// loading it into Triad once; load time, latency and failures are reported.
Pool buildRealPool (const juce::File& scanner)
{
    Pool pool;
    PluginCatalog::Options o = PluginCatalog::defaultOptions();
    o.cacheFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadStressReal");
    o.cacheFolder.deleteRecursively();
    o.scanner = scanner;
    o.autoCheck = true;
    step ("listing the plug-ins installed on this computer", 900);
    PluginCatalog catalog (o);
    require (catalog.waitUntilIdle (600000), "Plug-in list of this computer");
    const auto counts = catalog.getCounts();
    say ("Installed VST3 bundles: " + juce::String (counts.bundles) + " (" + juce::String (counts.fromModuleInfo) + " read from disk, "
         + juce::String (counts.fromAudioUnit) + " via their AU, " + juce::String (counts.fromScanner) + " checked by Triad Scanner, "
         + juce::String (counts.crashed) + " crashed it, " + juce::String (counts.timedOut) + " timed out)");
    const juce::StringArray skip { "AUNetSend", "AUSoundIsolation", "AURoundTripAAC", "AUSpeechSynthesis" };
    TriadProcessor probe;
    probe.prepareToPlay (48000, 256);
    juce::StringArray rejected;
    juce::VST3PluginFormat vst3;
    const bool haveDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    if (haveDisplay && ! hardwareGraphics())
        say ("Note: this test Mac has no graphics card (OpenGL renderer: " + (openGlRenderer().isNotEmpty() ? openGlRenderer() : juce::String ("none"))
             + "), so plug-in windows that draw with OpenGL are opened and closed but not left on screen (not a problem on a real Mac).");
    for (const auto& e : catalog.getEffects())
    {
        if (! e.verified || skip.contains (e.name)) continue;
        auto description = e.description;
        if (e.format == "VST3" && e.loadByFile)
        {
            juce::OwnedArray<juce::PluginDescription> found;
            vst3.findAllTypesForFile (found, e.description.fileOrIdentifier);
            for (auto* d : found) if (d->name == e.name && ! d->isInstrument) description = *d;
        }
        const int slot = probe.getSlotCount (0);
        step ("checking " + e.name + " (" + e.format + ")");
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto error = probe.loadEffect (0, slot, description, false);
        settle (probe);
        const auto loadMs = juce::Time::getMillisecondCounterHiRes() - start;
        const auto info = probe.getSlotInfo (0, slot);
        const auto label = e.name + " (" + e.format + (e.manufacturer.isNotEmpty() ? ", " + e.manufacturer : juce::String()) + ")";
        if (error.isEmpty() && info.loaded)
        {
            // Its window: opened on top of Triad, left to draw for a moment, closed.
            juce::String window;
            if (haveDisplay)
            {
                step ("opening the window of " + label);
                const auto w0 = juce::Time::getMillisecondCounterHiRes();
                probe.showEffect (0, slot);
                auto* w = effectWindow (0, slot);
                const auto openMs = juce::Time::getMillisecondCounterHiRes() - w0;
                const bool openGl = usesOpenGL (w);
                if (openGl) pool.openGlWindows.insert (windowKey (info));
                if (w == nullptr) window = ", no window";
                else if (openGl && ! hardwareGraphics()) window = ", window opens in " + ms (openMs) + " (OpenGL: not drawn on this test Mac)";
                else
                {
                    const auto d0 = juce::Time::getMillisecondCounterHiRes();
                    pump (300);
                    const auto drawMs = juce::Time::getMillisecondCounterHiRes() - d0 - 300.0;
                    window = ", window opens in " + ms (openMs) + (openGl ? " (OpenGL)" : "")
                           + (drawMs > 200.0 ? ", drawing it held Triad up for " + ms (drawMs) : juce::String());
                }
                probe.closeWindows();
                pump (20);
            }
            pool.effects.push_back (description);
            pool.names.add (label);
            say ("  OK   " + label + ": loads in " + ms (loadMs) + ", latency " + juce::String (info.latency) + " samples" + window);
        }
        else rejected.add (label + ": " + (error.isNotEmpty() ? error : info.status));
        if (slot < probe.getSlotCount (0)) probe.removeSlot (0, slot);
    }
    for (const auto& r : rejected) say ("  NOT LOADED  " + r);
    require (! pool.effects.empty(), "At least one effect installed");
    say ("Effect pool: " + juce::String ((int) pool.effects.size()) + " effects from this computer");
    return pool;
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
void bigProject (const Pool& pool, double seconds)
{
    say ("\n== 1. Big project: 10 bands x 20 slots ==");
    constexpr double rate = 48000.0;
    constexpr int block = 256, slotsPerBand = 20;
    auto p = std::make_unique<TriadProcessor>();
    Transport transport;
    p->setPlayHead (&transport);
    p->prepareToPlay (rate, block);
    while (p->addBand()) {}
    step ("big project: loading 200 effects", 900);
    auto start = juce::Time::getMillisecondCounterHiRes();
    int index = 0;
    for (int b = 0; b < triad::maxBands; ++b)
        for (int s = 0; s < slotsPerBand; ++s)
        {
            const auto& d = pool.effects[(size_t) (index++ % (int) pool.effects.size())];
            require (p->loadEffect (b, s, d, false).isEmpty(), "Load " + d.name + " into band " + juce::String (b + 1));
        }
    settle (*p);
    pump (300);   // a DAW's message loop keeps running; plug-ins finish starting up there
    const int loaded = loadedCount (*p);
    say ("Loaded " + juce::String (loaded) + " effects in " + ms (juce::Time::getMillisecondCounterHiRes() - start)
         + "; Triad latency " + juce::String (p->getLatencySamples()) + " samples");
    require (loaded == triad::maxBands * slotsPerBand, "All 200 effects running");
    juce::Random random (1);
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (rate * seconds / block);
    const double budget = 1000.0 * block / rate;
    double plainAverage = 0.0;
    for (int pass = 0; pass < 2; ++pass)
    {
        step (pass == 0 ? "big project: playing" : "big project: playing with every LFO on", 900);
        if (pass == 1)
        {
            activateAllLfos (*p, random);
            say ("Switched on every LFO: " + juce::String (activeRoutes (*p)) + " knobs now move with the song (slot mixes, band mixes, band volumes)");
        }
        double total = 0.0, worst = 0.0;
        float peak = 0.0f;
        bool finite = true;
        for (int i = 0; i < blocks; ++i)
        {
            fillNoise (audio, random);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            p->processBlock (audio, midi);
            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
            transport.advance (block, rate);
            total += dt; worst = juce::jmax (worst, dt);
            peak = juce::jmax (peak, peakOf (audio, finite));
        }
        say (juce::String (seconds, 0) + " s of audio through 200 effects" + (pass == 1 ? " with every LFO on" : "") + ": average "
             + ms (total / blocks) + " per " + juce::String (block) + "-sample block (" + juce::String (100.0 * total / blocks / budget, 1)
             + "% of one core), worst " + ms (worst) + ", peak level " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
        require (finite, "Big project output is finite (no NaN/Inf)");
        noteInvalidAudio (*p);
        if (pass == 0) plainAverage = total / blocks;
        else say ("LFO cost: " + juce::String (100.0 * (total / blocks - plainAverage) / juce::jmax (1.0e-6, plainAverage), 1)
                  + "% more processing than the same project without LFOs");
    }
    step ("big project: saving and reopening", 900);
    start = juce::Time::getMillisecondCounterHiRes();
    juce::MemoryBlock state;
    p->getStateInformation (state);
    p->setPlayHead (nullptr);
    const auto saveTime = juce::Time::getMillisecondCounterHiRes() - start;
    start = juce::Time::getMillisecondCounterHiRes();
    TriadProcessor reopened;
    reopened.prepareToPlay (rate, block);
    reopened.setStateInformation (state.getData(), (int) state.getSize());
    settle (reopened);
    pump (300);
    say ("Saved (" + juce::String ((int) (state.getSize() / 1024)) + " KB) in " + ms (saveTime) + ", reopened in "
         + ms (juce::Time::getMillisecondCounterHiRes() - start) + " with " + juce::String (loadedCount (reopened)) + " effects running");
    require (reopened.getBandCount() == triad::maxBands && loadedCount (reopened) == loaded, "Big project reopens complete");
    require (activeRoutes (reopened) == activeRoutes (*p), "Every LFO comes back with the project");
    for (int b = 0; b < triad::maxBands; ++b) require (reopened.getSlotCount (b) == slotsPerBand, "Every band reopens with its slots");
    step ("big project: closing", 600);
    pump (100);   // a DAW's message loop runs between playing and closing
    start = juce::Time::getMillisecondCounterHiRes();
    p.reset();
    say ("Closed the 200-effect project in " + ms (juce::Time::getMillisecondCounterHiRes() - start));
}

// ---- 2. Random edits while audio plays --------------------------------------------------------------
void editsWhilePlaying (const Pool& pool, juce::int64 seed, int operations, double maxSeconds)
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
    // A DAW transport that plays (and sometimes stops), so the LFOs follow a song position.
    Transport transport;
    p.setPlayHead (&transport);
    std::thread audioThread ([&]
    {
        juce::Thread::setCurrentThreadName ("TriadStress audio");
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
            if (r.nextInt (400) == 0) transport.playing = ! transport.playing;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            if (r.nextInt (50) == 0) p.processBlockBypassed (view, midi);
            else p.processBlock (view, midi);
            if (transport.playing) transport.ppq += n * transport.bpm / 60.0 / rate.load();
            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
            if (dt > worstBlock.load()) worstBlock = dt;
            bool finite = true;
            const auto peak = peakOf (view, finite);
            if (! finite) nonFinite = true;
            if (juce::exactlyEqual (peak, 0.0f)) ++silent;
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
    if (watchdog != nullptr) watchdog->watchAudio (&blocks, &paused);

    const bool haveDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Random r (seed);
    std::map<juce::String, int> counts;
    struct Timing { double total = 0.0, worst = 0.0; int worstOp = 0; };
    std::map<juce::String, Timing> timings;
    int peakEffects = 0, failedLoads = 0, done = 0, windowsOpened = 0, windowsNotDrawn = 0;
    bool timeLimit = false;
    constexpr int maxEffects = 120;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    try
    {
        for (int op = 0; op < operations; ++op)
        {
            const double elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
            if (op > 0 && op % 100 == 0)
                std::cout << "  ... " << op << " edits in " << juce::String (elapsed, 1) << " s, " << loadedCount (p) << " effects loaded, "
                          << blocks.load() << " audio blocks" << std::endl;
            if (maxSeconds > 0.0 && elapsed > maxSeconds) { timeLimit = true; break; }
            const auto opStart = juce::Time::getMillisecondCounterHiRes();
            const int bands = p.getBandCount();
            const int band = r.nextInt (bands);
            const int slots = p.getSlotCount (band);
            const int slot = r.nextInt (slots);
            const int effects = loadedCount (p);
            int choice = r.nextInt (130);
            if (effects >= maxEffects && choice < 30) choice = 30 + r.nextInt (70);   // full: edit or remove instead
            juce::String name;
            auto begin = [&] (const char* what, const juce::String& extra = {})
            {
                name = what;
                const auto there = slot < p.getSlotCount (band) ? p.getSlotInfo (band, slot) : TriadProcessor::SlotInfo {};
                step ("edit " + juce::String (op + 1) + ": " + name + " (band " + juce::String (band + 1) + ", slot " + juce::String (slot + 1)
                      + (there.name.isNotEmpty() ? ": " + there.name + " " + there.format : juce::String()) + ")" + extra);
            };
            if (choice < 22)
            {
                const auto& d = pool.effects[(size_t) r.nextInt ((int) pool.effects.size())];
                begin ("load effect (new slot)", ", loading " + d.name + " (" + d.pluginFormatName + ")");
                if (p.loadEffect (band, slots, d, false).isNotEmpty()) ++failedLoads;
            }
            else if (choice < 30)
            {
                const auto& d = pool.effects[(size_t) r.nextInt ((int) pool.effects.size())];
                begin ("replace effect", ", with " + d.name + " (" + d.pluginFormatName + ")");
                if (p.loadEffect (band, slot, d, false).isNotEmpty()) ++failedLoads;
            }
            else if (choice < 40) { begin ("remove slot"); p.removeSlot (band, slot); }
            else if (choice < 44) { begin ("remove effect, keep slot"); p.removeEffect (band, slot); }
            else if (choice < 50) { begin ("add empty slot"); p.addSlot (band); }
            else if (choice < 56) { begin ("move slot"); p.moveSlot (band, slot, r.nextBool() ? 1 : -1); }
            else if (choice < 62) { begin ("bypass/enable slot"); p.setSlotEnabled (band, slot, r.nextBool()); }
            else if (choice < 70) { begin ("slot mix"); p.setSlotMix (band, slot, r.nextFloat() * 100.0f); }
            else if (choice < 75) { begin ("add band"); p.addBand (r.nextBool() ? -1.0f : 20.0f * std::pow (1000.0f, r.nextFloat())); }
            else if (choice < 80) { begin ("remove band"); p.removeBand (band); }
            else if (choice < 82)
            {
                begin ("crossover slope");
                p.setCrossoverSlope (r.nextInt (juce::jmax (1, bands - 1)), 1 + r.nextInt (triad::CrossoverFilter::maxOrder));
            }
            else if (choice < 84)
            {
                begin ("drag crossover");
                if (auto* c = p.crossoverParameter (r.nextInt (juce::jmax (1, bands - 1))))
                {
                    c->beginChangeGesture(); c->setValueNotifyingHost (r.nextFloat()); c->endChangeGesture();
                }
            }
            else if (choice < 88)
            {
                begin ("save and reload project");
                juce::MemoryBlock state;
                p.getStateInformation (state);
                p.setStateInformation (state.getData(), (int) state.getSize());
            }
            else if (choice < 90)
            {
                begin ("sample rate / buffer change");
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
                begin ("open/close Triad window, switch tabs");
                if (editor == nullptr)
                {
                    editor.reset (p.createEditor());
                    editor->setVisible (true);
                    if (auto* e = dynamic_cast<TriadEditor*> (editor.get())) e->refreshNow();
                }
                else if (r.nextBool())
                {
                    if (auto* e = dynamic_cast<TriadEditor*> (editor.get()))
                    {
                        e->setTab ((TriadEditor::Tab) r.nextInt (3));
                        if (e->getTab() == TriadEditor::Tab::lfo) e->showLfo (band, r.nextBool() ? -1 : slot);
                    }
                }
                else editor.reset();
            }
            else if (choice < 98)
            {
                begin ("open effect window");
                const auto info = p.getSlotInfo (band, slot);
                if (haveDisplay && info.loaded)
                {
                    // People keep a few effect windows open, not dozens.
                    if (openEffectWindows() >= 4) p.closeWindows();
                    if (! hardwareGraphics() && pool.openGlWindows.count (windowKey (info)) > 0) ++windowsNotDrawn;
                    else
                    {
                        p.showEffect (band, slot);
                        ++windowsOpened;
                        if (! hardwareGraphics() && usesOpenGL (effectWindow (band, slot))) { p.closeWindows(); ++windowsNotDrawn; }
                    }
                }
            }
            else if (choice < 100) { begin ("close effect windows"); p.closeWindows(); }
            else if (choice < 108)
            {
                begin ("draw an LFO");
                namespace mod = triad::mod;
                const int lfo = r.nextInt (juce::jmax (1, p.getLfoCount (band)));
                auto shape = p.getLfoInfo (band, lfo).shape;
                switch (r.nextInt (7))
                {
                    case 0: shape = mod::preset (r.nextInt (mod::numPresets())); break;
                    case 5:
                    {
                        // The generator, any style, any grid.
                        mod::GridInfo grid;
                        grid.columns = 8 + r.nextInt (121);
                        shape = mod::generate ({ r.nextFloat(), r.nextFloat(), r.nextFloat() }, grid, r);
                        break;
                    }
                    case 1: shape = mod::repeated (shape, 2 + r.nextInt (2)); break;
                    case 2: shape = mod::halved (shape); break;
                    case 3: shape = mod::reversed (shape); break;
                    case 4: shape = mod::flippedVertical (shape); break;
                    default:
                    {
                        mod::Shape random;
                        const int points = 2 + r.nextInt (12);
                        for (int i = 0; i < points; ++i)
                            random.push_back ({ i == 0 ? 0.0f : i == points - 1 ? 1.0f : r.nextFloat(), r.nextFloat(), (r.nextFloat() - 0.5f) * 12.0f });
                        shape = random;
                    }
                }
                p.setLfoShape (band, lfo, shape, true);
                if (r.nextInt (4) == 0) p.undoLfo (band, lfo);
            }
            else if (choice < 114)
            {
                begin ("matrix edit");
                const int routes = p.getRouteCount (band);
                const int route = routes > 0 ? r.nextInt (routes) : -1;
                const auto destinations = p.getDestinations (band);
                switch (r.nextInt (6))
                {
                    case 0: p.addRoute (band, r.nextInt (juce::jmax (1, p.getLfoCount (band))), destinations[(size_t) r.nextInt ((int) destinations.size())].first); break;
                    case 1: if (route >= 0) p.removeRoute (band, route); break;
                    case 2: if (route >= 0) p.setRouteMuted (band, route, r.nextBool()); break;
                    case 3: if (route >= 0) p.setRouteAmount (band, route, r.nextFloat()); break;
                    case 4: if (route >= 0) p.setRouteInvert (band, route, r.nextBool()); break;
                    default: if (route >= 0) p.setRouteDestination (band, route, destinations[(size_t) r.nextInt ((int) destinations.size())].first); break;
                }
            }
            else if (choice < 117)
            {
                begin ("LFO length");
                p.setLfoLength (band, r.nextInt (juce::jmax (1, p.getLfoCount (band))), r.nextInt ((int) triad::mod::lengths().size()));
            }
            else if (choice < 120)
            {
                begin ("add/remove an LFO");
                if (r.nextBool()) p.addLfo (band);
                else p.removeLfo (band, r.nextInt (juce::jmax (1, p.getLfoCount (band))));
            }
            else if (choice < 124) { begin ("move a band"); p.moveBand (band, r.nextInt (bands)); }
            else if (choice < 127)
            {
                if (r.nextBool())
                {
                    begin ("chain preset copy");
                    if (effects < maxEffects - 20)
                        if (auto chain = p.createChainXml (band)) p.loadChainXml (r.nextInt (bands), *chain);
                }
                else
                {
                    begin ("copy / paste band state");
                    if (r.nextBool() || ! TriadProcessor::hasCopiedBand()) p.copyBandState (band);
                    if (effects < maxEffects - 20) p.pasteBandState (r.nextInt (bands));
                }
            }
            else
            {
                begin ("band volume");
                if (auto* gain = p.parameters.getParameter (TriadProcessor::bandGainID (band))) gain->setValueNotifyingHost (r.nextFloat());
            }
            ++counts[name];
            step ("edit " + juce::String (op + 1) + ": " + name + ", then waiting for loads and the windows");
            settle (p);
            pump (r.nextInt (3));
            if (auto* e = dynamic_cast<TriadEditor*> (editor.get())) e->refreshNow();
            const double took = juce::Time::getMillisecondCounterHiRes() - opStart;
            auto& t = timings[name];
            t.total += took;
            if (took > t.worst) { t.worst = took; t.worstOp = op + 1; }
            done = op + 1;
            checkInvariants (p, name + " (operation " + juce::String (op + 1) + ")");
            noteInvalidAudio (p);
            require (! nonFinite.load(), "Audio stayed finite (failed after " + name + ", operation " + juce::String (op + 1) + ")");
            peakEffects = juce::jmax (peakEffects, loadedCount (p));
        }
    }
    catch (...)
    {
        stop = true; resumeAudio(); audioThread.join();
        if (watchdog != nullptr) watchdog->watchAudio (nullptr, nullptr);
        p.setPlayHead (nullptr);
        throw;
    }
    step ("edits: playing on with the final layout, then closing", 300);
    if (timeLimit)
        say ("Time limit (" + juce::String (juce::roundToInt (maxSeconds / 60.0)) + " min) reached after " + juce::String (done) + " edits; the section stops there");
    // Let it play on a little with the final layout.
    pump (500);
    stop = true;
    resumeAudio();
    audioThread.join();
    if (watchdog != nullptr) watchdog->watchAudio (nullptr, nullptr);
    p.setPlayHead (nullptr);
    editor.reset();
    p.closeWindows();
    require (! nonFinite.load(), "Audio stayed finite");
    say (juce::String (done) + " edits in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) + " s: "
         + juce::String (blocks.load()) + " audio blocks processed alongside the edits (" + juce::String (automation.load())
         + " automation moves), worst block " + ms (worstBlock.load()) + ", up to " + juce::String (peakEffects) + " effects loaded at once");
    if (windowsOpened + windowsNotDrawn > 0)
        say ("Effect windows opened on top of Triad: " + juce::String (windowsOpened)
             + (windowsNotDrawn > 0 ? " (" + juce::String (windowsNotDrawn) + " OpenGL windows skipped: no graphics card on this test Mac)" : juce::String()));
    say ("Final layout: " + juce::String (p.getBandCount()) + (p.getBandCount() == 1 ? " band, " : " bands, ") + juce::String (loadedCount (p)) + " effects; NaN/Inf output: none");
    juce::StringArray lines;
    for (auto& [k, v] : counts) lines.add (k + " x" + juce::String (v));
    say ("Edits: " + lines.joinIntoString (", ") + (failedLoads > 0 ? "; loads refused " + juce::String (failedLoads) : juce::String()));
    // The slowest kinds of edit (each including the wait for loads and windows that follows it).
    std::vector<std::pair<juce::String, Timing>> slowest (timings.begin(), timings.end());
    std::sort (slowest.begin(), slowest.end(), [] (const auto& a, const auto& b) { return a.second.worst > b.second.worst; });
    juce::StringArray slow;
    for (size_t i = 0; i < juce::jmin<size_t> (5, slowest.size()); ++i)
        slow.add (slowest[i].first + " avg " + ms (slowest[i].second.total / juce::jmax (1, counts[slowest[i].first]))
                  + ", worst " + ms (slowest[i].second.worst) + " (edit " + juce::String (slowest[i].second.worstOp) + ")");
    say ("Slowest edits: " + slow.joinIntoString ("; "));
}

// ---- 3. Many instances ------------------------------------------------------------------------------
void manyInstances (const Pool& pool)
{
    say ("\n== 3. Many Triad instances (one per track), every LFO on ==");
    Transport transport;
    juce::Random lfoRandom (99);
    constexpr int instances = 24, slotsPerBand = 4;
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    std::vector<std::unique_ptr<TriadProcessor>> triads;
    step ("many instances: creating 24 Triads", 900);
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
        pump (50);
        activateAllLfos (p, lfoRandom);
        p.setPlayHead (&transport);
    }
    int total = 0;
    for (auto& t : triads) total += loadedCount (*t);
    say (juce::String (instances) + " instances with " + juce::String (total) + " hosted effects created in "
         + ms (juce::Time::getMillisecondCounterHiRes() - start));
    juce::Random random (3);
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (rate * 10.0 / block);
    step ("many instances: playing", 900);
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
        transport.advance (block, rate);
        const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
        totalTime += dt; worst = juce::jmax (worst, dt);
    }
    const double budget = 1000.0 * block / rate;
    say ("10 s of audio through all of them: average " + ms (totalTime / blocks) + " per block for the whole project ("
         + juce::String (100.0 * totalTime / blocks / budget, 1) + "% of one core's real-time budget), worst " + ms (worst));
    require (finite, "Many-instance output is finite");
    for (auto& t : triads) noteInvalidAudio (*t);
    step ("many instances: closing, then 40 open/close cycles", 900);
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
        pump (30);
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
    step ("plug-in list with crashing plug-ins", 900);
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
    Watchdog guard;
    watchdog = &guard;
    installCrashHandler();
    juce::File reportFile;
    try
    {
        require (argc >= 4, "Usage: TriadStress <Test Effect.vst3> <Crash Fixture.vst3> <Triad Scanner> [report.txt] [seed] [--real]");
        const juce::File effect (juce::String::fromUTF8 (argv[1]));
        const juce::File crashFixture (juce::String::fromUTF8 (argv[2]));
        const juce::File scanner (juce::String::fromUTF8 (argv[3]));
        // --real: use every effect installed on this computer instead of the test effect + Apple's AUs.
        bool real = false;
        juce::int64 seed = 20261015;
        for (int i = 4; i < argc; ++i)
        {
            const auto arg = juce::String::fromUTF8 (argv[i]);
            if (arg == "--real") real = true;
            else if (arg.containsOnly ("0123456789")) seed = arg.getLargeIntValue();
            else reportFile = juce::File (arg);
        }
        reportPath = reportFile;
        verbose = real;
        say ("Triad stress test" + juce::String (real ? " with the plug-ins installed on this computer" : "") + ", Triad " TRIAD_VERSION " ("
             + juce::SystemStats::getOperatingSystemName() + ", " + juce::SystemStats::getCpuModel() + ", "
             + juce::String (juce::SystemStats::getNumCpus()) + " cores)");
        const auto pool = real ? buildRealPool (scanner) : buildPool (effect, scanner);
        bigProject (pool, real ? 10.0 : 20.0);
        // With real plug-ins: five different random sequences, each given up to 8 minutes.
        if (real)
            for (int i = 0; i < 5; ++i) editsWhilePlaying (pool, seed + i, 3000, 480.0);
        else
            editsWhilePlaying (pool, seed, 3000, 0.0);
        manyInstances (pool);
        pluginList (effect, crashFixture, scanner);
        step ({}, 0);
        if (! invalidEffects.empty())
        {
            juce::StringArray list;
            for (auto& [effect, blocks] : invalidEffects) list.add (effect + ": " + juce::String (blocks) + (blocks == 1 ? " block" : " blocks"));
            say ("\nHosted effects that produced invalid audio (NaN/Inf), which Triad silenced and then restarted or bypassed: "
                 + list.joinIntoString ("; "));
        }
        say ("\nPASS: no crashes, no NaN/Inf output, all checks held");
        watchdog = nullptr;
        return 0;
    }
    catch (const std::exception& e)
    {
        step ({}, 0);
        say (juce::String ("\nFAIL: ") + e.what());
        watchdog = nullptr;
        return 1;
    }
}
