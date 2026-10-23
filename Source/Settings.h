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

    // The LFO shape generator's sliders (0..1) and the preset palette's category.
    void getGenerator (float& angular, float& soft, float& chaos) const;
    void setGenerator (float angular, float soft, float chaos);
    int getPaletteCategory() const;
    void setPaletteCategory (int);
    int getDiceSource (const juce::String& tasteKey, int fallback) const;   // which presets each dice draws from
    void setDiceSource (const juce::String& tasteKey, int source);
    int getTimePaletteCategory (int fallback) const;   // the folder open for Triad Time's TIME envelope
    void setTimePaletteCategory (int);

    // Triad Time's pitch labels on the TIME grid's straight lines ("FWD -12 st", "REV 0 st", "FREEZE"): off, while a
    // point is dragged (the default), or always.
    enum class PitchLabels { off = 0, dragging = 1, always = 2 };
    PitchLabels getPitchLabels() const;
    void setPitchLabels (PitchLabels);
    // Behind the LFO grid (0.15.0): nothing, the sound as a waveform across the LFO's cycle, or its spectrum (the
    // band's range lit in its colour). One choice for bands on TRIGGER: AUDIO (waveform to start with), one for SONG
    // (off to start with); Triad Time's grid has its own (off to start with: the waveform or the spectrum, as Triad's).
    enum class Backdrop { off = 0, waveform = 1, spectrum = 2 };
    Backdrop getLfoBackdrop (bool audioTrigger) const;
    void setLfoBackdrop (bool audioTrigger, Backdrop);
    Backdrop getTimeBackdrop() const;
    void setTimeBackdrop (Backdrop);

    // The colour theme (Theme.h), shared by Triad, Triad Time and Triad Split: its name ("Triad", "Midnight" ...,
    // "Custom", or "My: <name>" for one saved in themeFolder()) and the Custom theme's colours (as text).
    juce::String getThemeName() const;
    juce::String getCustomTheme() const;
    void setTheme (const juce::String& name, const juce::String& customText);   // customText empty: keep the stored one

    // The preset every new Triad opens with: "" = Init (an empty Triad), a saved preset's name, or
    // currentSettings (the settings saved with "Use the current settings as the startup preset").
    static constexpr const char* currentSettings = "*current*";
    juce::String getStartupPreset() const;
    void setStartupPreset (const juce::String&);
    static juce::File startupPresetFile (const juce::String& choice);   // {} for Init
    static juce::File startupSettingsFile();                            // ~/Music/Triad/Startup settings.triadpreset

    static juce::String favouriteKey (const juce::String& format, const juce::String& name);
    bool isFavourite (const juce::String& key) const;
    void setFavourite (const juce::String& key, bool);
    juce::StringArray getFavourites() const;

    static juce::File presetFolder();        // ~/Music/Triad/Presets
    static juce::File chainFolder();         // ~/Music/Triad/Chains
    static juce::File lfoShapeFolder();      // ~/Music/Triad/LFO Shapes (the LFO palette's USER folder)
    static juce::File favouriteShapesFile(); // ~/Music/Triad/Favorites.triadfavs (the hearts: the FAVORITES folder)
    static juce::File themeFolder();         // ~/Music/Triad/Themes (saved colour themes, .triadtheme)

private:
    void reload() const;
    juce::PropertiesFile::Options options() const;
    mutable std::unique_ptr<juce::PropertiesFile> file;
    mutable juce::Time loadedModification;
    JUCE_DECLARE_NON_COPYABLE (TriadSettings)
};
