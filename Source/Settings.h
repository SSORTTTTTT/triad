#pragma once
#include <juce_data_structures/juce_data_structures.h>

// Triad's preferences, shared by every Triad in every DAW on this computer:
// ~/Library/Application Support/Triad/Triad.settings (macOS).
class TriadSettings final
{
public:
    TriadSettings();

    enum class Sort { company, alphabetical };
    Sort getSort() const;
    void setSort (Sort);
    bool getAutoCheck() const;               // look for new plug-ins each time Triad opens
    void setAutoCheck (bool);
    double getScale() const;                 // window size, 0.75 .. 1.5
    void setScale (double);
    juce::StringArray getFolders() const;    // extra VST3 folders to scan
    void setFolders (const juce::StringArray&);

    static juce::String favouriteKey (const juce::String& format, const juce::String& name);
    bool isFavourite (const juce::String& key) const;
    void setFavourite (const juce::String& key, bool);
    juce::StringArray getFavourites() const;

    static juce::File presetFolder();        // ~/Music/Triad/Presets
    static juce::File chainFolder();         // ~/Music/Triad/Chains

private:
    void reload() const;
    juce::PropertiesFile::Options options() const;
    mutable std::unique_ptr<juce::PropertiesFile> file;
    mutable juce::Time loadedModification;
    JUCE_DECLARE_NON_COPYABLE (TriadSettings)
};
