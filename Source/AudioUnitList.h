#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Lists installed Audio Units straight from macOS (AudioComponent API) without loading any of
// them, so Triad can tell effects from instruments instantly. Kept free of JUCE headers to
// avoid CoreServices/JUCE name clashes. Returns an empty list on other platforms.
namespace triad::au
{
struct Component
{
    std::uint32_t type = 0, subtype = 0, manufacturer = 0;
    std::string name, maker;
    bool isV3 = false;
};
std::vector<Component> listComponents();
}
