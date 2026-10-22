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
#include "../Source/TriadTime.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
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
 #include <mach/mach.h>
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
// The stress process's memory in use now (resident), in MB.
int memoryMB()
{
   #if JUCE_MAC
    mach_task_basic_info info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) == KERN_SUCCESS)
        return (int) (info.resident_size / (1024 * 1024));
    return 0;
   #elif JUCE_LINUX
    long pages = 0, resident = 0;
    if (auto* f = std::fopen ("/proc/self/statm", "r")) { if (std::fscanf (f, "%ld %ld", &pages, &resident) != 2) resident = 0; std::fclose (f); }
    return (int) (resident * sysconf (_SC_PAGESIZE) / (1024 * 1024));
   #else
    return 0;
   #endif
}

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
    static char deletions[2600];
    TriadProcessor::describeRecentDeletions (deletions, sizeof (deletions));
    writeText ("Effects and windows Triad deleted just before (newest first):\n");
    writeText (deletions);
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
    // Triad's built-in effect too (the installed Triad Time plug-in, when there is one, is listed above as any other).
    pool.effects.push_back (TriadTime::makeDescription());
    pool.names.add ("Triad Time (built in)");
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
    pool.effects.push_back (TriadTime::makeDescription());   // Triad's built-in effect
    pool.names.add ("Triad Time (built in)");
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
    {
        // Undo with 200 effects: each snapshot asks every effect for its settings.
        step ("big project: undo / redo", 900);
        auto now = [] { return juce::Time::getMillisecondCounterHiRes(); };
        reopened.commitHistory();
        reopened.setSlotMix (3, 5, 37.0f);
        auto t = now();
        reopened.commitHistory();
        const double snapshot = now() - t;
        auto* kept = reopened.getHostedEffect (6, 0);
        reopened.removeSlot (4, 2);
        reopened.commitHistory();
        t = now();
        require (reopened.undo(), "Undo with 200 effects");
        settle (reopened);
        const double undoRemoval = now() - t;
        require (reopened.getSlotCount (4) == slotsPerBand && loadedCount (reopened) == loaded && reopened.getHostedEffect (6, 0) == kept,
                 "Undo brings the removed effect back; the other 199 stay loaded");
        t = now();
        require (reopened.undo() && std::abs (reopened.getSlotMix (3, 5) - 37.0f) > 0.01f, "Undo a slot mix with 200 effects");
        const double undoMix = now() - t;
        t = now();
        require (reopened.redo() && reopened.redo() && reopened.getSlotCount (4) == slotsPerBand - 1, "Redo with 200 effects");
        const double redoBoth = now() - t;
        noteInvalidAudio (reopened);
        say ("Undo with 200 effects: a snapshot takes " + ms (snapshot) + "; undoing a removed effect " + ms (undoRemoval)
             + " (it is loaded again, the others stay as they are); undoing a slot mix " + ms (undoMix) + "; redoing both " + ms (redoBoth));
    }
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
    int peakWaiting = 0, peakLate = 0, peakMemory = 0;   // removed effects waiting to be deleted (all; 3 s late); memory (MB)
    std::vector<juce::File> userShapes;   // LFO shapes saved to USER by the palette edits (deleted at the end)
    auto removeUserShapes = [&userShapes] { for (const auto& f : userShapes) triad::userShapes::remove (f); userShapes.clear(); };
    try
    {
        for (int op = 0; op < operations; ++op)
        {
            const double elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
            if (op > 0 && op % 100 == 0)
                std::cout << "  ... " << op << " edits in " << juce::String (elapsed, 1) << " s, " << loadedCount (p) << " effects loaded (" << TriadProcessor::getRetiredEffectCount() << " removed, waiting to be deleted), "
                          << blocks.load() << " audio blocks, " << memoryMB() << " MB in use" << std::endl;
            if (maxSeconds > 0.0 && elapsed > maxSeconds) { timeLimit = true; break; }
            peakWaiting = juce::jmax (peakWaiting, TriadProcessor::getRetiredEffectCount());
            peakLate = juce::jmax (peakLate, TriadProcessor::getLateRetiredEffectCount());
            peakMemory = juce::jmax (peakMemory, memoryMB());
            const auto opStart = juce::Time::getMillisecondCounterHiRes();
            const int bands = p.getBandCount();
            // One edit in eight is on the PRE or POST chain (every slot, LFO and link function takes them as bands).
            const int band = r.nextInt (8) == 0 ? (r.nextBool() ? TriadProcessor::preBand : TriadProcessor::postBand) : r.nextInt (bands);
            const int slots = p.getSlotCount (band);
            const int slot = r.nextInt (slots);
            const int effects = loadedCount (p);
            int choice = r.nextInt (158);
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
                begin ("LFO length / trigger");
                p.setLfoLength (band, r.nextInt (juce::jmax (1, p.getLfoCount (band))), r.nextInt ((int) triad::mod::lengths().size()));
                // TRIGGER (0.14.0): SONG / AUDIO now and then (its own random numbers, so the edits after it stay the same).
                juce::Random t ((juce::int64) op * 7919 + 13);
                if (t.nextInt (3) == 0)
                {
                    TriadProcessor::TriggerSettings trigger;
                    trigger.audio = t.nextBool();
                    trigger.listenFull = t.nextBool();
                    trigger.afterPause = t.nextBool();
                    trigger.thresholdDb = -6.0f * (float) (1 + t.nextInt (10));
                    p.setTrigger (band, trigger);
                }
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
            else if (choice >= 134 && choice < 136) { begin ("LIVE on/off"); p.setLiveMode (! p.isLiveMode()); }
            else if (choice >= 136 && choice < 140)
            {
                // Knob linking: an LFO linked to a knob inside an effect (from the list, or by LEARN with a knob
                // turned in the effect's window), or a link moved to another knob.
                begin ("knob link / LEARN");
                const auto knobs = p.getEffectParameters (band, slot);
                const int lfos = juce::jmax (1, p.getLfoCount (band));
                switch (r.nextInt (4))
                {
                    case 0:
                    case 1:
                        if (! knobs.empty()) p.addRoute (band, r.nextInt (lfos), knobs[(size_t) r.nextInt ((int) knobs.size())].first);
                        break;
                    case 2:
                        if (! knobs.empty() && p.getRouteCount (band) > 0)
                            p.setRouteDestination (band, r.nextInt (p.getRouteCount (band)), knobs[(size_t) r.nextInt ((int) knobs.size())].first);
                        break;
                    default:
                        if (p.beginParameterLearn (band, r.nextInt (lfos)))
                        {
                            if (auto* fx = p.getHostedEffect (band, slot); fx != nullptr && ! knobs.empty() && r.nextBool())
                            {
                                auto* knob = fx->getParameters()[knobs[(size_t) r.nextInt ((int) knobs.size())].first.parameter];
                                knob->beginChangeGesture();
                                knob->setValueNotifyingHost (r.nextFloat());
                                knob->endChangeGesture();
                            }
                            pump (2);
                            if (p.pollParameterLearn() < 0) p.cancelParameterLearn();
                        }
                        break;
                }
            }
            else if (choice >= 140 && choice < 143)
            {
                begin ("copy / paste plugin state");
                if (r.nextBool() || ! TriadProcessor::hasCopiedEffect()) p.copyEffectState (band, slot);
                const int to = r.nextInt (bands);
                if (TriadProcessor::hasCopiedEffect() && effects < maxEffects - 5)
                    p.pasteEffectState (to, r.nextBool() ? p.getSlotCount (to) : r.nextInt (p.getSlotCount (to)));
            }
            else if (choice >= 143 && choice < 146)
            {
                // Drag a slot to another place in its band (in the band strip when Triad's window is open).
                begin ("drag a slot");
                const int to = r.nextInt (slots);
                auto* e = dynamic_cast<TriadEditor*> (editor.get());
                auto* strip = e != nullptr && e->getTab() == TriadEditor::Tab::bands ? e->getStrip (band) : nullptr;
                if (strip != nullptr && strip->getNumRows() == slots && slots > 1)
                {
                    auto& row = strip->getRow (slot);
                    const float startY = (float) row.getY() + 15.0f;
                    const float endY = startY + (float) ((to - slot) * 32);
                    auto at = [&row] (float holderY, bool dragging)
                    {
                        const juce::Point<float> point (60.0f, holderY - (float) row.getY());
                        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point,
                                                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                                 &row, &row, juce::Time::getCurrentTime(), point, juce::Time::getCurrentTime(), 1, dragging);
                    };
                    row.mouseDown (at (startY, false));
                    for (int i = 1; i <= 6; ++i) row.mouseDrag (at (startY + (endY - startY) * (float) i / 6.0f + 0.01f, true));
                    row.mouseUp (at (endY, true));
                }
                else p.moveSlotTo (band, slot, to);
            }
            else if (choice >= 152 && choice < 155)
            {
                begin ("all dividers: hard wall / default / soft");
                const int orders[] { triad::CrossoverFilter::maxOrder, triad::CrossoverFilter::defaultOrder, triad::CrossoverFilter::minOrder,
                                     1 + r.nextInt (triad::CrossoverFilter::maxOrder) };
                p.setAllCrossoverSlopes (orders[r.nextInt (4)]);
            }
            else if (choice >= 155)
            {
                // PRE or POST: an effect loaded into it, one removed, or its strip opened / closed in Triad's window.
                begin ("PRE / POST chain");
                const int pre = r.nextBool() ? TriadProcessor::preBand : TriadProcessor::postBand;
                const int preSlots = p.getSlotCount (pre);
                switch (r.nextInt (4))
                {
                    case 0:
                    case 1:
                        if (effects < maxEffects - 5)
                        {
                            const auto& d = pool.effects[(size_t) r.nextInt ((int) pool.effects.size())];
                            if (p.loadEffect (pre, r.nextBool() ? preSlots : r.nextInt (preSlots), d, false).isNotEmpty()) ++failedLoads;
                        }
                        break;
                    case 2: p.removeSlot (pre, r.nextInt (preSlots)); break;
                    default:
                        if (auto* e = dynamic_cast<TriadEditor*> (editor.get()))
                        {
                            e->setTab (TriadEditor::Tab::bands);
                            if (pre == TriadProcessor::preBand) e->togglePre(); else e->togglePost();
                        }
                        break;
                }
            }
            else if (choice >= 146)
            {
                // The back / forward arrows, sometimes several clicks in a row, sometimes right after a change.
                begin ("undo / redo");
                if (r.nextBool()) p.commitHistory();
                auto* e = dynamic_cast<TriadEditor*> (editor.get());
                for (int n = 1 + r.nextInt (3); --n >= 0;)
                {
                    const bool back = r.nextInt (3) != 0;
                    if (e != nullptr) { if (back) e->undo(); else e->redo(); }
                    else if (back) p.undo(); else p.redo();
                }
            }
            else if (choice < 130)
            {
                begin ("band volume");
                if (auto* gain = p.parameters.getParameter (TriadProcessor::bandGainID (band))) gain->setValueNotifyingHost (r.nextFloat());
            }
            else
            {
                // The LFO palette: any folder (by type, GEN, EXTRA, USER) and page, a shape clicked, the dice,
                // a shape saved to USER or deleted from it.
                begin ("LFO palette: folders, pages, USER, dice");
                if (dynamic_cast<TriadEditor*> (editor.get()) == nullptr)
                {
                    editor.reset (p.createEditor());
                    editor->setVisible (true);
                }
                if (auto* e = dynamic_cast<TriadEditor*> (editor.get()))
                {
                    e->setTab (TriadEditor::Tab::lfo);
                    e->showLfo (band, r.nextBool() ? -1 : slot);
                    auto& page = e->getLfoPage();
                    const int folders = triad::mod::numPresetCategories();
                    const int folder = r.nextInt (folders + 1);
                    page.setPaletteCategory (folder == folders ? LfoPage::userFolder : folder);
                    page.setPalettePage (r.nextInt (page.getNumPalettePages()));
                    switch (r.nextInt (4))
                    {
                        case 0:
                            if (page.getNumPaletteButtons() > 0)
                                if (auto* b = page.getPaletteButton (r.nextInt (page.getNumPaletteButtons())); b != nullptr && b->onClick) b->onClick();
                            break;
                        case 1: page.generate(); break;
                        case 2:
                            if (userShapes.size() < 30)
                                if (const auto f = page.saveShapeToUser ("Stress shape"); f != juce::File()) userShapes.push_back (f);
                            break;
                        default:
                            if (! userShapes.empty())
                            {
                                const auto k = (size_t) r.nextInt ((int) userShapes.size());
                                triad::userShapes::remove (userShapes[k]);
                                userShapes.erase (userShapes.begin() + (std::ptrdiff_t) k);
                                page.setPaletteCategory (0);
                                page.setPaletteCategory (LfoPage::userFolder);
                            }
                            break;
                    }
                }
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
        removeUserShapes();
        stop = true; resumeAudio(); audioThread.join();
        if (watchdog != nullptr) watchdog->watchAudio (nullptr, nullptr);
        p.setPlayHead (nullptr);
        throw;
    }
    removeUserShapes();
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
         + " automation moves), worst block " + ms (worstBlock.load()) + ", up to " + juce::String (peakEffects) + " effects loaded at once, up to "
         + juce::String (peakWaiting) + " removed effects waiting their 1.5 s to be deleted (" + juce::String (peakLate)
         + " of them 3 s late or more), memory up to " + juce::String (peakMemory) + " MB");
    require (peakLate < 100, "Removed effects are deleted as fast as they are removed (no pile-up holding memory)");
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

// ---- 4. Live vocal monitoring: latency and real-time safety ----------------------------------------
// What a singer hears through Triad while recording: (a) the delay Triad adds and reports to the DAW
// (it should be zero unless a hosted effect itself has latency), checked by measuring an impulse;
// (b) the timing smear of the crossovers (group delay, from the phase of the bands added back);
// (c) whether Triad keeps up at the small buffer sizes used for live monitoring, on an audio thread
// paced like a real audio interface, with the Triad window open.
namespace live
{
constexpr double rate = 48000.0;
#if JUCE_DEBUG
constexpr bool debugBuild = true;
#else
constexpr bool debugBuild = false;
#endif

// Lets parameter smoothing and slope crossfades finish.
void runSilence (TriadProcessor& p, int block, double seconds)
{
    juce::AudioBuffer<float> silence (2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i < (int) (rate * seconds / block); ++i) { silence.clear(); p.processBlock (silence, midi); }
}
// The output for a unit impulse at the input (both channels), `length` samples.
std::vector<float> impulseResponse (TriadProcessor& p, int block, int length)
{
    runSilence (p, block, 0.25);
    std::vector<float> out;
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    for (int done = 0; done < length; done += block)
    {
        audio.clear();
        if (done == 0) { audio.setSample (0, 0, 1.0f); audio.setSample (1, 0, 1.0f); }
        p.processBlock (audio, midi);
        for (int i = 0; i < block; ++i) out.push_back (audio.getSample (0, i));
    }
    out.resize ((size_t) length);
    return out;
}
int firstSound (const std::vector<float>& h)
{
    float peak = 0.0f;
    for (auto v : h) peak = juce::jmax (peak, std::abs (v));
    for (size_t i = 0; i < h.size(); ++i) if (std::abs (h[i]) > peak * 0.01f) return (int) i;
    return -1;
}
int peakAt (const std::vector<float>& h)
{
    int at = 0;
    for (size_t i = 1; i < h.size(); ++i) if (std::abs (h[i]) > std::abs (h[(size_t) at])) at = (int) i;
    return at;
}
// Group delay (ms) at a frequency, from the impulse response: Re(FFT(n h) / FFT(h)).
std::vector<double> groupDelayMs (const std::vector<float>& h, const std::vector<float>& freqs)
{
    const size_t n = h.size();
    std::vector<std::complex<float>> a (n), b (n);
    for (size_t i = 0; i < n; ++i) { a[i] = { h[i], 0.0f }; b[i] = { (float) i * h[i], 0.0f }; }
    triad::fft (a); triad::fft (b);
    std::vector<double> out;
    for (auto f : freqs)
    {
        const auto k = (size_t) juce::roundToInt ((double) f / rate * (double) n);
        const auto ratio = b[k] / a[k];
        out.push_back ((double) ratio.real() / rate * 1000.0);
    }
    return out;
}
juce::String samplesAndMs (int samples) { return juce::String (samples) + " samples (" + juce::String (samples / rate * 1000.0, 2) + " ms)"; }

// The audio thread: blocks paced like an audio interface (one every block/rate seconds), each one's
// processing time measured. The message thread keeps running (Triad's window redraws) meanwhile.
// Per block: wall-clock time, and the CPU time Triad's own work took on that thread. On a shared or
// virtual machine the thread is sometimes paused by the system (wall time far above CPU time): that is
// counted apart from blocks where Triad's work itself did not fit in the buffer.
struct Timing { double average = 0.0, worst = 0.0, p999 = 0.0, worstCpu = 0.0; int late = 0, paused = 0, blocks = 0, silenced = 0; bool realtime = false; };
double threadCpuMs()
{
    timespec t {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
    return (double) t.tv_sec * 1000.0 + (double) t.tv_nsec / 1.0e6;
}
class PacedAudio final : public juce::Thread
{
public:
    PacedAudio (TriadProcessor& proc, Transport& t, int blockSize, double secs)
        : Thread ("Triad live-monitoring test"), p (proc), transport (t), block (blockSize), seconds (secs) {}
    Timing result;
    void run() override
    {
        juce::AudioBuffer<float> audio (2, block);
        juce::MidiBuffer midi;
        juce::Random r (7);
        const double period = 1000.0 * block / rate;
        const int count = (int) (rate * seconds / block);
        std::vector<double> times;
        times.reserve ((size_t) count);
        double next = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < count && ! threadShouldExit(); ++i)
        {
            // A voice-like test signal: noise bursts with gaps.
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < block; ++s)
                    audio.setSample (c, s, ((i / 40) % 3 == 2 ? 0.0f : 0.25f) * (r.nextFloat() * 2.0f - 1.0f));
            const auto c0 = threadCpuMs();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            p.processBlock (audio, midi);
            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto cpu = threadCpuMs() - c0;
            transport.advance (block, rate);
            times.push_back (cpu);
            result.worst = juce::jmax (result.worst, dt);
            if (cpu > period) ++result.late;
            else if (dt > period) ++result.paused;
            next += period;
            for (auto now = juce::Time::getMillisecondCounterHiRes(); now < next; now = juce::Time::getMillisecondCounterHiRes())
            {
                if (next - now > 1.5) juce::Thread::sleep (1);
                else std::this_thread::yield();
            }
            if (juce::Time::getMillisecondCounterHiRes() > next + 4.0 * period) next = juce::Time::getMillisecondCounterHiRes();   // fell behind: start again
        }
        result.blocks = (int) times.size();
        if (times.empty()) return;
        double total = 0.0;
        for (auto t : times) { total += t; result.worstCpu = juce::jmax (result.worstCpu, t); }
        result.average = total / (double) times.size();
        std::sort (times.begin(), times.end());
        result.p999 = times[juce::jmin (times.size() - 1, (size_t) ((double) times.size() * 0.999))];
    }
private:
    TriadProcessor& p;
    Transport& transport;
    int block;
    double seconds;
};
Timing play (TriadProcessor& p, Transport& transport, int block, double seconds)
{
    PacedAudio audio (p, transport, block, seconds);
    const int silencedBefore = p.getSilencedBlockCount();
    const auto options = juce::Thread::RealtimeOptions().withPriority (10).withApproximateAudioProcessingTime (block, rate)
                                                        .withPeriodMs (1000.0 * block / rate);
    const bool realtime = audio.startRealtimeThread (options);
    if (! realtime) audio.startThread (juce::Thread::Priority::highest);
    // Meanwhile the DAW autosaves the project every second (the message thread saves every effect's state).
    auto lastSave = juce::Time::getMillisecondCounterHiRes();
    while (audio.isThreadRunning())
    {
        pump (20);
        if (juce::Time::getMillisecondCounterHiRes() - lastSave > 1000.0)
        {
            juce::MemoryBlock state;
            p.getStateInformation (state);
            lastSave = juce::Time::getMillisecondCounterHiRes();
        }
    }
    audio.stopThread (2000);
    auto t = audio.result;
    t.realtime = realtime;
    t.silenced = p.getSilencedBlockCount() - silencedBefore;
    return t;
}
}

void liveMonitoring (const Pool& pool, bool real)
{
    using namespace live;
    say ("\n== 4. Live vocal monitoring: latency and real-time safety ==");
    // (a) The delay Triad adds, reported to the DAW and measured with an impulse.
    step ("live monitoring: Triad's own delay", 300);
    struct Setup { const char* name; int bands; int order; };
    for (const auto& setup : { Setup { "3 bands, 24 dB/oct (default)", 3, 4 }, Setup { "3 bands, 6 dB/oct (softest)", 3, 1 },
                               Setup { "3 bands, 96 dB/oct (hardest)", 3, 16 }, Setup { "10 bands, 96 dB/oct", 10, 16 } })
    {
        TriadProcessor p;
        p.prepareToPlay (rate, 64);
        while (p.getBandCount() < setup.bands && p.addBand()) {}
        for (int j = 0; j + 1 < p.getBandCount(); ++j) p.setCrossoverSlope (j, setup.order);
        const auto h = impulseResponse (p, 64, 16384);
        const int reported = p.getLatencySamples(), first = firstSound (h), peak = peakAt (h);
        say (juce::String ("Triad alone, ") + setup.name + ": reports " + samplesAndMs (reported) + " to the DAW; the sound starts at sample "
             + juce::String (first) + ", its peak at sample " + juce::String (peak) + " (" + juce::String (peak / rate * 1000.0, 2) + " ms)");
        // Minimum-phase crossovers: no latency; with many steep crossovers the click is rounded over a few samples (phase), not delayed.
        require (reported == 0 && first <= 4, "Triad alone adds no delay (nothing for the DAW to compensate)");
    }
    // (b) Timing smear of the crossovers: group delay of the bands added back, at vocal frequencies.
    {
        const std::vector<float> freqs { 100.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f };
        say ("Crossover timing smear (group delay of the bands added back, crossovers at 250 Hz and 2.5 kHz), in ms at:");
        juce::String header ("              ");
        for (auto f : freqs) header << juce::String (f < 1000.0f ? juce::String ((int) f) + " Hz" : juce::String (f / 1000.0f, 1) + " kHz").paddedLeft (' ', 9);
        say (header);
        double worstVoice = 0.0;
        for (int order : { 1, 2, 4, 8, 12, 16 })
        {
            TriadProcessor p;
            p.prepareToPlay (rate, 64);
            for (int j = 0; j + 1 < p.getBandCount(); ++j) p.setCrossoverSlope (j, order);
            const auto gd = groupDelayMs (impulseResponse (p, 64, 32768), freqs);
            juce::String line (TriadProcessor::slopeText (order).paddedRight (' ', 14));
            for (size_t i = 0; i < gd.size(); ++i)
            {
                line << juce::String (gd[i], 2).paddedLeft (' ', 9);
                if (freqs[i] >= 500.0f) worstVoice = juce::jmax (worstVoice, gd[i]);
            }
            say (line);
        }
        say ("(For comparison: a singer 1 m from the microphone arrives 2.9 ms late; a 64-sample buffer is 1.33 ms.)");
        juce::ignoreUnused (worstVoice);
    }
    // (c) Effects that report latency, and the delay Triad then reports: measured, with the effect's
    // slot at 0% mix, the band comes out exactly that much later and aligned with the others.
    step ("live monitoring: effects with latency", 600);
    {
        std::vector<std::pair<juce::String, int>> withLatency;
        int zero = 0;
        TriadProcessor probe;
        probe.prepareToPlay (rate, 64);
        for (size_t i = 0; i < pool.effects.size(); ++i)
        {
            if (probe.loadEffect (0, 0, pool.effects[i], false).isNotEmpty()) continue;
            settle (probe);
            pump (50);
            const int latency = probe.getSlotInfo (0, 0).latency;
            if (latency > 0) withLatency.push_back ({ pool.names[(int) i], latency });
            else ++zero;
            probe.removeEffect (0, 0);
            settle (probe);
        }
        juce::StringArray list;
        for (const auto& [name, latency] : withLatency) list.add (name + " " + samplesAndMs (latency));
        say (juce::String (zero) + " of " + juce::String ((int) pool.effects.size()) + " effects add no latency; "
             + (list.isEmpty() ? juce::String ("none add any.") : "these do (Triad then reports the largest band total to the DAW): " + list.joinIntoString ("; ")));
        if (! withLatency.empty())
        {
            // The one with the most latency, in band 2 at 0% mix: the output is the dry signal, delayed by exactly what Triad reports.
            size_t most = 0;
            for (size_t i = 0; i < withLatency.size(); ++i) if (withLatency[i].second > withLatency[most].second) most = i;
            const int index = pool.names.indexOf (withLatency[most].first);
            TriadProcessor p;
            p.prepareToPlay (rate, 64);
            require (p.loadEffect (1, 0, pool.effects[(size_t) index], false).isEmpty(), "Load " + withLatency[most].first);
            settle (p);
            pump (300);
            p.setSlotMix (1, 0, 0.0f);
            const auto h = impulseResponse (p, 64, 32768);
            const int reported = p.getLatencySamples(), first = firstSound (h);
            say ("With " + withLatency[most].first + " in band 2 (slot at 0% mix): Triad reports " + samplesAndMs (reported)
                 + "; the dry sound comes out at sample " + juce::String (first) + " (peak at " + juce::String (peakAt (h)) + ")");
            require (reported == withLatency[most].second && first == reported, "Triad reports exactly the delay it adds, so the DAW can line it up");
        }
    }
    // (d) Keeping up at live-monitoring buffer sizes, on a paced audio thread, with the window open.
    step ("live monitoring: real-time buffers", 900);
    {
        // A vocal chain: five effects without latency (what a singer can monitor through), the kinds used on
        // vocals first: dynamics, EQ, saturation, delay, reverb.
        std::vector<size_t> chain;
        {
            const juce::StringArray kinds { "Dynamics", "Compressor", "EQ", "MultiTool", "Tape", "Centaur", "TIME-12", "Delay", "Plate", "Reverb", "Room" };
            std::vector<size_t> order;
            for (const auto& kind : kinds)
                for (size_t i = 0; i < pool.effects.size(); ++i)
                    if (pool.names[(int) i].containsIgnoreCase (kind) && std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i);
            for (size_t i = 0; i < pool.effects.size(); ++i)
                if (std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i);
            TriadProcessor probe;
            probe.prepareToPlay (rate, 64);
            for (size_t i : order)
            {
                if (chain.size() >= 5) break;
                if (probe.loadEffect (0, 0, pool.effects[i], false).isNotEmpty()) continue;
                settle (probe);
                if (probe.getSlotInfo (0, 0).latency == 0) chain.push_back (i);
                probe.removeEffect (0, 0);
                settle (probe);
            }
        }
        juce::StringArray chainNames;
        for (auto i : chain) chainNames.add (pool.names[(int) i]);
        struct Case { const char* name; int bands, order, effects; bool lfos; };
        const Case cases[] { { "Triad alone, 3 bands, 24 dB/oct", 3, 4, 0, false },
                             { "Triad alone, 10 bands, 96 dB/oct", 10, 16, 0, false },
                             { "Triad alone, 10 bands, 96 dB/oct, every LFO on", 10, 16, 0, true },
                             { "vocal chain: 3 bands, 5 effects, LFOs on, 48 dB/oct", 3, 8, (int) chain.size(), true } };
        say ("Vocal chain effects: " + (chainNames.isEmpty() ? juce::String ("none available") : chainNames.joinIntoString (", ")));
        say ("Each case plays " + juce::String (real ? 8 : 6) + " s per buffer size at 48 kHz on an audio thread paced like an interface, with Triad's window open."
             " Times are Triad's own work per block (CPU time); \"late\": the work took longer than the buffer lasts (a dropout on any machine);"
             " \"paused by the system\": the test machine (shared, virtual) stopped the thread for a moment, not Triad's work;"
             " \"silenced\": blocks Triad output as silence because its window was changing the bands at that moment."
             " The project is saved every second meanwhile, like a DAW's autosave.");
        juce::Random random (5);
        for (const auto& c : cases)
        {
            if (c.effects == 0 && juce::String (c.name).startsWith ("vocal")) continue;
            for (int block : { 32, 64, 128, 256 })
            {
                step (juce::String ("live monitoring: ") + c.name + ", " + juce::String (block) + " samples", 300);
                auto p = std::make_unique<TriadProcessor>();
                Transport transport;
                p->setPlayHead (&transport);
                p->prepareToPlay (rate, block);
                while (p->getBandCount() < c.bands && p->addBand()) {}
                for (int j = 0; j + 1 < p->getBandCount(); ++j) p->setCrossoverSlope (j, c.order);
                for (int e = 0; e < c.effects; ++e)
                {
                    const int band = e == 0 ? 0 : (e < 3 ? 1 : 2);
                    const int slot = e == 0 ? 0 : (e < 3 ? e - 1 : e - 3);
                    require (p->loadEffect (band, slot, pool.effects[chain[(size_t) e]], false).isEmpty(), "Load the vocal chain");
                }
                settle (*p);
                if (c.lfos) activateAllLfos (*p, random);
                std::unique_ptr<juce::AudioProcessorEditor> editor;
                if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
                {
                    editor.reset (p->createEditor());
                    editor->setVisible (true);
                    if (auto* e = dynamic_cast<TriadEditor*> (editor.get())) e->setTab (c.lfos ? TriadEditor::Tab::lfo : TriadEditor::Tab::bands);
                }
                pump (200);
                runSilence (*p, block, 0.2);
                const auto t = play (*p, transport, block, real ? 8.0 : 6.0);
                const double budget = 1000.0 * block / rate;
                say ("  " + juce::String (c.name) + ", " + juce::String (block).paddedLeft (' ', 3) + " samples (" + juce::String (budget, 2) + " ms): average "
                     + juce::String (juce::roundToInt (t.average * 1000.0)) + " us (" + juce::String (100.0 * t.average / budget, 1) + "% of the time), 99.9% under "
                     + juce::String (juce::roundToInt (t.p999 * 1000.0)) + " us, worst " + juce::String (juce::roundToInt (t.worstCpu * 1000.0)) + " us; late "
                     + juce::String (t.late) + " of " + juce::String (t.blocks) + ", silenced " + juce::String (t.silenced) + ", paused by the system "
                     + juce::String (t.paused) + " (longest wait " + juce::String (t.worst, 1) + " ms); Triad latency " + samplesAndMs (p->getLatencySamples())
                     + (t.realtime ? "" : " (normal-priority thread)"));
                require (p->getLatencySamples() == 0, "No added latency with zero-latency effects");
                require (t.silenced == 0, "No dropouts from Triad's window, its timer or saving the project while monitoring");
                // Triad on its own must keep up at 64 samples and above: well under half the time a block lasts, on
                // average. (Single late blocks are reported, not failed: the CI Mac is a virtual machine whose host
                // pauses it, at times for seconds, and a paused virtual CPU still counts as CPU time for the thread.
                // Only in optimised builds: a debug build runs the filters several times slower.)
                if (c.effects == 0 && block >= 64 && ! debugBuild)
                    require (t.average < 0.5 * budget, "Triad alone keeps up with live monitoring at " + juce::String (block) + " samples");
                editor.reset();
                p->setPlayHead (nullptr);
                p.reset();
                pump (50);
            }
        }
    }
}

// ---- 5. Knobs inside the effects: Triad's LFO links, LEARN and undo with every effect ---------------------------
// For every effect: how many knobs it lets a host automate; then, in Triad itself, an LFO linked to up to four of
// them (one by LEARN, the others from the list) while audio plays: the knobs follow the LFO, the sound changes, the
// output stays finite. A knob turned (a gesture, as in its window) and undone goes back. Each effect is tried in a
// separate process, so an effect that crashes when its knobs move is reported instead of ending the test.
struct ParameterProbe { int parameters = 0, linked = 0; bool arrived = false, changed = false, finite = true, learned = false, undone = false; juce::String example; };
bool isLinkable (const juce::String& name) { return name.isNotEmpty() && ! name.containsIgnoreCase ("bypass"); }
ParameterProbe probeParameters (const juce::PluginDescription& description)
{
    namespace mod = triad::mod;
    constexpr double rate = 48000.0;
    constexpr int block = 128;
    ParameterProbe result;
    auto run = [&] (bool modulate, std::vector<float>& trace)
    {
        TriadProcessor q;
        q.prepareToPlay (rate, block);
        if (q.loadEffect (0, 0, description, false).isNotEmpty()) return false;
        settle (q);
        auto* fx = q.getHostedEffect (0, 0);
        if (fx == nullptr) return false;
        std::vector<TriadProcessor::Destination> knobs;
        for (auto& [destination, knobName] : q.getEffectParameters (0, 0))
            if (isLinkable (knobName)) { knobs.push_back (destination); if (result.example.isEmpty()) result.example = knobName; }
        result.parameters = (int) knobs.size();
        if (knobs.empty()) return true;
        std::vector<int> linked;
        if (modulate)
        {
            const int lfo = q.addLfo (0);
            q.setLfoShape (0, lfo, mod::preset (3), false);   // Triangle
            for (int i = 0; i < (int) mod::lengths().size(); ++i)
                if (juce::String (mod::lengths()[(size_t) i].label) == "1/4") q.setLfoLength (0, lfo, i);
            // LEARN: a gesture on the first knob (as when it is turned in the effect's window) links it.
            if (q.beginParameterLearn (0, lfo))
            {
                auto* knob = fx->getParameters()[knobs.front().parameter];
                knob->beginChangeGesture();
                knob->setValueNotifyingHost (knob->getValue());
                knob->endChangeGesture();
                const int route = q.pollParameterLearn();
                result.learned = route >= 0;
                if (route >= 0) linked.push_back (knobs.front().parameter);
                else q.cancelParameterLearn();
            }
            for (size_t j = 1; j < knobs.size() && linked.size() < 4; ++j)
                if (q.addRoute (0, lfo, knobs[j]) >= 0) linked.push_back (knobs[j].parameter);
            result.linked = (int) linked.size();
        }
        std::vector<float> lowest (linked.size(), 1.0f), highest (linked.size(), 0.0f);
        juce::AudioBuffer<float> audio (2, block);
        juce::MidiBuffer midi;
        juce::Random noise (5);
        for (int i = 0; i < (int) (2.0 * rate / block); ++i)
        {
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < block; ++k) audio.setSample (c, k, (noise.nextFloat() * 2.0f - 1.0f) * 0.25f);
            q.processBlock (audio, midi);
            for (size_t j = 0; j < linked.size(); ++j)
            {
                const float v = fx->getParameters()[linked[j]]->getValue();
                lowest[j] = juce::jmin (lowest[j], v);
                highest[j] = juce::jmax (highest[j], v);
            }
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < block; ++k)
                {
                    const float v = audio.getSample (c, k);
                    if (! std::isfinite (v)) result.finite = false;
                    if (c == 0 && k % 16 == 0) trace.push_back (v);
                }
        }
        if (modulate)
        {
            result.arrived = ! linked.empty();
            for (size_t j = 0; j < linked.size(); ++j) result.arrived &= highest[j] - lowest[j] > 0.25f;
        }
        else
        {
            // Undo: a knob turned in the effect's window goes back (the effect stays loaded; its settings are restored).
            auto* knob = fx->getParameters()[knobs.front().parameter];
            const float before = knob->getValue();
            q.commitHistory();
            knob->beginChangeGesture();
            knob->setValueNotifyingHost (before < 0.5f ? 0.9f : 0.1f);
            knob->endChangeGesture();
            for (int i = 0; i < 4; ++i) q.processBlock (audio, midi);
            const bool moved = std::abs (knob->getValue() - before) > 0.02f;
            q.commitHistory();
            q.undo();
            for (int i = 0; i < 4; ++i) q.processBlock (audio, midi);
            pump (20);
            result.undone = q.getHostedEffect (0, 0) == fx && (! moved || std::abs (knob->getValue() - before) < 0.02f);
        }
        return true;
    };
    std::vector<float> plain, moved;
    if (! run (false, plain) || result.parameters == 0) return result;
    run (true, moved);
    double difference = 0.0;
    for (size_t k = 0; k < juce::jmin (plain.size(), moved.size()); ++k) difference += std::abs (plain[k] - moved[k]);
    result.changed = difference > 1.0e-3;
    return result;
}
// Child process: TriadStress --param-probe <description.xml> <result.txt>
int parameterProbeChild (const juce::File& descriptionFile, const juce::File& resultFile)
{
    juce::PluginDescription description;
    if (auto xml = juce::XmlDocument::parse (descriptionFile); xml == nullptr || ! description.loadFromXml (*xml)) return 2;
    const auto r = probeParameters (description);
    resultFile.replaceWithText (juce::String (r.parameters) + "|" + (r.arrived ? "1" : "0") + "|" + (r.changed ? "1" : "0") + "|"
                                + (r.finite ? "1" : "0") + "|" + (r.learned ? "1" : "0") + "|" + (r.undone ? "1" : "0") + "|"
                                + juce::String (r.linked) + "|" + r.example);
    return 0;
}
void parameterLinking (const Pool& pool)
{
    say ("\n== 5. Knobs inside the effects: Triad's LFO links, LEARN and undo, with every effect ==");
    int withParameters = 0, followed = 0, changedSound = 0, tested = 0, learned = 0, undone = 0;
    std::vector<int> counts;
    juce::StringArray notes, crashed;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadParameterProbe");
    folder.createDirectory();
    for (size_t e = 0; e < pool.effects.size(); ++e)
    {
        const auto& name = pool.names[(int) e];
        step ("knobs: " + name, 150);
        const auto descriptionFile = folder.getChildFile ("effect.xml"), resultFile = folder.getChildFile ("result.txt");
        resultFile.deleteFile();
        pool.effects[e].createXml()->writeTo (descriptionFile);
        juce::ChildProcess child;
        const juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), "--param-probe",
                                       descriptionFile.getFullPathName(), resultFile.getFullPathName() };
        if (! child.start (args, 0)) { notes.add (name + ": could not start the probe"); continue; }
        const bool finished = child.waitForProcessToFinish (120000);
        if (! finished) child.kill();
        const auto fields = juce::StringArray::fromTokens (resultFile.loadFileAsString(), "|", "");
        if (! finished || child.getExitCode() != 0 || fields.size() < 7)
        {
            crashed.add (name);
            continue;
        }
        ++tested;
        const int parameters = fields[0].getIntValue();
        const bool arrived = fields[1] == "1", changed = fields[2] == "1", finite = fields[3] == "1", learnt = fields[4] == "1", back = fields[5] == "1";
        counts.push_back (parameters);
        if (parameters == 0) { notes.add (name + ": no automatable knobs"); continue; }
        ++withParameters;
        followed += arrived ? 1 : 0;
        changedSound += changed ? 1 : 0;
        learned += learnt ? 1 : 0;
        undone += back ? 1 : 0;
        if (! finite) notes.add (name + ": produced NaN/Inf when its knobs moved (Triad silences that)");
        if (! arrived) notes.add (name + ": a linked knob did not follow the LFO all the way (the effect steps or limits it)");
        if (! changed) notes.add (name + ": its first knobs did not change the sound of the test noise");
        if (! learnt) notes.add (name + ": LEARN did not link its first knob");
        if (! back) notes.add (name + ": undo did not put its first knob back");
        if (verbose)
            say ("  " + name + ": " + juce::String (parameters) + " knobs (e.g. " + fields[7] + "); " + fields[6] + " linked: "
                 + (arrived ? "they follow the LFO" : "not all follow") + (changed ? ", the sound follows" : ", no audible change on noise")
                 + (learnt ? ", LEARN works" : ", LEARN failed") + (back ? ", undo works" : ", undo failed"));
    }
    std::sort (counts.begin(), counts.end());
    const int median = counts.empty() ? 0 : counts[counts.size() / 2];
    say (juce::String (withParameters) + " of " + juce::String (tested + crashed.size()) + " effects have knobs an LFO can link to (median "
         + juce::String (median) + " each, up to " + juce::String (counts.empty() ? 0 : counts.back()) + "). With Triad's own links (an LFO on up to "
         + "4 knobs, set every 64 samples): the knobs followed in " + juce::String (followed) + ", the sound followed in " + juce::String (changedSound)
         + "; LEARN linked a knob in " + juce::String (learned) + "; undo put a turned knob back in " + juce::String (undone)
         + (crashed.isEmpty() ? juce::String ("; none crashed") : "; " + juce::String (crashed.size()) + " crashed (inside the effect): " + crashed.joinIntoString (", ")));
    for (const auto& n : notes) say ("  note: " + n);
    folder.deleteRecursively();
    require (tested > 0, "Knob linking probe ran");
}

// ---- 2 with real plug-ins: each random sequence in its own process ---------------------------------------------
// Like one DAW session each, with one format of every plug-in (VST3 or AU, as people use them; both builds of the
// same plug-in in one process share some of their code and state). Everything the child prints is passed on.
void writePool (const Pool& pool, const juce::File& file)
{
    juce::XmlElement root ("POOL");
    for (size_t e = 0; e < pool.effects.size(); ++e)
    {
        auto x = pool.effects[e].createXml();
        x->setAttribute ("triadName", pool.names[(int) e]);
        root.addChildElement (x.release());
    }
    for (const auto& key : pool.openGlWindows) root.createNewChildElement ("OPENGL")->setAttribute ("key", key);
    root.writeTo (file);
}
Pool readPool (const juce::File& file)
{
    Pool pool;
    if (auto xml = juce::XmlDocument::parse (file))
        for (auto* x : xml->getChildIterator())
        {
            if (x->hasTagName ("OPENGL")) { pool.openGlWindows.insert (x->getStringAttribute ("key")); continue; }
            juce::PluginDescription d;
            if (d.loadFromXml (*x)) { pool.effects.push_back (d); pool.names.add (x->getStringAttribute ("triadName")); }
        }
    return pool;
}
// Each plug-in once: in `format` where it comes in that format too, otherwise in the one it has.
Pool oneFormatEach (const Pool& pool, const juce::String& format)
{
    auto key = [] (const juce::PluginDescription& d) { return d.name.toLowerCase().removeCharacters (" -_."); };
    std::map<juce::String, std::vector<size_t>> builds;
    for (size_t e = 0; e < pool.effects.size(); ++e) builds[key (pool.effects[e])].push_back (e);
    Pool out;
    out.openGlWindows = pool.openGlWindows;
    for (size_t e = 0; e < pool.effects.size(); ++e)
    {
        const auto& group = builds[key (pool.effects[e])];
        const bool hasFormat = std::any_of (group.begin(), group.end(), [&] (size_t i) { return pool.effects[i].pluginFormatName == format; });
        const auto chosen = hasFormat ? *std::find_if (group.begin(), group.end(), [&] (size_t i) { return pool.effects[i].pluginFormatName == format; })
                                      : group.front();
        if (chosen == e) { out.effects.push_back (pool.effects[e]); out.names.add (pool.names[(int) e]); }
    }
    return out;
}
// Child process: TriadStress --edits <pool.xml> <seed> <max seconds>
int editsChild (const juce::File& poolFile, juce::int64 seed, double maxSeconds)
{
    verbose = true;
    Watchdog guard;
    watchdog = &guard;
    installCrashHandler();
    try
    {
        editsWhilePlaying (readPool (poolFile), seed, 3000, maxSeconds);
        if (! invalidEffects.empty())
        {
            juce::StringArray list;
            for (auto& [name, count] : invalidEffects) list.add (name + ": " + juce::String (count) + (count == 1 ? " block" : " blocks"));
            say ("Hosted effects that produced invalid audio (NaN/Inf), which Triad silenced and then restarted or bypassed: " + list.joinIntoString ("; "));
        }
        watchdog = nullptr;
        std::cout << "TRIAD-EDITS-OK" << std::endl;   // the only proof of success (a crashed child can look like exit code 0)
        return 0;
    }
    catch (const std::exception& e)
    {
        say (juce::String ("FAIL: ") + e.what());
        watchdog = nullptr;
        return 1;
    }
}
// True when the sequence finished and held every check; a crash or a failed check is reported, and the caller decides.
bool editsInOwnProcess (const Pool& pool, juce::int64 seed, double maxSeconds, const juce::String& format)
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadEdits");
    folder.createDirectory();
    const auto poolFile = folder.getChildFile ("pool.xml");
    const auto chosen = oneFormatEach (pool, format);
    writePool (chosen, poolFile);
    say ("\n(Seed " + juce::String (seed) + " in its own process, like one DAW session: " + juce::String ((int) chosen.effects.size())
         + " effects, each plug-in in one format, " + format + " where it has both)");
    step ("random edits with real plug-ins, seed " + juce::String (seed) + " (own process)", maxSeconds + 900.0);
    juce::ChildProcess child;
    const juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), "--edits",
                                   poolFile.getFullPathName(), juce::String (seed), juce::String (maxSeconds) };
    require (child.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr), "Start the random edits process");
    juce::String pending;
    char buffer[8192];
    bool succeeded = false;
    auto forward = [&succeeded] (const juce::String& line)
    {
        if (line.trim() == "TRIAD-EDITS-OK") succeeded = true;
        else if (line.startsWith ("  > ") || line.startsWith ("  ... ")) std::cout << line << std::endl;   // progress: the log only
        else say (line);
    };
    for (;;)
    {
        const int n = child.readProcessOutput (buffer, (int) sizeof (buffer));
        if (n <= 0) break;
        pending += juce::String::fromUTF8 (buffer, n);
        for (int end = pending.indexOfChar ('\n'); end >= 0; end = pending.indexOfChar ('\n'))
        {
            forward (pending.substring (0, end));
            pending = pending.substring (end + 1);
        }
    }
    if (pending.isNotEmpty()) forward (pending);
    child.waitForProcessToFinish (30000);
    const auto code = child.getExitCode();
    folder.deleteRecursively();
    if (succeeded && code == 0) return true;
    say ("FAILED: random edits with real plug-ins (seed " + juce::String (seed) + ", " + format + "): the process "
         + (code == 1 ? juce::String ("failed a check") : juce::String ("crashed (its crash report is above)")) + "; the other sections go on");
    return false;
}

// ---- 0. Removing effects, one effect at a time --------------------------------------------------------------
// Each effect on its own, in its own process: loaded into a band, PRE or POST, played, its window opened, then
// removed (Triad deletes it a moment later), 8 times; then 6 copies at once, all removed by loading an empty project,
// and Triad closed. An effect that crashes here crashes when it is deleted (inside the effect: any host deletes
// removed effects sooner or later); it is left out of the random edits that follow, which test Triad.
int removalProbeChild (const juce::File& descriptionFile, const juce::File& resultFile)
{
    juce::PluginDescription description;
    if (auto xml = juce::XmlDocument::parse (descriptionFile); xml == nullptr || ! description.loadFromXml (*xml)) return 2;
    const bool display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    int loaded = 0;
    {
        TriadProcessor q;
        q.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> audio (2, 128);
        juce::MidiBuffer midi;
        juce::Random noise (5);
        auto play = [&] (int blocks) { for (int i = 0; i < blocks; ++i) { fillNoise (audio, noise); q.processBlock (audio, midi); } };
        auto showWindow = [&] (int band, int slot)
        {
            if (! display) return;
            q.showEffect (band, slot);
            if (! hardwareGraphics() && usesOpenGL (effectWindow (band, slot))) q.closeWindows();
            pump (80);
        };
        const int chains[] { 0, TriadProcessor::preBand, 1, TriadProcessor::postBand };
        for (int round = 0; round < 8; ++round)
        {
            const int band = chains[round % 4];
            if (q.loadEffect (band, 0, description, false).isNotEmpty()) continue;
            settle (q);
            ++loaded;
            play (8);
            if (round % 2 == 0) showWindow (band, 0);
            q.removeEffect (band, 0);
            play (4);
            pump (round == 7 ? 2600 : 250);   // some are deleted while the next copies come and go
        }
        for (int copy = 0; copy < 6; ++copy)
        {
            const int band = copy % 3;
            if (q.loadEffect (band, q.getSlotCount (band), description, false).isEmpty()) ++loaded;
        }
        settle (q);
        play (8);
        showWindow (0, 1);
        juce::MemoryBlock empty;
        { TriadProcessor blank; blank.getStateInformation (empty); }
        q.setStateInformation (empty.getData(), (int) empty.getSize());   // every copy removed in the same moment
        settle (q);
        play (8);
        pump (2600);
    }
    pump (500);   // Triad closed: the idle copy kept for this effect goes too
    resultFile.replaceWithText ("ok|" + juce::String (loaded));
    return 0;
}
juce::StringArray removingEffects (const Pool& pool)
{
    say ("\n== 0. Removing effects: each effect loaded, played, its window opened and removed 8 times, then 6 copies removed at once (each effect in its own process) ==");
    juce::StringArray crashed, notes;
    int tested = 0;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadRemovalProbe");
    folder.createDirectory();
    for (size_t e = 0; e < pool.effects.size(); ++e)
    {
        const auto& name = pool.names[(int) e];
        step ("removing: " + name, 150);
        const auto descriptionFile = folder.getChildFile ("effect.xml"), resultFile = folder.getChildFile ("result.txt");
        resultFile.deleteFile();
        pool.effects[e].createXml()->writeTo (descriptionFile);
        juce::ChildProcess child;
        const juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), "--removal-probe",
                                       descriptionFile.getFullPathName(), resultFile.getFullPathName() };
        if (! child.start (args, 0)) { notes.add (name + ": could not start the probe"); continue; }
        const bool finished = child.waitForProcessToFinish (120000);
        if (! finished) child.kill();
        const auto fields = juce::StringArray::fromTokens (resultFile.loadFileAsString(), "|", "");
        if (! finished || child.getExitCode() != 0 || fields.size() < 2 || fields[0] != "ok")
        {
            crashed.add (name);
            say ("  " + name + ": " + (finished ? "crashed (exit code " + juce::String (child.getExitCode()) + ")" : juce::String ("did not finish in 2 minutes"))
                 + " while copies of it were removed and deleted");
            continue;
        }
        ++tested;
        if (fields[1].getIntValue() == 0) notes.add (name + ": did not load in Triad here");
    }
    say (juce::String (tested) + " of " + juce::String ((int) pool.effects.size()) + " effects were removed and deleted 14 times each without a problem"
         + (crashed.isEmpty() ? juce::String() : "; " + juce::String (crashed.size()) + " crashed inside the effect: " + crashed.joinIntoString (", ")
                                                  + " (left out of the random edits below)"));
    for (const auto& n : notes) say ("  note: " + n);
    folder.deleteRecursively();
    return crashed;
}

// ---- 6. Plug-in list with crashing plug-ins -------------------------------------------------------
void pluginList (const juce::File& effect, const juce::File& crashFixture, const juce::File& scanner)
{
    say ("\n== 6. Plug-in list: 60 plug-ins, 20 of which crash when loaded ==");
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

#include "RecordingTests.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc == 4 && juce::String (argv[1]) == "--param-probe")
        return parameterProbeChild (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));
    if (argc == 5 && juce::String (argv[1]) == "--edits")
        return editsChild (juce::File (juce::String::fromUTF8 (argv[2])), juce::String (argv[3]).getLargeIntValue(), juce::String (argv[4]).getDoubleValue());
    if (argc == 4 && juce::String (argv[1]) == "--removal-probe")
        return removalProbeChild (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));
    Watchdog guard;
    watchdog = &guard;
    installCrashHandler();
    juce::File reportFile;
    try
    {
        require (argc >= 4, "Usage: TriadStress <Test Effect.vst3> <Crash Fixture.vst3> <Triad Scanner> [report.txt] [seed] [--real] [--live]");
        const juce::File effect (juce::String::fromUTF8 (argv[1]));
        const juce::File crashFixture (juce::String::fromUTF8 (argv[2]));
        const juce::File scanner (juce::String::fromUTF8 (argv[3]));
        // --real: use every effect installed on this computer instead of the test effect + Apple's AUs.
        bool real = false, liveOnly = false;   // --live: only the live-monitoring section
        bool recordingOnly = false;            // --recording: only the recording and pitch-correction sections (7 and 8)
        juce::int64 seed = 20261015;
        for (int i = 4; i < argc; ++i)
        {
            const auto arg = juce::String::fromUTF8 (argv[i]);
            if (arg == "--real") real = true;
            else if (arg == "--live") liveOnly = true;
            else if (arg == "--recording") recordingOnly = true;
            else if (arg.containsOnly ("0123456789")) seed = arg.getLargeIntValue();
            else reportFile = juce::File (arg);
        }
        reportPath = reportFile;
        verbose = real;
        say ("Triad stress test" + juce::String (real ? " with the plug-ins installed on this computer" : "") + ", Triad " TRIAD_VERSION " ("
             + juce::SystemStats::getOperatingSystemName() + ", " + juce::SystemStats::getCpuModel() + ", "
             + juce::String (juce::SystemStats::getNumCpus()) + " cores)");
        const auto pool = real ? buildRealPool (scanner) : buildPool (effect, scanner);
        if (recordingOnly)
        {
            recordingStress (pool, real);
            pitchCorrection (pool);
            say ("\nPASS: recording and pitch-correction checks held");
            watchdog = nullptr;
            return 0;
        }
        if (liveOnly)
        {
            liveMonitoring (pool, real);
            parameterLinking (pool);
            say ("\nPASS: live monitoring checks held");
            watchdog = nullptr;
            return 0;
        }
        // First each effect removed and deleted on its own; a real plug-in that crashes inside itself when deleted is
        // left out of the sections that load and remove effects all the time.
        auto working = pool;
        {
            const auto crashing = removingEffects (pool);
            require (real || crashing.isEmpty(), "The test effects are removed and deleted without a problem");
            if (! crashing.isEmpty())
            {
                working = {};
                working.openGlWindows = pool.openGlWindows;
                for (size_t e = 0; e < pool.effects.size(); ++e)
                    if (! crashing.contains (pool.names[(int) e])) { working.effects.push_back (pool.effects[e]); working.names.add (pool.names[(int) e]); }
            }
        }
        bigProject (working, real ? 10.0 : 20.0);
        // With real plug-ins: five different random sequences, each given up to 8 minutes.
        juce::StringArray failedSequences;
        if (real)
        {
            std::vector<std::pair<juce::int64, juce::String>> failed;
            for (int i = 0; i < 5; ++i)
            {
                const juce::String format = i % 2 == 0 ? "VST3" : "AudioUnit";
                if (! editsInOwnProcess (working, seed + i, 480.0, format)) failed.push_back ({ seed + i, format });
            }
            // A sequence that crashed inside a plug-in, again without the Chowdhury plug-ins (every crash so far was in
            // one of their message queues, or FILT-R's): does the crash follow them?
            if (! failed.empty())
            {
                Pool withoutChow;
                withoutChow.openGlWindows = working.openGlWindows;
                juce::StringArray leftOut;
                for (size_t e = 0; e < working.effects.size(); ++e)
                {
                    const auto& d = working.effects[e];
                    if (d.manufacturerName.containsIgnoreCase ("chowdsp") || d.name.startsWithIgnoreCase ("chow") || d.name.containsIgnoreCase ("BYOD"))
                        leftOut.addIfNotAlreadyThere (d.name);
                    else { withoutChow.effects.push_back (d); withoutChow.names.add (working.names[(int) e]); }
                }
                say ("\nThe sequences that failed, again without the Chowdhury plug-ins (" + leftOut.joinIntoString (", ") + "):");
                for (const auto& [failedSeed, format] : failed)
                {
                    const bool passed = editsInOwnProcess (withoutChow, failedSeed, 480.0, format);
                    say ("Seed " + juce::String (failedSeed) + " (" + format + ") without them: " + (passed ? "passed" : "failed too"));
                    failedSequences.add ("seed " + juce::String (failedSeed) + " (" + format + (passed ? "; passed without the Chowdhury plug-ins)" : "; failed without them too)"));
                }
            }
        }
        else if (std::getenv ("TRIAD_STRESS_EDITS_IN_CHILD") != nullptr)   // to check the child-process path without real plug-ins
            require (editsInOwnProcess (working, seed, 0.0, "VST3"), "Random edits in their own process");
        else
            editsWhilePlaying (working, seed, 3000, 0.0);
        manyInstances (working);
        liveMonitoring (working, real);
        parameterLinking (pool);
        recordingStress (working, real);
        pitchCorrection (pool);
        pluginList (effect, crashFixture, scanner);
        step ({}, 0);
        require (failedSequences.isEmpty(), "Random edits with real plug-ins crashed or failed a check: " + failedSequences.joinIntoString (", "));
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
