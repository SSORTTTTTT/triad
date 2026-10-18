#pragma once
#include "CrossoverDisplay.h"
#include "BandStrip.h"
#include "LfoPage.h"
#include "MatrixPage.h"
#include "PluginCatalog.h"
#include "Settings.h"

using TriadLook = triad::ui::TriadLook;

// A small "Save as" panel shown inside Triad's own window (typing works in every DAW).
class NamePrompt final : public juce::Component
{
public:
    NamePrompt();
    void show (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> onSave);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::TextEditor text;
private:
    void finish (bool save);
    juce::TextButton saveButton { "SAVE" }, cancelButton { "CANCEL" };
    juce::String title;
    std::function<void (const juce::String&)> callback;
};

class TriadEditor final : public juce::AudioProcessorEditor, public StripHost,
                          private juce::Timer, private juce::ChangeListener
{
public:
    static constexpr int logicalWidth = 1000, logicalHeight = 620;
    enum class Tab { bands = 0, lfo, matrix };

    explicit TriadEditor (TriadProcessor&);
    ~TriadEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    // StripHost
    void chooseEffect (int band, int slot, juce::Component& target) override;
    void setCrossoverFrequency (int index, float hz) override;
    void showLfo (int band, int slot) override;
    void showBandMenu (int band, juce::Component& target, bool atMouse = false) override;
    // A choice from the band menu (its item ids): copy/paste band state, chains, LFOs, move, remove.
    void bandMenuChoice (int band, int choice, const juce::Array<juce::File>& chainFiles = {});
    enum BandMenuItem { bandMenuCopy = 7, bandMenuPaste = 8, bandMenuPastePlugin = 9 };
    void bandDrag (int band, const juce::MouseEvent&, int phase) override;
    bool isFavourite (const juce::String& format, const juce::String& name) override;
    void setFavourite (const juce::String& format, const juce::String& name, bool) override;

    void setTab (Tab);
    Tab getTab() const noexcept { return tab; }
    void setScale (double);
    double getScale() const noexcept { return scale; }
    CrossoverDisplay& getDisplay() noexcept { return frequencyDisplay; }
    LfoPage& getLfoPage() noexcept { return lfoPage; }
    MatrixPage& getMatrixPage() noexcept { return matrixPage; }
    int getNumStrips() const noexcept { return (int) strips.size(); }
    BandStrip* getStrip (int band) const
    {
        if (band == TriadProcessor::preBand) return preStrip.get();
        return band >= 0 && band < (int) strips.size() ? strips[(size_t) band].get() : nullptr;
    }
    // The PRE chain's strip, left of the bands: shown while PRE has effects, or after the PRE button opened it.
    bool isPreShown() const noexcept { return preStrip != nullptr && preStrip->isVisible(); }
    void togglePre();
    void showPreMenu (juce::Component& target, bool atMouse);
    juce::String getStatusText() const { return status.getText(); }
    void refreshNow() { timerCallback(); animate (lastFrame + 0.05); }
    // Every frame of the screen (up to 60 a second): meters, CPU, the LFO dots on the knobs.
    void animate (double frameTime);
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File&);
    // The startup preset (settings): "" = Init, a preset name, or TriadSettings::currentSettings.
    void setStartupPreset (const juce::String& choice);
    bool useCurrentSettingsAtStartup();   // saves the current settings and opens new Triads with them
    bool saveChain (int band, const juce::String& name);
    // The back / forward arrows left of the gear.
    void undo();
    void redo();
    triad::ui::IconButton& getUndoButton() noexcept { return undoButton; }
    triad::ui::IconButton& getRedoButton() noexcept { return redoButton; }
    bool loadChain (int band, const juce::File&);

private:
    class Content final : public juce::Component
    {
    public:
        explicit Content (TriadEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
    private:
        TriadEditor& owner;
    };
    class TabButton final : public juce::Button
    {
    public:
        explicit TabButton (const juce::String& name) : Button (name) { setRadioGroupId (1001); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };
    class PresetButton final : public juce::Button
    {
    public:
        PresetButton() : Button ("Presets") { setTooltip ("Save and load whole-Triad presets"); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
        juce::String presetName;
    };

    void paintContent (juce::Graphics&);
    void layoutContent();
    void timerCallback() override;
    double lastFrame = 0.0;
    juce::VBlankAttachment frames { this, [this] (double t) { animate (t); } };
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildStrips();
    void layoutStrips();
    void browse (int band, int slot);
    void showPresetMenu();
    void showGearMenu();
    void addPluginFolder();

    TriadProcessor& processor;
    TriadLook look;
    juce::SharedResourcePointer<PluginCatalog> catalog;
    juce::SharedResourcePointer<TriadSettings> settings;
    Content content { *this };
    TabButton bandsTab { "BANDS" }, lfoTab { "LFO" }, matrixTab { "MATRIX" };
    std::array<TabButton*, 3> tabButtons { &bandsTab, &lfoTab, &matrixTab };
    PresetButton presetButton;
    triad::ui::IconButton gearButton { triad::ui::IconButton::Icon::gear, "Settings: effect list, window size, presets folder" };
    triad::ui::IconButton undoButton { triad::ui::IconButton::Icon::undo, "Undo" }, redoButton { triad::ui::IconButton::Icon::redo, "Redo" };
    juce::Component bandsPage;
    CrossoverDisplay frequencyDisplay;
    juce::Component stripHolder;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<BandStrip>> strips;
    std::unique_ptr<BandStrip> preStrip;
    bool preOpened = false;
    int stripOrigin() const;   // where the band strips start (after the PRE strip, when it is shown)
    void updatePre();
    LfoPage lfoPage;
    MatrixPage matrixPage;
    juce::Label status;
    NamePrompt prompt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };
    Tab tab = Tab::bands;
    double scale = 1.0;
    int builtCount = -1;
    struct Drag { int band = -1; float startMouse = 0.0f; int startX = 0; bool active = false; int target = -1; } drag;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadEditor)
};
