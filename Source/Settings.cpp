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
