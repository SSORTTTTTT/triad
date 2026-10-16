#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// One installed effect the user can pick.
struct EffectEntry
{
    juce::PluginDescription description;
    juce::String name, manufacturer, format;   // format: "VST3" or "AU"
    bool loadByFile = false;                    // load the bundle with TriadProcessor::loadFile (name picks the plug-in)
    bool verified = true;                       // false: could not be checked (listed separately)
    bool crashed = false;                       // the plug-in crashed Triad's scanner: not offered
};

// Process-wide list of installed *effect* plug-ins; instruments (Omnisphere, Battery, ...) are excluded.
//
// Nothing is ever loaded into the DAW's process to build this list:
//  - Audio Units: macOS states each component's type, so they are listed without loading.
//  - VST3 with a moduleinfo.json: the file is read directly.
//  - VST3 without one: an Audio Unit of the same name tells effect from instrument; otherwise
//    "Triad Scanner", a small helper program inside Triad's bundle, opens the plug-in in its own
//    process (like a DAW's plug-in scanner). If a plug-in crashes or hangs there, only the helper
//    is affected: the plug-in is marked and skipped, and the DAW keeps running.
// Work runs on a background thread; results are cached on disk, so later sessions only look at
// plug-ins that are new or have changed.
class PluginCatalog final : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    struct Options
    {
        juce::Array<juce::File> vst3Folders;
        juce::File cacheFolder;
        juce::File scanner;                 // the helper; empty = look inside Triad's own bundle
        bool includeAudioUnits = true;
        int timeoutMs = 30000;              // per plug-in, for the helper
    };
    static Options defaultOptions();
    static juce::File findScanner();        // Contents/Helpers/Triad Scanner next to this binary

    PluginCatalog() : PluginCatalog (defaultOptions()) {}
    explicit PluginCatalog (Options);
    ~PluginCatalog() override;

    std::vector<EffectEntry> getEffects() const;   // sorted: VST3, then AU; by maker and name
    bool isScanning() const noexcept { return scanning.load(); }
    juce::String getProgress() const;
    void refresh();                        // pick up new, changed or removed plug-ins
    void rescanAll();                      // forget the cache (including plug-ins that crashed)
    bool waitUntilIdle (int timeoutMs);    // tests: true once a scan pass has finished

    struct Counts { int bundles = 0, fromModuleInfo = 0, fromAudioUnit = 0, fromScanner = 0, crashed = 0, timedOut = 0, unchecked = 0; };
    Counts getCounts() const;

private:
    enum class Status { ok, crashed, timedOut, unavailable };
    enum class Source { moduleInfo, audioUnit, scanner };
    struct FileRecord
    {
        juce::int64 modified = 0;
        Status status = Status::ok;
        Source source = Source::moduleInfo;
        int instruments = 0;
        std::vector<EffectEntry> effects;
        juce::String name, maker;          // for plug-ins that could not be checked
    };
    void run() override;
    void scanPass (bool forget);
    void listAudioUnits();
    FileRecord classify (const juce::File& bundle);
    bool readModuleInfo (const juce::File& bundle, FileRecord&) const;
    void runScanner (const juce::File& bundle, FileRecord&);
    void collectBundles (const juce::File& folder, int depth, juce::Array<juce::File>&) const;
    void loadCache();
    void saveCache() const;
    void publish();
    juce::File cacheFile() const { return options.cacheFolder.getChildFile ("PluginCache-v2.xml"); }

    const Options options;
    juce::File scanner;
    // Owned by the scan thread.
    std::map<juce::String, std::pair<int, juce::String>> auKinds;   // normalised AU name -> (1 effect / 2 instrument, maker)
    std::vector<EffectEntry> auEffects;
    std::map<juce::String, FileRecord> records;                    // keyed by bundle path
    bool cacheLoaded = false;
    // Shared with other threads.
    mutable juce::CriticalSection lock;
    std::vector<EffectEntry> effects;
    Counts counts;
    juce::String current;
    int processed = 0, queued = 0;
    std::atomic<bool> scanning { true }, passRequested { true }, forgetRequested { false };
    std::atomic<int> passesFinished { 0 };
};
