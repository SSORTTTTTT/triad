// macOS window helpers (kept free of JUCE headers).
#import <Cocoa/Cocoa.h>

// Makes the effect window a child of the DAW window that shows Triad: it stays in front of it,
// moves with it and is not left floating over other apps.
void triadMacKeepAbove (void* childView, void* parentView)
{
    NSWindow* child = [(NSView*) childView window];
    NSWindow* parent = [(NSView*) parentView window];
    if (child == nil || parent == nil || child == parent) return;
    if ([child parentWindow] == parent) { [child orderFront: nil]; return; }
    if ([child parentWindow] != nil) [[child parentWindow] removeChildWindow: child];
    [parent addChildWindow: child ordered: NSWindowAbove];
}

// Detaches the window from the DAW window again (before it is hidden).
void triadMacRelease (void* childView)
{
    NSWindow* child = [(NSView*) childView window];
    if (child != nil && [child parentWindow] != nil) [[child parentWindow] removeChildWindow: child];
}

// Re-sends the key press being handled to the DAW window, and gives that window the keyboard.
bool triadMacSendCurrentKey (void* targetView)
{
    NSEvent* event = [NSApp currentEvent];
    if (event == nil || [event type] != NSEventTypeKeyDown) return false;
    NSWindow* window = [(NSView*) targetView window];
    if (window == nil || [event window] == window) return false;
    [window makeKeyWindow];
    [window sendEvent: event];
    return true;
}
