#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// One installed effect the user can pick.
struct EffectEntry
{
    juce::PluginDescription description;
    juce::String name, manufacturer, format;   // format: "VST3" or "AU"
    bool loadByFile = false;                    // only the bundle is known: load it with TriadProcessor::loadFile
};

// Process-wide list of installed *effect* plug-ins; instruments (Omnisphere, etc.) are excluded.
//  - Audio Units: macOS states each component's type, so the list is read without loading anything.
//  - VST3: the bundle's moduleinfo.json (fast, nothing loaded); otherwise an AU of the same name
//    tells effect from instrument; only as a last resort is the plug-in's factory queried, once,
//    guarded by a crash marker so a plug-in that crashes is skipped next time.
// Work runs on the message thread in small time slices (JUCE's VST3 loader is not thread-safe),
// and results are cached on disk so later sessions only check modification dates.
class PluginCatalog final : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    struct Options
    {
        juce::Array<juce::File> vst3Folders;
        juce::File cacheFolder;
        bool includeAudioUnits = true;
    };
    static Options defaultOptions();

    PluginCatalog() : PluginCatalog (defaultOptions()) {}
    explicit PluginCatalog (Options);
    ~PluginCatalog() override;

    const std::vector<EffectEntry>& getEffects() const noexcept { return effects; }
    bool isScanning() const noexcept { return scanning; }
    juce::String getProgress() const;
    void refresh();                        // pick up new, changed or removed plug-ins
    void rescanAll();                      // forget the cache and the crash list
    bool scanUntilIdle (int timeoutMs);    // synchronous scan (tests)

private:
    struct FileRecord
    {
        juce::int64 modified = 0;
        bool blacklisted = false;
        int instruments = 0;
        std::vector<EffectEntry> effects;
    };
    void timerCallback() override;
    bool step (double budgetMs);
    void beginScan();
    void classify (const juce::File& bundle, FileRecord&);
    void collectBundles (const juce::File& folder, int depth);
    void loadCache();
    void saveCache() const;
    void publish();
    juce::File cacheFile() const { return options.cacheFolder.getChildFile ("PluginCache.xml"); }
    juce::File crashMarker() const { return options.cacheFolder.getChildFile ("ScanInProgress.txt"); }

    Options options;
    juce::VST3PluginFormat vst3;
    std::map<juce::String, std::pair<int, juce::String>> auKinds;   // normalised AU name -> (1 effect / 2 instrument, maker)
    std::vector<EffectEntry> auEffects;
    std::map<juce::String, FileRecord> records; // keyed by bundle path
    juce::Array<juce::File> queue;
    std::vector<EffectEntry> effects;
    int processed = 0, queued = 0;
    bool scanning = false, cacheLoaded = false;
    juce::String current;
};
