#include "PluginCatalog.h"
#include "AudioUnitList.h"
#include "TriadIdentity.h"
#include "Settings.h"

#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <spawn.h>
 #include <signal.h>
 #include <fcntl.h>
 #include <sys/wait.h>
 #include <cerrno>
 extern char** environ;
#endif

namespace
{
constexpr std::uint32_t fourCharCode (const char (&s)[5])
{
    return ((std::uint32_t) (unsigned char) s[0] << 24) | ((std::uint32_t) (unsigned char) s[1] << 16)
         | ((std::uint32_t) (unsigned char) s[2] << 8) | (std::uint32_t) (unsigned char) s[3];
}
constexpr auto auEffectType = fourCharCode ("aufx"), auMusicEffectType = fourCharCode ("aumf"),
               auInstrumentType = fourCharCode ("aumu"), auGeneratorType = fourCharCode ("augn");

// Same encoding as JUCE's AudioUnit identifiers ("AudioUnit:Effects/aufx,subt,manu").
juce::String fourCC (std::uint32_t code)
{
    juce::String s;
    for (int shift : { 24, 16, 8, 0 })
        s += juce::String::charToString ((juce::juce_wchar) ((code >> shift) & 0xff));
    return s;
}
juce::String normalise (const juce::String& text)
{
    juce::String out;
    auto p = text.toLowerCase().getCharPointer();
    while (! p.isEmpty())
    {
        const auto c = p.getAndAdvance();
        if (juce::CharacterFunctions::isLetterOrDigit (c)) out += juce::String::charToString (c);
    }
    return out;
}
bool looksLikeInstrument (const juce::String& subCategories)
{
    return subCategories.containsIgnoreCase ("Instrument") || subCategories.containsIgnoreCase ("Synth")
        || subCategories.containsIgnoreCase ("Sampler") || subCategories.containsIgnoreCase ("Drum");
}
const char* statusName (int s) { return s == 1 ? "crashed" : s == 2 ? "timeout" : s == 3 ? "unavailable" : "ok"; }

enum class HelperOutcome { exited, crashed, timedOut, cannotStart, cancelled };

// Runs the scanner helper as a separate process, waiting at most timeoutMs. Its output goes to
// /dev/null; the result is the file it writes. Killed on timeout or when Triad closes.
HelperOutcome runHelper (const juce::File& exe, const juce::StringArray& args, int timeoutMs, const std::function<bool()>& cancelled)
{
   #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    std::vector<std::string> storage { exe.getFullPathName().toStdString() };
    for (auto& a : args) storage.push_back (a.toStdString());
    std::vector<char*> argv;
    for (auto& s : storage) argv.push_back (s.data());
    argv.push_back (nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_addopen (&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen (&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen (&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attributes;
    posix_spawnattr_init (&attributes);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
   #if JUCE_MAC
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;   // do not hand the DAW's open files to the helper
   #endif
    sigset_t none, all;
    sigemptyset (&none); sigfillset (&all);
    posix_spawnattr_setsigmask (&attributes, &none);
    posix_spawnattr_setsigdefault (&attributes, &all);
    posix_spawnattr_setflags (&attributes, flags);
    pid_t pid = 0;
    const int started = posix_spawn (&pid, storage[0].c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy (&actions);
    posix_spawnattr_destroy (&attributes);
    if (started != 0) return HelperOutcome::cannotStart;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    for (;;)
    {
        int status = 0;
        const auto r = waitpid (pid, &status, WNOHANG);
        if (r == pid) return WIFSIGNALED (status) ? HelperOutcome::crashed : HelperOutcome::exited;
        if (r < 0 && errno != EINTR) return HelperOutcome::exited;   // reaped by the host: judge by its result file
        const bool stop = cancelled();
        if (stop || juce::Time::getMillisecondCounterHiRes() > deadline)
        {
            kill (pid, SIGKILL);
            while (waitpid (pid, &status, 0) < 0 && errno == EINTR) {}
            return stop ? HelperOutcome::cancelled : HelperOutcome::timedOut;
        }
        juce::Thread::sleep (10);
    }
   #else
    juce::ChildProcess child;
    juce::StringArray all { exe.getFullPathName() };
    all.addArray (args);
    if (! child.start (all, 0)) return HelperOutcome::cannotStart;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (child.isRunning())
    {
        const bool stop = cancelled();
        if (stop || juce::Time::getMillisecondCounterHiRes() > deadline)
        {
            child.kill();
            return stop ? HelperOutcome::cancelled : HelperOutcome::timedOut;
        }
        juce::Thread::sleep (10);
    }
    return child.getExitCode() == 0 ? HelperOutcome::exited : HelperOutcome::crashed;
   #endif
}
}

PluginCatalog::Options PluginCatalog::defaultOptions()
{
    Options o;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    o.vst3Folders.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    o.vst3Folders.add (home.getChildFile ("Library/Audio/Plug-Ins/VST3"));
    o.cacheFolder = appData.getChildFile ("Application Support/Triad");
   #elif JUCE_WINDOWS
    o.vst3Folders.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
    o.cacheFolder = appData.getChildFile ("Triad");
   #else
    o.vst3Folders.add (juce::File ("/usr/lib/vst3"));
    o.vst3Folders.add (juce::File ("/usr/local/lib/vst3"));
    o.vst3Folders.add (home.getChildFile (".vst3"));
    o.cacheFolder = appData.getChildFile ("Triad");
   #endif
    const TriadSettings settings;
    for (const auto& folder : settings.getFolders()) o.vst3Folders.addIfNotAlreadyThere (juce::File (folder));
    o.autoCheck = settings.getAutoCheck();
    return o;
}
juce::File PluginCatalog::findScanner()
{
    // .../Triad v2.vst3/Contents/MacOS/Triad v2  ->  .../Triad v2.vst3/Contents/Helpers/Triad Scanner
    const auto binary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_WINDOWS
    return binary.getParentDirectory().getParentDirectory().getChildFile ("Helpers/Triad Scanner.exe");
   #else
    return binary.getParentDirectory().getParentDirectory().getChildFile ("Helpers/Triad Scanner");
   #endif
}

PluginCatalog::PluginCatalog (Options o)
    : Thread ("Triad plug-in list"), options (std::move (o))
{
    scanner = options.scanner != juce::File() ? options.scanner : findScanner();
    startThread (juce::Thread::Priority::background);
}
PluginCatalog::~PluginCatalog()
{
    signalThreadShouldExit();
    notify();
    stopThread (10000);   // a running helper is killed within ~10 ms
}

void PluginCatalog::run()
{
    while (! threadShouldExit())
    {
        if (passRequested.exchange (false))
        {
            scanning = true;
            {
                const juce::ScopedLock sl (lock);
                active = options;
            }
            // Without "check for new plug-ins", the saved list is used as it is until Rescan is pressed.
            const bool cacheOnly = ! forcedPass.exchange (false) && ! active.autoCheck && cacheFile().existsAsFile();
            scanPass (forgetRequested.exchange (false), cacheOnly);
            if (threadShouldExit()) break;
            if (! passRequested.load())
            {
                scanning = false;
                ++passesFinished;
                sendChangeMessage();
            }
            continue;
        }
        wait (-1);
    }
}
void PluginCatalog::configure (const juce::StringArray& extraFolders, bool autoCheck)
{
    {
        const juce::ScopedLock sl (lock);
        auto defaults = defaultOptions();
        options.vst3Folders = defaults.vst3Folders;
        for (const auto& folder : extraFolders) options.vst3Folders.addIfNotAlreadyThere (juce::File (folder));
        options.autoCheck = autoCheck;
    }
    refresh();
}
void PluginCatalog::refresh()
{
    forcedPass = true;
    scanning = true;
    passRequested = true;
    notify();
}
void PluginCatalog::rescanAll()
{
    forgetRequested = true;
    refresh();
}
bool PluginCatalog::waitUntilIdle (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (scanning.load() || passRequested.load())
    {
        if (juce::Time::getMillisecondCounterHiRes() > deadline) return false;
        juce::Thread::sleep (20);   // the scan thread never needs the message thread
    }
    return true;
}

void PluginCatalog::scanPass (bool forget, bool cacheOnly)
{
    if (forget) { records.clear(); cacheFile().deleteFile(); cacheLoaded = true; }
    if (! cacheLoaded) { loadCache(); cacheLoaded = true; }
    if (cacheOnly)
    {
        listAudioUnits();
        publish();
        return;
    }
    // Triad 0.2.1's list could load plug-ins inside the DAW; its files are no longer used.
    options.cacheFolder.getChildFile ("PluginCache.xml").deleteFile();
    options.cacheFolder.getChildFile ("ScanInProgress.txt").deleteFile();
    listAudioUnits();
    juce::Array<juce::File> queue;
    for (const auto& folder : active.vst3Folders)
        if (folder.isDirectory()) collectBundles (folder, 0, queue);
    for (auto it = records.begin(); it != records.end();)   // forget uninstalled plug-ins
        it = juce::File (it->first).exists() ? std::next (it) : records.erase (it);
    {
        const juce::ScopedLock sl (lock);
        processed = 0;
        queued = queue.size();
    }
    publish();
    auto lastPublish = juce::Time::getMillisecondCounterHiRes();
    int sinceSave = 0;
    for (const auto& bundle : queue)
    {
        if (threadShouldExit() || passRequested.load()) break;
        const auto path = bundle.getFullPathName();
        const auto modified = bundle.getLastModificationTime().toMilliseconds();
        {
            const juce::ScopedLock sl (lock);
            ++processed;
            current = bundle.getFileNameWithoutExtension();
        }
        // Unchanged since last time (including plug-ins that crashed the scanner): keep the result.
        if (auto found = records.find (path); found != records.end() && found->second.modified == modified
                                               && found->second.status != Status::unavailable)
            continue;
        auto record = classify (bundle);
        if (threadShouldExit()) break;
        record.modified = modified;
        const bool helperUsed = record.source == Source::scanner;
        records[path] = std::move (record);
        if (helperUsed && ++sinceSave >= 5) { saveCache(); sinceSave = 0; }   // keep progress if Triad closes
        if (juce::Time::getMillisecondCounterHiRes() - lastPublish > 500.0)
        {
            publish();
            lastPublish = juce::Time::getMillisecondCounterHiRes();
        }
    }
    {
        const juce::ScopedLock sl (lock);
        current.clear();
    }
    saveCache();
    publish();
}

void PluginCatalog::listAudioUnits()
{
    auKinds.clear();
    auEffects.clear();
    if (! active.includeAudioUnits) return;
    for (const auto& c : triad::au::listComponents())
    {
        const bool effect = c.type == auEffectType || c.type == auMusicEffectType;
        const bool instrument = c.type == auInstrumentType || c.type == auGeneratorType;
        if (! effect && ! instrument) continue;
        const auto name = juce::String::fromUTF8 (c.name.c_str());
        const auto maker = juce::String::fromUTF8 (c.maker.c_str());
        for (const auto& key : { normalise (name), normalise (maker + name) })
            if (key.isNotEmpty())
            {
                auto& k = auKinds[key];   // an instrument of the same name wins
                k = { juce::jmax (k.first, instrument ? 2 : 1), maker };
            }
        if (! effect || triad::isTriadName (name)) continue;
        EffectEntry e;
        e.name = name;
        e.manufacturer = maker.isNotEmpty() ? maker : fourCC (c.manufacturer).trim();
        e.format = "AU";
        auto& d = e.description;
        d.name = d.descriptiveName = name;
        d.manufacturerName = e.manufacturer;
        d.pluginFormatName = "AudioUnit";
        d.category = "Effect";
        d.fileOrIdentifier = "AudioUnit:Effects/" + fourCC (c.type) + "," + fourCC (c.subtype) + "," + fourCC (c.manufacturer);
        d.isInstrument = false;
        d.numInputChannels = d.numOutputChannels = 2;
        d.uniqueId = d.deprecatedUid = (int) (c.subtype ^ c.manufacturer);
        auEffects.push_back (std::move (e));
    }
}

void PluginCatalog::collectBundles (const juce::File& folder, int depth, juce::Array<juce::File>& queue) const
{
    if (depth > 4) return;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto f = entry.getFile();
        if (f.hasFileExtension ("vst3")) queue.add (f);
        else if (entry.isDirectory()) collectBundles (f, depth + 1, queue);
    }
}

PluginCatalog::FileRecord PluginCatalog::classify (const juce::File& bundle)
{
    FileRecord record;
    if (readModuleInfo (bundle, record)) { record.source = Source::moduleInfo; return record; }
    const auto base = bundle.getFileNameWithoutExtension();
    if (auto twin = auKinds.find (normalise (base)); twin != auKinds.end())
    {
        record.source = Source::audioUnit;
        if (twin->second.first == 2) { record.instruments = 1; return record; }
        if (triad::isTriadName (base)) return record;
        EffectEntry e;
        e.name = base;
        e.manufacturer = twin->second.second;
        e.format = "VST3";
        e.loadByFile = true;
        e.description.name = e.description.descriptiveName = base;
        e.description.manufacturerName = e.manufacturer;
        e.description.pluginFormatName = "VST3";
        e.description.fileOrIdentifier = bundle.getFullPathName();
        record.effects.push_back (std::move (e));
        return record;
    }
    record.source = Source::scanner;
    runScanner (bundle, record);
    return record;
}

// VST3's moduleinfo.json lists every class with its sub-categories ("Fx", "Instrument|Synth", ...).
bool PluginCatalog::readModuleInfo (const juce::File& bundle, FileRecord& record) const
{
    auto file = bundle.getChildFile ("Contents/Resources/moduleinfo.json");
    if (! file.existsAsFile()) file = bundle.getChildFile ("Contents/moduleinfo.json");
    if (! file.existsAsFile() || file.getSize() > 4 * 1024 * 1024) return false;
    juce::var json;
    if (juce::JSON::parse (file.loadFileAsString(), json).failed()) return false;
    const auto* classes = json.getProperty ("Classes", {}).getArray();
    if (classes == nullptr) return false;
    const auto factoryVendor = json.getProperty ("Factory Info", {}).getProperty ("Vendor", {}).toString();
    bool any = false;
    for (const auto& c : *classes)
    {
        if (c.getProperty ("Category", {}).toString() != "Audio Module Class") continue;
        any = true;
        juce::StringArray subs;
        if (const auto* list = c.getProperty ("Sub Categories", {}).getArray())
            for (const auto& s : *list) subs.add (s.toString());
        else subs.addTokens (c.getProperty ("Sub Categories", {}).toString(), "|", {});
        const auto joined = subs.joinIntoString ("|");
        const auto name = c.getProperty ("Name", {}).toString().trim();
        if (looksLikeInstrument (joined)) { ++record.instruments; continue; }
        if (name.isEmpty() || triad::isTriadName (name)) continue;
        EffectEntry e;
        e.name = name;
        e.manufacturer = c.getProperty ("Vendor", {}).toString().trim();
        if (e.manufacturer.isEmpty()) e.manufacturer = factoryVendor.trim();
        e.format = "VST3";
        e.loadByFile = true;
        auto& d = e.description;
        d.name = d.descriptiveName = name;
        d.manufacturerName = e.manufacturer;
        d.pluginFormatName = "VST3";
        d.category = joined;
        d.version = c.getProperty ("Version", {}).toString();
        d.fileOrIdentifier = bundle.getFullPathName();
        record.effects.push_back (std::move (e));
    }
    return any;
}

void PluginCatalog::runScanner (const juce::File& bundle, FileRecord& record)
{
    record.name = bundle.getFileNameWithoutExtension();
    if (auto twin = auKinds.find (normalise (record.name)); twin != auKinds.end()) record.maker = twin->second.second;
    if (! scanner.existsAsFile()) { record.status = Status::unavailable; return; }
    const auto result = options.cacheFolder.getChildFile ("scan-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
    options.cacheFolder.createDirectory();
    const auto outcome = runHelper (scanner, { "--scan", bundle.getFullPathName(), result.getFullPathName() },
                                    active.timeoutMs, [this] { return threadShouldExit(); });
    const auto xml = result.existsAsFile() ? juce::XmlDocument::parse (result) : nullptr;
    result.deleteFile();
    result.getSiblingFile (result.getFileName() + ".part").deleteFile();
    if (outcome == HelperOutcome::cancelled || outcome == HelperOutcome::cannotStart) { record.status = Status::unavailable; return; }
    if (xml == nullptr || ! xml->hasTagName ("TriadScan"))
    {
        record.status = outcome == HelperOutcome::timedOut ? Status::timedOut : Status::crashed;
        return;
    }
    for (auto* p : xml->getChildWithTagNameIterator ("PLUGIN"))
    {
        juce::PluginDescription d;
        if (! d.loadFromXml (*p)) continue;
        if (d.isInstrument) { ++record.instruments; continue; }
        if (triad::isTriadName (d.name)) continue;
        EffectEntry e;
        e.description = d;
        e.name = d.name;
        e.manufacturer = d.manufacturerName;
        e.format = "VST3";
        record.effects.push_back (std::move (e));
    }
}

void PluginCatalog::publish()
{
    std::vector<EffectEntry> next;
    Counts c;
    for (const auto& [path, r] : records)
    {
        ++c.bundles;
        if (r.source == Source::moduleInfo) ++c.fromModuleInfo;
        else if (r.source == Source::audioUnit) ++c.fromAudioUnit;
        else if (r.status == Status::ok) ++c.fromScanner;
        if (r.status == Status::ok)
        {
            next.insert (next.end(), r.effects.begin(), r.effects.end());
            continue;
        }
        // Not checked: still offered (separately), except plug-ins that crashed the scanner.
        if (r.status == Status::crashed) ++c.crashed;
        else if (r.status == Status::timedOut) ++c.timedOut;
        else ++c.unchecked;
        EffectEntry e;
        e.name = r.name.isNotEmpty() ? r.name : juce::File (path).getFileNameWithoutExtension();
        e.manufacturer = r.maker;
        e.format = "VST3";
        e.loadByFile = true;
        e.verified = false;
        e.crashed = r.status == Status::crashed;
        e.description.name = e.description.descriptiveName = e.name;
        e.description.pluginFormatName = "VST3";
        e.description.fileOrIdentifier = path;
        next.push_back (std::move (e));
    }
    next.insert (next.end(), auEffects.begin(), auEffects.end());
    std::sort (next.begin(), next.end(), [] (const EffectEntry& a, const EffectEntry& b)
    {
        if (a.verified != b.verified) return a.verified;
        if (a.format != b.format) return a.format == "VST3";
        if (const auto d = a.manufacturer.compareIgnoreCase (b.manufacturer); d != 0) return d < 0;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    {
        const juce::ScopedLock sl (lock);
        effects = std::move (next);
        counts = c;
    }
    sendChangeMessage();
}

std::vector<EffectEntry> PluginCatalog::getEffects() const
{
    const juce::ScopedLock sl (lock);
    return effects;
}
PluginCatalog::Counts PluginCatalog::getCounts() const
{
    const juce::ScopedLock sl (lock);
    return counts;
}
juce::String PluginCatalog::getProgress() const
{
    const juce::ScopedLock sl (lock);
    if (scanning.load())
        return "Finding effects... " + juce::String (processed) + " of " + juce::String (queued)
             + (current.isNotEmpty() ? " (" + current + ")" : juce::String());
    int verified = 0;
    for (auto& e : effects) if (e.verified) ++verified;
    return juce::String (verified) + " effects";
}

void PluginCatalog::saveCache() const
{
    juce::XmlElement root ("TriadPluginCache");
    root.setAttribute ("version", 2);
    for (const auto& [path, r] : records)
    {
        if (r.status == Status::unavailable) continue;   // try again next time
        auto* f = root.createNewChildElement ("File");
        f->setAttribute ("path", path);
        f->setAttribute ("modified", juce::String (r.modified));
        f->setAttribute ("status", statusName ((int) r.status));
        f->setAttribute ("source", (int) r.source);
        f->setAttribute ("instruments", r.instruments);
        f->setAttribute ("name", r.name);
        f->setAttribute ("maker", r.maker);
        for (const auto& e : r.effects)
        {
            auto* x = f->createNewChildElement ("Effect");
            x->setAttribute ("name", e.name);
            x->setAttribute ("manufacturer", e.manufacturer);
            x->setAttribute ("byFile", e.loadByFile);
            x->addChildElement (e.description.createXml().release());
        }
    }
    options.cacheFolder.createDirectory();
    root.writeTo (cacheFile());   // written to a temporary file first, then swapped in
}

void PluginCatalog::loadCache()
{
    const auto xml = juce::XmlDocument::parse (cacheFile());
    if (xml == nullptr || ! xml->hasTagName ("TriadPluginCache") || xml->getIntAttribute ("version") != 2) return;
    for (auto* f : xml->getChildWithTagNameIterator ("File"))
    {
        FileRecord r;
        r.modified = f->getStringAttribute ("modified").getLargeIntValue();
        const auto status = f->getStringAttribute ("status");
        r.status = status == "crashed" ? Status::crashed : status == "timeout" ? Status::timedOut : Status::ok;
        r.source = (Source) juce::jlimit (0, 2, f->getIntAttribute ("source"));
        r.instruments = f->getIntAttribute ("instruments");
        r.name = f->getStringAttribute ("name");
        r.maker = f->getStringAttribute ("maker");
        for (auto* x : f->getChildWithTagNameIterator ("Effect"))
        {
            EffectEntry e;
            e.name = x->getStringAttribute ("name");
            e.manufacturer = x->getStringAttribute ("manufacturer");
            e.loadByFile = x->getBoolAttribute ("byFile");
            e.format = "VST3";
            if (auto* p = x->getChildByName ("PLUGIN")) e.description.loadFromXml (*p);
            r.effects.push_back (std::move (e));
        }
        records[f->getStringAttribute ("path")] = std::move (r);
    }
}
