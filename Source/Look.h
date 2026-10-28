#pragma once
#include "UiCommon.h"

// Triad's drawn icons, lights and the look of its buttons, sliders and knobs (no audio code: Triad Time uses them too).
namespace triad::ui
{
// Small drawn icons for toolbar buttons (no font glyphs needed).
class IconButton final : public juce::Button
{
public:
    enum class Icon { trash, undo, redo, reverse, flipVertical, left, right, gear, menu, plus, close, lfo, dice, moveLeft, moveRight };
    IconButton (Icon, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);
private:
    Icon icon;
};

// FL-style green on/off light.
class Led final : public juce::Button
{
public:
    explicit Led (const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// ---- Menus (0.16.0, Thomas: "change it to that different style of what I really love") -------------------------
// Every menu in Triad, Triad Time and Triad Split is drawn by TriadLook: a rounded panel in the theme's colours,
// section titles in the accent colour (a title with small letters, such as a plug-in's name, is drawn larger, in the
// text colour), the chosen item filled and ticked, the item under the mouse lit, counts and values on the right,
// folders opening to the side. The colours are worked out from the theme (in Plum: purple, lavender, pink).
struct MenuColours
{
    juce::Colour back, edge, chosen, chosenInk, hover, text, dim, header, line;
};
MenuColours menuColours();
constexpr int menuRowHeight = 25, menuHeaderHeight = 24, menuTitleHeight = 32, menuSeparatorHeight = 9;
// Tests: menus open inside the plug-in's window (so a snapshot of the window shows them), not in windows of their own.
inline bool menusInsideWindow = false;

// What an item shows on its right, in place of text (it goes in the item's shortcutKeyDescription):
juce::String menuSwatches (const juce::Array<juce::Colour>&);   // small squares: a theme's colours
juce::String menuTag (const juce::String& tag);                  // a small framed tag: VST3, AU, TRIAD
juce::String menuLfo (const juce::String& text);                 // an item with the LFO's wave in front ("~ Link to LFO")
juce::String menuFilter (char kind, const juce::String& text);   // an item with a small filter picture: s, w, l, h
void drawFilterIcon (juce::Graphics&, juce::Rectangle<float>, char kind, juce::Colour);
// An item or a folder with a value or a count on its right ("Theme   Plum >", "DELAY   8 >").
void addItemWithRight (juce::PopupMenu&, int id, const juce::String& name, const juce::String& right, bool enabled = true, bool ticked = false);
void addSubMenu (juce::PopupMenu&, const juce::String& name, juce::PopupMenu, const juce::String& right = {}, bool enabled = true,
                 bool ticked = false);

// A row of chips in a menu (WINDOW SIZE: 75% 100% 125% ...): a click picks one and closes the menu with this item's id;
// the chip picked is put in `picked`.
class MenuChips final : public juce::PopupMenu::CustomComponent
{
public:
    MenuChips (juce::StringArray labels, int chosen, std::shared_ptr<int> picked);
    void getIdealSize (int& width, int& height) override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    int chipAt (juce::Point<float>) const;
    juce::Rectangle<float> chip (int index) const;
private:
    juce::StringArray labels;
    int chosen;
    std::shared_ptr<int> picked;
};

// ---- Value boxes (0.16.0): every knob is drawn as a box, the value on top and a line meter under it ----------------
// filling left to right in the knob's colour ("I like the way the metering thing looks"). A knob can have a caption
// drawn in its box (the property "triadLabel", e.g. "BAND MIX").
juce::Rectangle<float> valueBoxArea (juce::Rectangle<float> knobBounds, bool captioned);
juce::Rectangle<float> valueBoxMeter (juce::Rectangle<float> knobBounds, bool captioned);   // the meter line (for an LFO's dot)

// The look of every flat button, slider, knob and menu in Triad.
class TriadLook final : public juce::LookAndFeel_V4
{
public:
    TriadLook();                                     // in the theme stored in Triad.settings
    // JUCE draws menus and tooltips in windows of their own with the process's default look: while any window of this
    // plug-in is open, a TriadLook is that default (0.16.0; before, the menus were JUCE's own grey ones).
    ~TriadLook() override;
    void applyTheme();                               // after a theme change (the window then calls sendLookAndFeelChange)
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    // A button with the property triadAlign = "left" reads as a name on its card (an LFO card's destination): one line,
    // left-aligned, cut with "..." when long; the others are centred as JUCE's.
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    // Menus
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height, const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area, bool isHighlighted,
                                       const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&,
                                                const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                               int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int standardMenuItemHeight,
                                                        int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&) override;
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return 6; }
    juce::Component* getParentComponentForMenuOptions (const juce::PopupMenu::Options&) override;
    void drawPopupMenuColumnSeparatorWithOptions (juce::Graphics&, const juce::Rectangle<int>&, const juce::PopupMenu::Options&) override;
    int getPopupMenuColumnSeparatorWidthWithOptions (const juce::PopupMenu::Options&) override { return 6; }
    // Tooltips and the START FROM box of the colour editor, in the menus' look
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};
}
