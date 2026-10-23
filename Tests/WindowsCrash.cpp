// Windows: what the tests print when they crash (the Mac and Linux builds use signal handlers instead).
// Kept free of JUCE headers, so <windows.h> cannot clash with JUCE names.
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
#endif
