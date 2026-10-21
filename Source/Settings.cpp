#include "Settings.h"

TriadSettings::TriadSettings() { reload(); }

juce::PropertiesFile::Options TriadSettings::options() const
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Triad";
    o.filenameSuffix = ".settings";
    o.folderName = "Triad";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 0;
    return o;
}
void TriadSettings::reload() const
{
    // Another Triad (in this or another DAW) may have changed the file: pick up its changes.
    const auto path = options().getDefaultFile();
    const auto modified = path.getLastModificationTime();
    if (file != nullptr && modified == loadedModification) return;
    file = std::make_unique<juce::PropertiesFile> (options());
    loadedModification = modified;
}

TriadSettings::Sort TriadSettings::getSort() const
{
    reload();
    return file->getValue ("sort") == "az" ? Sort::alphabetical : Sort::company;
}
void TriadSettings::setSort (Sort s)
{
    reload();
    file->setValue ("sort", s == Sort::alphabetical ? "az" : "company");
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
bool TriadSettings::getAutoCheck() const
{
    reload();
    return file->getBoolValue ("autoCheck", true);
}
void TriadSettings::setAutoCheck (bool on)
{
    reload();
    file->setValue ("autoCheck", on);
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
double TriadSettings::getScale() const
{
    reload();
    return juce::jlimit (0.75, 1.5, file->getDoubleValue ("scale", 1.0));
}
void TriadSettings::setScale (double scale)
{
    reload();
    file->setValue ("scale", juce::jlimit (0.75, 1.5, scale));
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
juce::StringArray TriadSettings::getFolders() const
{
    reload();
    juce::StringArray list;
    list.addLines (file->getValue ("folders"));
    list.removeEmptyStrings();
    return list;
}
void TriadSettings::setFolders (const juce::StringArray& folders)
{
    reload();
    file->setValue ("folders", folders.joinIntoString ("\n"));
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
void TriadSettings::getGenerator (float& angular, float& soft, float& chaos) const
{
    reload();
    juce::StringArray parts;
    parts.addTokens (file->getValue ("generator", "1,0,0.35"), ",", "");
    auto part = [&parts] (int i, float fallback)
    {
        return parts.size() > i && parts[i].trim().containsOnly ("0123456789.") && parts[i].isNotEmpty()
                   ? juce::jlimit (0.0f, 1.0f, parts[i].getFloatValue()) : fallback;
    };
    angular = part (0, 1.0f);
    soft = part (1, 0.0f);
    chaos = part (2, 0.35f);
}
void TriadSettings::setGenerator (float angular, float soft, float chaos)
{
    reload();
    auto text = [] (float v) { return juce::String (juce::jlimit (0.0f, 1.0f, v), 2); };
    file->setValue ("generator", text (angular) + "," + text (soft) + "," + text (chaos));
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
int TriadSettings::getPaletteCategory() const
{
    reload();
    return file->getIntValue ("lfoPalette", 0);
}
int TriadSettings::getTimePaletteCategory (int fallback) const
{
    reload();
    return file->getIntValue ("timePalette", fallback);
}
void TriadSettings::setTimePaletteCategory (int category)
{
    reload();
    file->setValue ("timePalette", category);
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
void TriadSettings::setPaletteCategory (int category)
{
    reload();
    file->setValue ("lfoPalette", category);
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
TriadSettings::PitchLabels TriadSettings::getPitchLabels() const
{
    reload();
    return (PitchLabels) juce::jlimit (0, 2, file->getIntValue ("timePitchLabels", (int) PitchLabels::dragging));
}
void TriadSettings::setPitchLabels (PitchLabels mode)
{
    reload();
    file->setValue ("timePitchLabels", (int) mode);
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
juce::String TriadSettings::favouriteKey (const juce::String& format, const juce::String& name)
{
    return (format == "AudioUnit" ? juce::String ("AU") : format) + "|" + name.trim().toLowerCase();
}
juce::StringArray TriadSettings::getFavourites() const
{
    reload();
    juce::StringArray list;
    list.addLines (file->getValue ("favourites"));
    list.removeEmptyStrings();
    return list;
}
bool TriadSettings::isFavourite (const juce::String& key) const { return getFavourites().contains (key); }
void TriadSettings::setFavourite (const juce::String& key, bool on)
{
    auto list = getFavourites();
    if (on) list.addIfNotAlreadyThere (key);
    else list.removeString (key);
    file->setValue ("favourites", list.joinIntoString ("\n"));
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
juce::File TriadSettings::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Triad/Presets");
}
juce::File TriadSettings::chainFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Triad/Chains");
}
juce::String TriadSettings::getStartupPreset() const
{
    reload();
    return file->getValue ("startupPreset", {});
}
void TriadSettings::setStartupPreset (const juce::String& choice)
{
    reload();
    file->setValue ("startupPreset", choice);
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
juce::File TriadSettings::startupSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Triad/Startup settings.triadpreset");
}
juce::File TriadSettings::startupPresetFile (const juce::String& choice)
{
    if (choice.isEmpty()) return {};
    if (choice == currentSettings) return startupSettingsFile();
    return presetFolder().getChildFile (choice + ".triadpreset");
}
juce::File TriadSettings::lfoShapeFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Triad/LFO Shapes");
}
juce::File TriadSettings::themeFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Triad/Themes");
}
juce::String TriadSettings::getThemeName() const
{
    reload();
    return file->getValue ("theme", "Triad");
}
juce::String TriadSettings::getCustomTheme() const
{
    reload();
    return file->getValue ("themeCustom");
}
void TriadSettings::setTheme (const juce::String& name, const juce::String& customText)
{
    reload();
    file->setValue ("theme", name);
    if (customText.isNotEmpty()) file->setValue ("themeCustom", customText);
    file->saveIfNeeded();
    loadedModification = options().getDefaultFile().getLastModificationTime();
}
