#include "AudioUnitList.h"

#if defined (__APPLE__)
 #include <AudioToolbox/AudioToolbox.h>
 #include <cstring>
#endif

namespace triad::au
{
#if defined (__APPLE__)
namespace
{
std::string toUtf8 (CFStringRef text)
{
    if (text == nullptr) return {};
    const auto capacity = CFStringGetMaximumSizeForEncoding (CFStringGetLength (text), kCFStringEncodingUTF8) + 1;
    std::string out ((size_t) capacity, '\0');
    if (! CFStringGetCString (text, out.data(), capacity, kCFStringEncodingUTF8)) return {};
    out.resize (std::strlen (out.c_str()));
    return out;
}
std::string trim (const std::string& s)
{
    const auto first = s.find_first_not_of (" \t");
    if (first == std::string::npos) return {};
    return s.substr (first, s.find_last_not_of (" \t") - first + 1);
}
}

std::vector<Component> listComponents()
{
    std::vector<Component> result;
    AudioComponentDescription any {};
    AudioComponent comp = nullptr;
    while ((comp = AudioComponentFindNext (comp, &any)) != nullptr)
    {
        AudioComponentDescription d {};
        if (AudioComponentGetDescription (comp, &d) != noErr) continue;
        Component c;
        c.type = d.componentType;
        c.subtype = d.componentSubType;
        c.manufacturer = d.componentManufacturer;
        c.isV3 = (d.componentFlags & kAudioComponentFlag_IsV3AudioUnit) != 0;
        CFStringRef name = nullptr;
        if (AudioComponentCopyName (comp, &name) == noErr && name != nullptr)
        {
            const auto full = toUtf8 (name);
            CFRelease (name);
            const auto colon = full.find (':');   // "Manufacturer: Plug-in name"
            if (colon != std::string::npos) { c.maker = trim (full.substr (0, colon)); c.name = trim (full.substr (colon + 1)); }
            else c.name = trim (full);
        }
        result.push_back (std::move (c));
    }
    return result;
}
#else
std::vector<Component> listComponents() { return {}; }
#endif
}
