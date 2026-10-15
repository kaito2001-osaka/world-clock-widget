// WorldClockGadget — resident frameless, semi-transparent desktop clock.
// Win32 + Direct2D. Intentionally lightweight (no App SDK / heavy runtime).
//
// This file holds nothing but the entry point: process-level setup, the
// single-instance guard, and handing off to GadgetWindow.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <delayimp.h>

#include "Config.hpp"
#include "GadgetWindow.hpp"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // for WIC

    // Single instance: bail out if another gadget is already running.
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"WorldClockGadget_SingleInstance");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    // shell32 is delay-loaded and needed only to find the AppData folder,
    // which ConfigDir() caches. Look it up now and unload shell32, so the
    // resident process does not keep it and what it pulls in mapped.
    ConfigDir();
    __FUnloadDelayLoadedDLL2("SHELL32.dll");

    int rc = 0;
    {
        GadgetWindow gadget;
        if (gadget.Create(hInst))
            rc = gadget.RunMessageLoop();
        else
            rc = 1;
    }

    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return rc;
}
