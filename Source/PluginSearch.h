#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginCatalog.h"
#include "UiCommon.h"

// Plug-in search (Thomas, 0.14.0: "a little search bar where we can search just the plugin name ... similar ... like at
// Ableton when you press Command F"). The magnifying glass at the top of the add / replace effect menu (or just typing
// while that menu is open) lays a search box over Triad's window: type part of a name (or the maker) and the list
// narrows as you type; Up / Down, Return loads, Esc closes. The A-Z and by-company menus stay as they were. No Cmd+F
// (Thomas: it would be confusing next to Ableton's own).
namespace triad::search
{
// The plug-ins matching `query`, best first: the name itself, names starting with it, a word of the name starting with
// it, names containing it (spaces and dashes ignored: "proq3" finds "Pro-Q 3"), then the maker. Several words: each
// must match the name or the maker. Ties: favourites, then A to Z, VST3 before AU. Plug-ins that crashed Triad's
// scanner are left out. An empty query lists every plug-in (favourites first, then A to Z).
std::vector<int> match (const std::vector<EffectEntry>&, const juce::String& query, const std::function<bool (const EffectEntry&)>& isFavourite = {});

// The magnifying-glass item at the top of the effect menu. Typing a letter or digit while the menu is open closes the
// menu and opens the search with what was typed.
class MenuItem final : public juce::PopupMenu::CustomComponent, private juce::KeyListener
{
public:
    explicit MenuItem (std::function<void (const juce::String& typed)> openSearch);
    ~MenuItem() override;
    void getIdealSize (int& width, int& height) override { width = 300; height = 30; }
    void paint (juce::Graphics&) override;
    void parentHierarchyChanged() override;
private:
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    std::function<void (const juce::String&)> open;
    juce::Component::SafePointer<juce::Component> window;
    bool opening = false;
};

void drawMagnifier (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

// The search box and its results, laid over a window (as the name prompt and the colour editor are).
class Panel final : public juce::Component, private juce::ListBoxModel, private juce::KeyListener
{
public:
    Panel();
    ~Panel() override;
    // Opens over its parent: `target` names where the effect goes ("Band 2  /  Slot 3"); `choose` gets the chosen
    // entry's index in `entries`.
    // The heart on each row (0.15.0) marks a favourite through `setFavourite` (the same favourites as the effect menu's).
    void open (std::shared_ptr<std::vector<EffectEntry>> entries, const juce::String& target, const juce::String& initialText,
               std::function<bool (const EffectEntry&)> isFavourite, std::function<void (int)> choose,
               std::function<void (const EffectEntry&, bool)> setFavourite = {});
    void toggleFavourite (int row);                 // the heart on a row of getResults()
    void setFavouritesOnly (bool);                  // the FAVORITES button: only the favourites
    bool isFavouritesOnly() const noexcept { return favouritesButton.getToggleState(); }
    bool isFavouriteRow (int row) const;
    void close();
    void setQuery (const juce::String&);            // as if typed
    juce::String getQuery() const { return box.getText(); }
    const std::vector<int>& getResults() const noexcept { return results; }
    int getSelectedResult() const;                  // a row of getResults(), -1 for none
    void selectResult (int row);
    bool chooseSelected();                          // Return: loads the selected plug-in; false if there is none
    bool handleKey (const juce::KeyPress& k) { return keyPressed (k, &box); }   // Up / Down / Page Up / Page Down
    juce::TextEditor& getTextBox() noexcept { return box; }
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // the magnifying glass, in the box
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // outside the box: close
private:
    int getNumRows() override { return (int) results.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int) override { chooseSelected(); }
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void update();
    juce::Rectangle<int> frame() const;
    static constexpr int heartColumn = 34;   // the heart, at the left of each row (pixels from the row's left)
    std::shared_ptr<std::vector<EffectEntry>> list;
    std::function<bool (const EffectEntry&)> favourite;
    std::function<void (const EffectEntry&, bool)> markFavourite;
    std::function<void (int)> onChoose;
    std::vector<int> results;
    juce::String targetText;
    juce::TextEditor box;
    juce::ListBox rows { "Plug-ins", this };
    juce::TextButton closeButton { "CLOSE" }, favouritesButton;
};
}
