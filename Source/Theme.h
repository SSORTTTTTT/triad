#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "UiCommon.h"
#include "Settings.h"

// Colour themes for Triad, Triad Time and Triad Split (Thomas, 2026-10-21): the background, panels, buttons, grids,
// grid lines, text, accent and Triad Time's two line colours, from a ready-made theme or picked one by one. The band
// colours never change (in Triad the LFO lines keep their band's colour). One theme for all three plug-ins on the
// computer: stored in Triad.settings, followed by every open window within half a second.
namespace triad::ui
{
struct Theme
{
    enum Role { background = 0, panels, buttons, grids, gridLines, text, accent, timeLine, volumeLine, numRoles };
    juce::String name;                                // "Triad", "Midnight" ..., "Custom", "My: <name>"
    std::array<juce::Colour, numRoles> colours {};

    juce::Colour& operator[] (int role) { return colours[(size_t) juce::jlimit (0, numRoles - 1, role)]; }
    juce::Colour operator[] (int role) const { return colours[(size_t) juce::jlimit (0, numRoles - 1, role)]; }
    bool sameColours (const Theme& other) const;
    static juce::String roleName (int role);          // "Background", "Grid lines", "Triad Time: TIME line" ...
    static juce::String roleKey (int role);           // "background", "gridLines" ... (in the text form)
    juce::String toText() const;                      // "background=#252729; panels=#1b1e1c; ..."
    static Theme fromText (const juce::String& text, const juce::String& name);   // roles it lacks: the default's
};

const std::vector<Theme>& builtInThemes();            // the first one is Triad's own (the default)
const Theme& defaultTheme();
Palette paletteFor (const Theme&);                     // every interface colour, worked out from the theme's
float contrast (juce::Colour a, juce::Colour b);       // 1 (the same) .. 21 (black on white), as WCAG measures it
float colourDistance (juce::Colour a, juce::Colour b); // how different two colours look (CIE76 delta E; ~2.3 is just visible)
// The ten band colours are kept for the bands (Thomas): a theme colour must look at least as different from each of
// them as the bands do from each other, and the backgrounds must stay dark enough for every band to show.
constexpr float bandClearance = 20.0f;                 // the closest two band colours are 18 apart
int nearestBand (juce::Colour, float* distance = nullptr);   // 0..9
juce::Colour keepClearOfBands (int role, juce::Colour);       // moved just enough (itself when it is already clear)
Theme keepClearOfBands (Theme);                        // every colour (Triad's own theme is left as it is)
juce::StringArray readabilityProblems (const Theme&);  // "Text is hard to read on the panels" ...
Theme makeReadable (Theme);                            // the same theme with text and lines that can be read / seen
// A random colourway (Thomas: "a random colorway generator that makes a color scheme that doesn't clash"): dark
// backgrounds tinted from one hue, an accent and the two Triad Time lines from a colour scheme around it; clear of the
// band colours, readable, the accent and the lines clearly different from each other, and not a copy of a theme there
// is already. Named "Custom".
Theme randomTheme (juce::Random&);

// The theme of this plug-in now. applyTheme redraws every window at once; with save it is also stored in
// Triad.settings, so the other plug-ins (and the next session) follow.
const Theme& currentTheme();
void applyTheme (const Theme&, bool save);
Theme themeNamed (const juce::String& name);           // a built-in, "Custom" (the stored one), "My: <name>" (a file)
juce::StringArray savedThemeNames();                   // the themes in ~/Music/Triad/Themes, by name
bool saveTheme (const juce::String& name, const Theme&);
void syncThemeFromSettings();                          // follows a theme chosen in another plug-in

// Calls `changed` after every theme change (from this plug-in or, within half a second, another), so a window can
// re-colour what it set up once (its look, its buttons) and redraw.
class ThemeWatcher final : private juce::Timer
{
public:
    explicit ThemeWatcher (std::function<void()> changed);
    ~ThemeWatcher() override;
    void notify() { if (onChange) onChange(); }
private:
    void timerCallback() override { syncThemeFromSettings(); }
    std::function<void()> onChange;
};

// The THEME part of a settings (gear) menu: the ready-made themes, saved ones, Custom, "Edit colours...".
void addThemeItems (juce::PopupMenu&, int firstId);
bool handleThemeChoice (int choice, int firstId, const std::function<void()>& openEditor);   // true: it was a theme item

// Edit colours: laid over a whole window. A row per colour (a swatch, its hex code, back to default) and a colour
// picker for the chosen one; every change shows at once. Start from any theme, make it readable, save it by name.
class ThemePanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    ThemePanel();
    ~ThemePanel() override;
    void open();                                       // over its parent, starting from the current theme
    void close();                                      // DONE: the theme left here is every Triad's from now on
    void cancel();                                     // CANCEL: back to the theme there was when it opened
    void randomise();                                  // RANDOM COLOURWAY
    void selectRole (int role);
    void setRoleColour (int role, juce::Colour);       // as if picked (tests use it)
    const Theme& getEditedTheme() const noexcept { return edited; }
    int getSelectedRole() const noexcept { return selected; }
    juce::String getNote() const { return note; }      // e.g. "moved away from Band 1's colour"
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    struct Row;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void edit (const Theme&, bool fromPicker);
    void refresh();
    juce::Rectangle<int> box() const;
    juce::Rectangle<int> bandChipArea() const;
    Theme edited, before;
    juce::Random random;
    juce::String note;
    int selected = 0;
    bool pendingSave = false, updating = false;
    std::vector<std::unique_ptr<Row>> rows;
    juce::ColourSelector picker { juce::ColourSelector::showColourAtTop | juce::ColourSelector::editableColour
                                  | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace };
    juce::ComboBox startFrom;
    juce::TextEditor saveName;
    juce::TextButton saveButton { "SAVE AS" }, fixButton { "MAKE READABLE" }, doneButton { "DONE" }, cancelButton { "CANCEL" };
    struct RandomButton;
    std::unique_ptr<RandomButton> randomButton;
    juce::Label problems;
};
}
