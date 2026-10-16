#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace triad::platform
{
// Keeps `window` in front of the window that contains `owner` (Triad's window in the DAW).
// macOS: it becomes a child window that stays above Triad and moves with it; elsewhere it is
// kept on top.
void keepAbove (juce::Component& window, juce::Component* owner);
// Passes the key press being handled right now to the DAW window that contains `owner`, so the
// DAW's own shortcuts (spacebar play/stop) work while an effect window is focused. macOS only.
bool sendKeyToHost (juce::Component& owner);
}
