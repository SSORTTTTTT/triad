#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace triad::ui
{
// One colour per band position (up to 10 bands).
inline juce::Colour bandColour (int band)
{
    static const juce::Colour colours[] { juce::Colour (0xff9bd85c), juce::Colour (0xff67c6b8), juce::Colour (0xffa39ce8),
                                          juce::Colour (0xffe8b15c), juce::Colour (0xffe87f8a), juce::Colour (0xff5ca8e8),
                                          juce::Colour (0xffd88ce0), juce::Colour (0xffc9d65c), juce::Colour (0xff5cdca2),
                                          juce::Colour (0xffe8955c) };
    return colours[juce::jlimit (0, 9, band)];
}
const juce::Colour background (0xff252729), panel (0xff1b1e1c), textColour (0xffe7eae5), dimTextColour (0xffa6afa5),
                   slotRow (0xff2e3230), slotRowHover (0xff363b38), ledColour (0xff8fe04a);

inline juce::String formatHz (float hz)
{
    return (hz >= 1000.0f ? juce::String (hz / 1000.0f, 3) : juce::String (hz, 2))
        .trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + (hz >= 1000.0f ? " kHz" : " Hz");
}
// Accepts "250", "250 Hz", "2.5k", "2.5 kHz". Returns <= 0 if the text is not a frequency.
inline float parseHz (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t.isEmpty() || ! juce::CharacterFunctions::isDigit (t[0])) return -1.0f;
    const auto value = t.getFloatValue();
    return t.containsChar ('k') ? value * 1000.0f : value;
}
}
