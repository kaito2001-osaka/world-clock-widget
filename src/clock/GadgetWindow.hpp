// The resident gadget window: frameless, layered, draggable, with a right-click
// menu and a config-file watcher that live-reloads on change. Encapsulates all
// window state so main.cpp holds nothing but wWinMain.
#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <optional>

#include "Config.hpp"
#include "Renderer.hpp"
#include "Visibility.hpp"

class GadgetWindow {
public:
    GadgetWindow() = default;
    ~GadgetWindow();

    GadgetWindow(const GadgetWindow&) = delete;
    GadgetWindow& operator=(const GadgetWindow&) = delete;

    // Register the class, create the window, init the renderer, apply config,
    // start the timer and config watcher, and show. Returns false on failure.
    bool Create(HINSTANCE hInst);

    // Pump messages until WM_QUIT; tears down the watcher and renderer on exit.
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

    // Window behavior
    void ApplyTopmost();
    void RenderNow();
    void SaveCurrentPosition();
    void RescueFromOffscreen();   // after a monitor change; does not save
    void StartTimer();
    void ArmTimer();   // one-shot aimed at the next display boundary
    void Tick();       // redraw if the displayed instant moved, then re-arm; no-op while hidden
    void RegisterVisibilityNotifications();
    void UnregisterVisibilityNotifications();
    void OnVisibilityEvent(VisibilityEvent e);
    void ReloadConfig();
    void OnConfigLoadFailed(ConfigLoadStatus status);
    void ShowWarning(const std::wstring& text);
    void LaunchSettings();
    // Writes one field to config.json; false when it refused to (see
    // BaseForEdit), in which case the caller must not apply the change.
    bool PersistToggle(bool Config::* field, bool value);
    void ShowMenu();

    // Config-directory watcher
    void StartWatcher();
    void StopWatcher();
    void WatchThreadProc();
    // One open-arm-wait session on the config directory; returns on an error
    // (the caller backs off and reopens) or when the stop event is set.
    void WatchDirectory(bool afterFailure, int& failures);
    void RequestReload();   // watcher thread; coalesced via reloadPending_

    Renderer    renderer_;
    Config      config_;
    // config_ holds settings read from config.json, not the defaults standing
    // in for a file that could not be read at startup (see NeedsApply).
    bool        configFromFile_ = false;
    WindowState state_;
    HWND        hwnd_  = nullptr;
    HINSTANCE   hinst_ = nullptr;

    std::thread       watchThread_;
    std::atomic<bool> watchRun_{ false };
    HANDLE            watchStop_ = nullptr;
    // A WM_APP_RELOAD is queued and not yet handled; further requests ride on
    // it instead of queueing a second full reload.
    std::atomic<bool> reloadPending_{ false };

    // The instant last drawn (see DisplayedInstant); reset to force a redraw.
    std::optional<std::chrono::system_clock::time_point> lastShown_;

    // Display off / session locked / disconnected: the tick is stopped until
    // someone can see the window again.
    Visibility   visibility_;
    HPOWERNOTIFY displayNotify_ = nullptr;
    bool         sessionNotify_ = false;

    // A failed load never replaces what is on screen. It is retried a few
    // times (the file may be mid-write or briefly locked), then reported once
    // until a load succeeds again.
    int  reloadRetries_     = 0;
    bool configNoticeShown_ = false;
    bool showingNotice_     = false;   // MessageBox pumps messages; no nesting
};
