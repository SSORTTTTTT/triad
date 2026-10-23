// Test fixture: a "VST3" whose code crashes (or hangs) the moment it is loaded, like the plug-in
// that took FL Studio down while Triad 0.2.1 listed effects. Triad must survive it: only the
// Triad Scanner helper may ever load it. TRIAD_FIXTURE_HANG=1 makes it hang instead of crash.
#include <chrono>
#include <cstdlib>
#include <thread>
#if defined (_WIN32)
 #include <stdlib.h>
#endif

namespace
{
const int trap = []
{
    if (std::getenv ("TRIAD_FIXTURE_HANG") != nullptr)
        for (;;) std::this_thread::sleep_for (std::chrono::seconds (1));
   #if defined (_WIN32)
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // no dialog: end at once, as on the Mac
   #endif
    std::abort();
    return 0;
}();
}

#if defined (_WIN32)
 #define FIXTURE_EXPORT extern "C" __declspec (dllexport)
#else
 #define FIXTURE_EXPORT extern "C" __attribute__ ((visibility ("default")))
#endif

FIXTURE_EXPORT void* GetPluginFactory() { return trap == 0 ? nullptr : nullptr; }
