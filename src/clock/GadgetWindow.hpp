// The resident gadget window: frameless, layered, draggable, with a right-click
// menu and a config-file watcher that live-reloads on change. Encapsulates all
// window state so main.cpp holds nothing but wWinMain.
#pragma once
#include <windows.h>
#include <thread>
#include <atomic>

#include "Config.hpp"
#include "Renderer.hpp"

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
    void StartTimer();
    void ReloadConfig();
    void LaunchSettings();
    void PersistToggle(bool Config::* field, bool value);
    void ShowMenu();

    // Config-directory watcher
    void StartWatcher();
    void StopWatcher();
    void WatchThreadProc();

    Renderer    renderer_;
    Config      config_;
    WindowState state_;
    HWND        hwnd_  = nullptr;
    HINSTANCE   hinst_ = nullptr;

    std::thread       watchThread_;
    std::atomic<bool> watchRun_{ false };
    HANDLE            watchStop_ = nullptr;

    int lastMinute_ = -1;
    int lastSecond_ = -1;
};
