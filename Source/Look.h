#pragma once
#include "UiCommon.h"

// Triad's drawn icons, lights and the look of its buttons, sliders and knobs (no audio code: Triad Time uses them too).
namespace triad::ui
{
// Small drawn icons for toolbar buttons (no font glyphs needed).
class IconButton final : public juce::Button
{
public:
    enum class Icon { trash, undo, redo, reverse, flipVertical, left, right, gear, menu, plus, close, lfo, dice };
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

// The look of every flat button, slider and knob in Triad.
class TriadLook final : public juce::LookAndFeel_V4
{
public:
    TriadLook();                                     // in the theme stored in Triad.settings
    void applyTheme();                               // after a theme change (the window then calls sendLookAndFeelChange)
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&,
                                                const juce::PopupMenu::Options&) override;
};
}
