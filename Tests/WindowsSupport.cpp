// Windows support for the test programs: what they print when they crash (the Mac and Linux builds use signal
// handlers instead), and a thread's CPU time. Kept free of JUCE headers, so <windows.h> cannot clash with JUCE names.
#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <csignal>
 #include <cstdio>
 #include <cstdlib>
 #include <intrin.h>
 #include <string>
 #include <GL/gl.h>
 #pragma comment (lib, "opengl32.lib")
 #pragma comment (lib, "gdi32.lib")
 #pragma comment (lib, "user32.lib")

namespace
{
void (*describeCrash)() = nullptr;
volatile LONG reporting = 0;

void describeOnce()
{
    if (InterlockedExchange (&reporting, 1) != 0) return;   // one report, even when several threads crash
    if (describeCrash != nullptr) describeCrash();
    std::fflush (stdout);
    std::fflush (stderr);
}

LONG WINAPI onUnhandledException (EXCEPTION_POINTERS* info)
{
    const auto* record = info != nullptr ? info->ExceptionRecord : nullptr;
    const auto code = record != nullptr ? (unsigned long) record->ExceptionCode : 0ul;
    void* address = record != nullptr ? record->ExceptionAddress : nullptr;
    std::fprintf (stderr, "\n*** CRASH (exception 0x%08lX%s at %p", code,
                  code == 0xC0000005ul ? ", access violation" : code == 0xC00000FDul ? ", stack overflow" : "", address);
    HMODULE module = nullptr;
    char name[MAX_PATH] {};
    if (address != nullptr
        && GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) address, &module)
        && GetModuleFileNameA (module, name, MAX_PATH) > 0)
        std::fprintf (stderr, ", in %s +0x%llx", name, (unsigned long long) ((const char*) address - (const char*) module));
    std::fprintf (stderr, ")\n");
    describeOnce();
    return EXCEPTION_EXECUTE_HANDLER;   // ends the process with the exception code, without a "has stopped working" dialog
}

void onAbort (int)
{
    std::fprintf (stderr, "\n*** CRASH (abort)\n");
    describeOnce();
    std::_Exit (3);
}
}

// Called once at the start of main (and of each child process the tests start).
void triadInstallCrashReport (void (*describe)())
{
    describeCrash = describe;
    // No error dialogs (a crash or a missing DLL ends the process at once; nobody is there to click them), and child
    // processes inherit this.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetUnhandledExceptionFilter (onUnhandledException);
    std::signal (SIGABRT, onAbort);
}

// CPU time this thread has used, in ms. Counted in processor cycles (QueryThreadCycleTime: exact, where
// GetThreadTimes only moves in 15.6 ms steps), turned into ms with the cycle rate measured once at startup.
namespace
{
double measureCyclesPerMs()
{
    LARGE_INTEGER frequency {}, start {}, now {};
    QueryPerformanceFrequency (&frequency);
    QueryPerformanceCounter (&start);
    const auto c0 = __rdtsc();
    do QueryPerformanceCounter (&now); while ((now.QuadPart - start.QuadPart) * 1000 < frequency.QuadPart * 40);
    const auto c1 = __rdtsc();
    const double ms = (double) (now.QuadPart - start.QuadPart) * 1000.0 / (double) frequency.QuadPart;
    return ms > 0.0 ? (double) (c1 - c0) / ms : 0.0;
}
const double cyclesPerMs = measureCyclesPerMs();   // at startup, not in the middle of a timed audio block
}
double triadThreadCpuMs()
{
    ULONG64 cycles = 0;
    if (cyclesPerMs > 0.0 && QueryThreadCycleTime (GetCurrentThread(), &cycles)) return (double) cycles / cyclesPerMs;
    FILETIME created {}, exited {}, kernel {}, user {};
    GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user);
    auto ms = [] (const FILETIME& f) { return (double) (((ULONG64) f.dwHighDateTime << 32) | f.dwLowDateTime) / 10000.0; };
    return ms (kernel) + ms (user);
}

// The stress test's graphics checks, as on the Mac (StressMac.mm). GitHub's Windows machines are virtual and have no
// graphics card: OpenGL there is Windows' own software renderer ("GDI Generic", OpenGL 1.1), so slow (or missing what
// a plug-in asks for) that a plug-in window drawing with it can hold up or upset the whole test.
// The OpenGL renderer's name ("NVIDIA GeForce ...", "GDI Generic", ...), or empty when there is none.
std::string triadStressOpenGLRenderer()
{
    std::string name;
    const auto instance = GetModuleHandleW (nullptr);
    WNDCLASSW windowClass {};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"TriadStressOpenGLProbe";
    RegisterClassW (&windowClass);
    const auto window = CreateWindowW (L"TriadStressOpenGLProbe", L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) return name;
    const auto dc = GetDC (window);
    PIXELFORMATDESCRIPTOR wanted {};
    wanted.nSize = sizeof (wanted);
    wanted.nVersion = 1;
    wanted.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    wanted.iPixelType = PFD_TYPE_RGBA;
    wanted.cColorBits = 32;
    wanted.cDepthBits = 24;
    const int format = ChoosePixelFormat (dc, &wanted);
    if (format > 0 && SetPixelFormat (dc, format, &wanted))
        if (const auto context = wglCreateContext (dc))
        {
            if (wglMakeCurrent (dc, context))
            {
                if (const auto* renderer = (const char*) glGetString (GL_RENDERER)) name = renderer;
                wglMakeCurrent (nullptr, nullptr);
            }
            wglDeleteContext (context);
        }
    ReleaseDC (window, dc);
    DestroyWindow (window);
    return name;
}

namespace
{
bool drawsWithOpenGL (HWND window)
{
    bool found = false;
    if (const auto dc = GetDC (window))
    {
        PIXELFORMATDESCRIPTOR format {};
        const int index = GetPixelFormat (dc);
        if (index > 0 && DescribePixelFormat (dc, index, sizeof (format), &format) > 0) found = (format.dwFlags & PFD_SUPPORT_OPENGL) != 0;
        ReleaseDC (window, dc);
    }
    return found;
}
BOOL CALLBACK findOpenGLChild (HWND child, LPARAM result)
{
    if (! drawsWithOpenGL (child)) return TRUE;
    *(bool*) result = true;
    return FALSE;   // found: stop
}
}
// The window that owns this one (it stays in front of its owner), or null.
void* triadTestOwnerOf (void* window)
{
    return window != nullptr ? (void*) GetWindowLongPtrW ((HWND) window, GWLP_HWNDPARENT) : nullptr;
}

// `nativeHandle` is a JUCE peer's native handle (an HWND): does it, or a window inside it, draw with OpenGL?
bool triadStressUsesOpenGL (void* nativeHandle)
{
    const auto window = (HWND) nativeHandle;
    if (window == nullptr || ! IsWindow (window)) return false;
    bool found = drawsWithOpenGL (window);
    if (! found) EnumChildWindows (window, findOpenGLChild, (LPARAM) &found);
    return found;
}
#endif
