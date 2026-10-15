#include "PluginCatalog.h"
#include "AudioUnitList.h"

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
bool hasModuleInfo (const juce::File& bundle)
{
    return bundle.getChildFile ("Contents/Resources/moduleinfo.json").existsAsFile()
        || bundle.getChildFile ("Contents/moduleinfo.json").existsAsFile();
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
    return o;
}

PluginCatalog::PluginCatalog (Options o) : options (std::move (o))
{
    beginScan();
}
PluginCatalog::~PluginCatalog()
{
    stopTimer();
    if (scanning) saveCache();
}

void PluginCatalog::beginScan()
{
    if (! cacheLoaded) { loadCache(); cacheLoaded = true; }
    // A crash marker left behind means the plug-in named in it took the host down while being
    // inspected: skip it from now on (until it is updated).
    const auto marker = crashMarker();
    if (marker.existsAsFile())
    {
        const auto path = marker.loadFileAsString().trim();
        if (path.isNotEmpty())
        {
            auto& r = records[path];
            r.blacklisted = true;
            r.effects.clear();
            r.modified = juce::File (path).getLastModificationTime().toMilliseconds();
        }
        marker.deleteFile();
    }
    auKinds.clear();
    auEffects.clear();
    if (options.includeAudioUnits)
    {
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
            if (! effect || name == "Triad") continue;
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
    queue.clear();
    for (const auto& folder : options.vst3Folders)
        if (folder.isDirectory()) collectBundles (folder, 0);
    for (auto it = records.begin(); it != records.end();)   // forget uninstalled plug-ins
        it = juce::File (it->first).exists() ? std::next (it) : records.erase (it);
    processed = 0;
    queued = queue.size();
    scanning = true;
    publish();
    startTimerHz (30);
}

void PluginCatalog::collectBundles (const juce::File& folder, int depth)
{
    if (depth > 4) return;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto f = entry.getFile();
        if (f.hasFileExtension ("vst3")) queue.add (f);
        else if (entry.isDirectory()) collectBundles (f, depth + 1);
    }
}

bool PluginCatalog::step (double budgetMs)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    while (! queue.isEmpty())
    {
        const auto bundle = queue.removeAndReturn (0);
        ++processed;
        const auto path = bundle.getFullPathName();
        const auto modified = bundle.getLastModificationTime().toMilliseconds();
        if (auto found = records.find (path); found != records.end() && found->second.modified == modified)
            continue;   // unchanged since last time (including plug-ins on the crash list)
        current = bundle.getFileNameWithoutExtension();
        FileRecord record;
        record.modified = modified;
        classify (bundle, record);
        records[path] = std::move (record);
        if (juce::Time::getMillisecondCounterHiRes() - start > budgetMs) break;
    }
    return queue.isEmpty();
}

void PluginCatalog::classify (const juce::File& bundle, FileRecord& record)
{
    juce::OwnedArray<juce::PluginDescription> found;
    if (hasModuleInfo (bundle))
        vst3.findAllTypesForFile (found, bundle.getFullPathName());   // reads moduleinfo.json only
    if (found.isEmpty())
    {
        const auto base = bundle.getFileNameWithoutExtension();
        if (auto twin = auKinds.find (normalise (base)); twin != auKinds.end())
        {
            if (twin->second.first == 2) { record.instruments = 1; return; }
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
            return;
        }
        // Last resort: ask the plug-in itself. The marker names it in case it crashes the host.
        options.cacheFolder.createDirectory();
        crashMarker().replaceWithText (bundle.getFullPathName());
        vst3.findAllTypesForFile (found, bundle.getFullPathName());
        crashMarker().deleteFile();
    }
    for (auto* d : found)
    {
        if (d->isInstrument) { ++record.instruments; continue; }
        if (d->name == "Triad") continue;
        EffectEntry e;
        e.description = *d;
        e.name = d->name;
        e.manufacturer = d->manufacturerName;
        e.format = "VST3";
        record.effects.push_back (std::move (e));
    }
}

void PluginCatalog::timerCallback()
{
    const bool finished = step (8.0);
    if (finished)
    {
        stopTimer();
        scanning = false;
        current.clear();
        saveCache();
    }
    publish();
}

void PluginCatalog::publish()
{
    effects.clear();
    for (const auto& [path, r] : records)
        if (! r.blacklisted)
            effects.insert (effects.end(), r.effects.begin(), r.effects.end());
    effects.insert (effects.end(), auEffects.begin(), auEffects.end());
    std::sort (effects.begin(), effects.end(), [] (const EffectEntry& a, const EffectEntry& b)
    {
        if (a.format != b.format) return a.format == "VST3";
        if (const auto c = a.manufacturer.compareIgnoreCase (b.manufacturer); c != 0) return c < 0;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    sendChangeMessage();
}

juce::String PluginCatalog::getProgress() const
{
    if (scanning)
        return "Finding effects... " + juce::String (processed) + " of " + juce::String (queued)
             + (current.isNotEmpty() ? " (" + current + ")" : juce::String());
    return juce::String ((int) effects.size()) + " effects";
}

void PluginCatalog::refresh()
{
    if (! scanning) beginScan();
}

void PluginCatalog::rescanAll()
{
    stopTimer();
    records.clear();
    cacheFile().deleteFile();
    crashMarker().deleteFile();
    beginScan();
}

bool PluginCatalog::scanUntilIdle (int timeoutMs)
{
    stopTimer();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (! step (1000.0) && juce::Time::getMillisecondCounterHiRes() < deadline) {}
    scanning = ! queue.isEmpty();
    if (! scanning) { current.clear(); saveCache(); }
    publish();
    return ! scanning;
}

void PluginCatalog::saveCache() const
{
    juce::XmlElement root ("TriadPluginCache");
    root.setAttribute ("version", 1);
    for (const auto& [path, r] : records)
    {
        auto* f = root.createNewChildElement ("File");
        f->setAttribute ("path", path);
        f->setAttribute ("modified", juce::String (r.modified));
        f->setAttribute ("blacklisted", r.blacklisted);
        f->setAttribute ("instruments", r.instruments);
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
    root.writeTo (cacheFile());
}

void PluginCatalog::loadCache()
{
    const auto xml = juce::XmlDocument::parse (cacheFile());
    if (xml == nullptr || ! xml->hasTagName ("TriadPluginCache")) return;
    for (auto* f : xml->getChildWithTagNameIterator ("File"))
    {
        FileRecord r;
        r.modified = f->getStringAttribute ("modified").getLargeIntValue();
        r.blacklisted = f->getBoolAttribute ("blacklisted");
        r.instruments = f->getIntAttribute ("instruments");
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
