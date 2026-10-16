#include "PlatformWindows.h"

#if JUCE_MAC
// PlatformWindowsMac.mm
void triadMacKeepAbove (void* childView, void* parentView);
bool triadMacSendCurrentKey (void* targetView);
#endif

namespace triad::platform
{
void keepAbove (juce::Component& window, juce::Component* owner)
{
   #if JUCE_MAC
    if (owner != nullptr)
        if (auto* child = window.getPeer())
            if (auto* parent = owner->getPeer())
            {
                triadMacKeepAbove (child->getNativeHandle(), parent->getNativeHandle());
                return;
            }
   #else
    juce::ignoreUnused (owner);
   #endif
    window.setAlwaysOnTop (true);
}
bool sendKeyToHost (juce::Component& owner)
{
   #if JUCE_MAC
    if (auto* peer = owner.getPeer()) return triadMacSendCurrentKey (peer->getNativeHandle());
   #else
    juce::ignoreUnused (owner);
   #endif
    return false;
}
}
