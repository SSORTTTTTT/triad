// Test fixture: a "VST3" whose code crashes (or hangs) the moment it is loaded, like the plug-in
// that took FL Studio down while Triad 0.2.1 listed effects. Triad must survive it: only the
// Triad Scanner helper may ever load it. TRIAD_FIXTURE_HANG=1 makes it hang instead of crash.
#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{
const int trap = []
{
    if (std::getenv ("TRIAD_FIXTURE_HANG") != nullptr)
        for (;;) std::this_thread::sleep_for (std::chrono::seconds (1));
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
