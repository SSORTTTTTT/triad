// Windows window helpers (kept free of JUCE headers, so <windows.h> cannot clash with JUCE names).
#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>

// Makes the effect window an owned window of the DAW window that shows Triad: it stays in front of that window,
// is minimised with it and is not left floating over other apps (as "always on top" would).
bool triadWinKeepAbove (void* childWindow, void* parentWindow)
{
    const auto child = (HWND) childWindow;
    const auto owner = parentWindow != nullptr ? GetAncestor ((HWND) parentWindow, GA_ROOT) : nullptr;
    if (child == nullptr || owner == nullptr || child == owner || ! IsWindow (child) || ! IsWindow (owner)) return false;
    if ((HWND) GetWindowLongPtrW (child, GWLP_HWNDPARENT) != owner)
    {
        SetLastError (0);
        if (SetWindowLongPtrW (child, GWLP_HWNDPARENT, (LONG_PTR) owner) == 0 && GetLastError() != 0) return false;
    }
    return true;
}

// Detaches the window from the DAW window again (before it is hidden), so closing the DAW's window never takes it along.
void triadWinRelease (void* childWindow)
{
    const auto child = (HWND) childWindow;
    if (child != nullptr && IsWindow (child) && GetWindowLongPtrW (child, GWLP_HWNDPARENT) != 0)
        SetWindowLongPtrW (child, GWLP_HWNDPARENT, 0);
}
#endif
