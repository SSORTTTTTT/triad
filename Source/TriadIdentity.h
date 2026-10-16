#pragma once
#include <juce_core/juce_core.h>

// The product name and version come from CMakeLists.txt (TRIAD_PRODUCT_NAME / TRIAD_VERSION).
#ifndef TRIAD_PRODUCT_NAME
 #define TRIAD_PRODUCT_NAME "Triad"
#endif
#ifndef TRIAD_VERSION
 #define TRIAD_VERSION "0.0.0"
#endif

namespace triad
{
// True for any build of Triad itself ("Triad", "Triad v2", ...): Triad never hosts itself.
inline bool isTriadName (const juce::String& name)
{
    if (name == "Triad" || name == TRIAD_PRODUCT_NAME) return true;
    return name.startsWith ("Triad v") && name.length() > 7 && juce::CharacterFunctions::isDigit (name[7]);
}
}
