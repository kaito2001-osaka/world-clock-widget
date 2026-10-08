#include "GadgetWindow.hpp"
#include "ConfigWatch.hpp"
#include "Startup.hpp"
#include "TimerCadence.hpp"
#include "WindowPlacement.hpp"
#include "resource.h"

#include <shellapi.h>
#include <wtsapi32.h>
#include <string>
#include <chrono>
#include <vector>

namespace {

constexpr UINT     WM_APP_RELOAD = WM_APP + 1;
constexpr UINT_PTR TIMER_ID      = 1;
constexpr UINT_PTR RELOAD_RETRY_TIMER_ID = 2;
constexpr UINT_PTR DISPLAY_SETTLE_TIMER_ID = 3;

// WM_DISPLAYCHANGE can arrive before the monitor topology has finished
// changing, and one undock sends several notifications. Re-check once, this
// long after the last of them.
constexpr UINT kDisplaySettleMs = 500;

// A failed config load is retried this many times, this far apart, before it
// is reported -- long enough to ride out an editor's save or a virus scan.
constexpr int  kMaxReloadRetries = 4;
constexpr UINT kReloadRetryMs    = 250;

constexpr wchar_t kConfigPathHint[] = L"\n\n%APPDATA%\\WorldClockGadget\\config.json";

constexpr int ID_SETTINGS = 1001;
constexpr int ID_TOPMOST  = 1002;
constexpr int ID_LOCK     = 1003;
constexpr int ID_EXIT     = 1004;

constexpr wchar_t kClassName[] = L"WorldClockGadgetWindow";

// Single resident instance; the static WndProc thunk routes to it.
GadgetWindow* g_instance = nullptr;

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : p.substr(0, pos + 1);
}

// Work area of every attached monitor, for validating a restored position.
BOOL CALLBACK CollectWorkArea(HMONITOR mon, HDC, LPRECT, LPARAM userData) {
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi))
        reinterpret_cast<std::vector<RECT>*>(userData)->push_back(mi.rcWork);
    return TRUE;
}

std::vector<RECT> MonitorWorkAreas() {
    std::vector<RECT> areas;
    EnumDisplayMonitors(nullptr, nullptr, &CollectWorkArea,
                        reinterpret_cast<LPARAM>(&areas));
    return areas;
}

} // namespace

GadgetWindow::~GadgetWindow() {
    StopWatcher();
}

bool GadgetWindow::Create(HINSTANCE hInst) {
    hinst_ = hInst;
    g_instance = this;

    WriteDefaultConfigIfMissing();
    const ConfigLoadResult loaded = LoadConfig();
    config_ = loaded.config;   // the defaults unless it loaded
    configFromFile_ = loaded.status == ConfigLoadStatus::Ok;
    state_  = LoadState();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &GadgetWindow::WndProcThunk;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    // Shown by Task Manager and the shell (the window itself is a tool window).
    wc.hIcon   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);

    // Frameless, layered, no taskbar / Alt-Tab presence, not activated.
    DWORD style   = WS_POPUP;
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    // The real size arrives with the first UpdateLayeredWindow; this is only
    // the seed the placement check reasons about.
    const int kSeedW = 240, kSeedH = 200;
    int x = state_.hasPosition ? state_.windowX : 1200;
    int y = state_.hasPosition ? state_.windowY : 80;

    // A saved position can be off-screen now: a monitor was unplugged, the
    // laptop left its dock, the resolution shrank. This window has no taskbar
    // button and no Alt-Tab entry, so off-screen means unrecoverable -- pull it
    // back onto a monitor that exists.
    RECT placed = ClampToVisibleArea(RECT{ x, y, x + kSeedW, y + kSeedH },
                                     MonitorWorkAreas());
    x = placed.left;
    y = placed.top;

    hwnd_ = CreateWindowExW(exStyle, kClassName, L"World Clock",
                            style, x, y, kSeedW, kSeedH,
                            nullptr, nullptr, hInst, nullptr);
    if (!hwnd_) return false;

    if (!renderer_.Init(hwnd_)) {
        MessageBoxW(nullptr, L"Direct2D の初期化に失敗しました。", L"World Clock", MB_ICONERROR);
        return false;
    }
    renderer_.SetConfig(config_);
    ApplyTopmost();
    // Reconcile the Run key only against a config that was actually read. The
    // defaults say "no autostart", so applying them would unregister it.
    if (loaded.status == ConfigLoadStatus::Ok)
        ApplyStartupRegistry(config_.launchAtStartup);
    RenderNow();   // build the layered surface before first show

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    StartTimer();
    // After the timer, so a display that is already off stops it again: the
    // current state is sent right after registering.
    RegisterVisibilityNotifications();
    StartWatcher();
    if (loaded.status != ConfigLoadStatus::Ok)
        OnConfigLoadFailed(loaded.status);
    return true;
}

int GadgetWindow::RunMessageLoop() {
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    StopWatcher();
    renderer_.Shutdown();
    return 0;
}

LRESULT CALLBACK GadgetWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_instance) return g_instance->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT GadgetWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER: {
            if (wParam == RELOAD_RETRY_TIMER_ID) {
                KillTimer(hwnd, RELOAD_RETRY_TIMER_ID);   // one-shot
                ReloadConfig();
                return 0;
            }
            if (wParam == DISPLAY_SETTLE_TIMER_ID) {
                KillTimer(hwnd, DISPLAY_SETTLE_TIMER_ID);   // one-shot
                RescueFromOffscreen();
                return 0;
            }
            if (wParam != TIMER_ID) break;
            Tick();
            return 0;
        }

        // The clock jumped (a clock change, or resume from sleep). The pending
        // timer was aimed with the old time, so waiting for it could leave the
        // stale time up for a whole period. Redraw now and re-aim. While hidden
        // Tick does nothing; showing the window again redraws anyway.
        case WM_TIMECHANGE:
            lastShown_.reset();
            Tick();
            return 0;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC) {
                lastShown_.reset();
                Tick();
            } else if (wParam == PBT_POWERSETTINGCHANGE) {
                const auto* s = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
                if (s && s->PowerSetting == GUID_SESSION_DISPLAY_STATUS
                      && s->DataLength >= sizeof(DWORD)) {
                    DWORD status;
                    memcpy(&status, s->Data, sizeof(status));
                    if (const auto e = DisplayStatusEvent(status)) OnVisibilityEvent(*e);
                }
            }
            return TRUE;

        case WM_WTSSESSION_CHANGE:
            if (const auto e = SessionChangeEvent(wParam)) OnVisibilityEvent(*e);
            return 0;

        // A monitor was removed or resized, or the taskbar moved, while we are
        // up. The startup clamp has long since run, so re-check here. Setting
        // the timer again restarts it, which coalesces a burst into one check.
        case WM_DISPLAYCHANGE:
            SetTimer(hwnd, DISPLAY_SETTLE_TIMER_ID, kDisplaySettleMs, nullptr);
            break;

        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETWORKAREA)
                SetTimer(hwnd, DISPLAY_SETTLE_TIMER_ID, kDisplaySettleMs, nullptr);
            break;

        case WM_PAINT: {
            // Content is presented via UpdateLayeredWindow; just validate.
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            if (!config_.lockPosition) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;

        case WM_EXITSIZEMOVE:
            SaveCurrentPosition();
            return 0;

        case WM_RBUTTONUP:
            ShowMenu();
            return 0;

        case WM_APP_RELOAD:
            // Cleared before reading, so a change landing during the read
            // queues another reload instead of being folded into this one.
            reloadPending_.store(false);
            ReloadConfig();
            return 0;

        case WM_DPICHANGED: {
            UINT dpi = HIWORD(wParam);
            renderer_.OnDpiChanged(dpi);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            // Move to the suggested origin; size follows from UpdateLayeredWindow.
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            RenderNow();
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            KillTimer(hwnd, RELOAD_RETRY_TIMER_ID);
            KillTimer(hwnd, DISPLAY_SETTLE_TIMER_ID);
            UnregisterVisibilityNotifications();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void GadgetWindow::ApplyTopmost() {
    SetWindowPos(hwnd_, config_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void GadgetWindow::RenderNow() {
    renderer_.Render(std::chrono::system_clock::now());
}

void GadgetWindow::SaveCurrentPosition() {
    RECT rc; GetWindowRect(hwnd_, &rc);
    state_.windowX = rc.left;
    state_.windowY = rc.top;
    HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        int n = WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, nullptr, 0, nullptr, nullptr);
        std::string dev(n > 0 ? n - 1 : 0, '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, dev.data(), n, nullptr, nullptr);
        state_.monitor = dev;
    }
    SaveState(state_);
}

// Pulls the window back onto a monitor that exists, using its real size (the
// startup clamp only had the seed size).
//
// The new position is deliberately not saved. state.json holds where the user
// last put the window, and only a drag (WM_EXITSIZEMOVE) writes it -- the
// startup clamp and WM_DPICHANGED moves do not either. Topology changes are
// often transient (a DisplayPort monitor powering off, a remote desktop
// session, undocking for the day), and saving here would replace that position
// with the fallback, so the window would not come back to the external monitor
// on the next start. If the old position is still off-screen then, the startup
// clamp rescues it again.
void GadgetWindow::RescueFromOffscreen() {
    RECT rc;
    if (!GetWindowRect(hwnd_, &rc)) return;
    const std::optional<POINT> to = RescueOrigin(rc, MonitorWorkAreas());
    if (!to) return;
    SetWindowPos(hwnd_, nullptr, to->x, to->y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void GadgetWindow::StartTimer() {
    ArmTimer();
}

// A fixed 1000 ms SetTimer drifts: WM_TIMER is only delivered when the queue
// is empty, it coalesces, and its period is always >= the one requested. Two
// fires then land inside the same second, the redraw gate skips one, and the
// clock shows that second twice before jumping by two. Re-arming to the next
// boundary keeps every fire just after a real rollover.
//
// The boundary follows what is actually on screen, so hiding seconds means
// sleeping a whole minute instead of waking 60 times to decide there is
// nothing to do.
void GadgetWindow::ArmTimer() {
    const UINT ms = MillisecondsToNextTick(std::chrono::system_clock::now(), config_);
    SetTimer(hwnd_, TIMER_ID, ms, nullptr);
}

void GadgetWindow::Tick() {
    // A clock change, resume or config reload while hidden must not re-arm the
    // timer; becoming visible redraws and re-arms instead.
    if (!IsVisible(visibility_)) return;
    const auto now = std::chrono::system_clock::now();
    if (NeedsRedraw(lastShown_, now, config_.showSeconds)) {
        lastShown_ = DisplayedInstant(now, config_.showSeconds);
        RenderNow();
    }
    ArmTimer();   // re-aim at the next boundary
}

// Display off, session locked, remote session disconnected: nobody sees the
// frames, so stop drawing them. If either registration fails the gadget just
// keeps rendering through that state, as it did before.
void GadgetWindow::RegisterVisibilityNotifications() {
    displayNotify_ = RegisterPowerSettingNotification(hwnd_, &GUID_SESSION_DISPLAY_STATUS,
                                                      DEVICE_NOTIFY_WINDOW_HANDLE);
    sessionNotify_ = WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE;
}

void GadgetWindow::UnregisterVisibilityNotifications() {
    if (displayNotify_) {
        UnregisterPowerSettingNotification(displayNotify_);
        displayNotify_ = nullptr;
    }
    if (sessionNotify_) {
        WTSUnRegisterSessionNotification(hwnd_);
        sessionNotify_ = false;
    }
}

void GadgetWindow::OnVisibilityEvent(VisibilityEvent e) {
    switch (ApplyVisibilityEvent(visibility_, e)) {
        case VisibilityTransition::Hidden:
            KillTimer(hwnd_, TIMER_ID);
            break;
        case VisibilityTransition::Shown:
            // Draw before anything else can be seen, then re-aim: the last
            // frame on the layered window is from when it was hidden.
            lastShown_.reset();
            Tick();
            break;
        case VisibilityTransition::None:
            break;
    }
}

void GadgetWindow::ReloadConfig() {
    const ConfigLoadResult loaded = LoadConfig();
    if (loaded.status != ConfigLoadStatus::Ok) {
        OnConfigLoadFailed(loaded.status);   // keep showing config_
        return;
    }
    KillTimer(hwnd_, RELOAD_RETRY_TIMER_ID);
    reloadRetries_ = 0;
    configNoticeShown_ = false;

    if (!NeedsApply(configFromFile_, config_, loaded.config)) return;
    configFromFile_ = true;
    config_ = loaded.config;
    renderer_.SetConfig(config_);
    ApplyTopmost();
    ApplyStartupRegistry(config_.launchAtStartup);
    lastShown_.reset();   // force redraw
    Tick();               // the cadence follows showSeconds, so re-aim it right away
}

void GadgetWindow::OnConfigLoadFailed(ConfigLoadStatus status) {
    // A deleted file is nothing to report: no settings are lost, and the
    // watcher fires again when it is recreated.
    if (status == ConfigLoadStatus::Missing) return;

    // An editor may be mid-save, or a scanner may hold the file for a moment.
    if (reloadRetries_ < kMaxReloadRetries) {
        ++reloadRetries_;
        SetTimer(hwnd_, RELOAD_RETRY_TIMER_ID, kReloadRetryMs, nullptr);
        return;
    }
    reloadRetries_ = 0;
    if (configNoticeShown_) return;   // once per run of failures
    configNoticeShown_ = true;

    std::wstring text = status == ConfigLoadStatus::ParseFailed
        ? L"config.json に構文エラーがあるため、読み込めませんでした。\n"
        : L"config.json を読み込めませんでした"
          L"（他のアプリが使用中か、アクセスが拒否されています）。\n";
    text += L"読み込めるようになるまで、直前に読み込めた設定"
            L"（無い場合は既定の設定）で表示します。config.json は変更しません。";
    ShowWarning(text + kConfigPathHint);
}

// Modal, so it pumps messages: a reload landing meanwhile must not stack a
// second box on top of this one.
void GadgetWindow::ShowWarning(const std::wstring& text) {
    if (showingNotice_) return;
    showingNotice_ = true;
    MessageBoxW(hwnd_, text.c_str(), L"World Clock", MB_OK | MB_ICONWARNING);
    showingNotice_ = false;
}

void GadgetWindow::LaunchSettings() {
    std::wstring exe = ExeDir() + L"WorldClockSettings.exe";
    HINSTANCE r = ShellExecuteW(hwnd_, L"open", exe.c_str(), nullptr,
                                ExeDir().c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        MessageBoxW(hwnd_,
            L"設定アプリ (WorldClockSettings.exe) が見つかりませんでした。\n"
            L"config.json を直接編集してください:\n"
            L"%APPDATA%\\WorldClockGadget\\config.json",
            L"World Clock", MB_OK | MB_ICONINFORMATION);
    }
}

// Re-reads the file rather than rebuilding it from config_, so a save the
// settings app made a moment ago (and the watcher has not reloaded yet)
// survives. A file that is there but unreadable is left alone: writing the
// defaults over it would throw away every city the user can still fix.
bool GadgetWindow::PersistToggle(bool Config::* field, bool value) {
    const ConfigLoadResult onDisk = LoadConfig();
    std::optional<Config> base = BaseForEdit(onDisk, config_);
    if (!base) {
        std::wstring text = onDisk.status == ConfigLoadStatus::ParseFailed
            ? L"config.json に構文エラーがあるため、この変更を保存できませんでした。\n"
              L"ファイルを修正するか、設定アプリから保存し直してください。"
            : L"config.json を読み込めないため、この変更を保存できませんでした。\n"
              L"しばらくしてから、もう一度お試しください。";
        ShowWarning(text + kConfigPathHint);
        return false;
    }
    (*base).*field = value;
    // On failure the caller leaves config_ (and the menu check) as it was, so
    // what is shown never claims a setting the file does not hold.
    if (!WriteConfigFull(*base)) {
        ShowWarning(L"config.json に変更を保存できませんでした。\n"
                    L"ファイルが読み取り専用でないか確認し、もう一度お試しください。"
                    + std::wstring(kConfigPathHint));
        return false;
    }
    return true;
}

void GadgetWindow::ShowMenu() {
    POINT pt; GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"設定…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (config_.alwaysOnTop ? MF_CHECKED : 0),
                ID_TOPMOST, L"常に最前面");
    AppendMenuW(menu, MF_STRING | (config_.lockPosition ? MF_CHECKED : 0),
                ID_LOCK, L"位置を固定");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"終了");

    SetForegroundWindow(hwnd_); // required for TrackPopupMenu to dismiss correctly
    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case ID_SETTINGS: LaunchSettings(); break;
        case ID_TOPMOST:
            if (PersistToggle(&Config::alwaysOnTop, !config_.alwaysOnTop)) {
                config_.alwaysOnTop = !config_.alwaysOnTop;
                ApplyTopmost();
            }
            break;
        case ID_LOCK:
            if (PersistToggle(&Config::lockPosition, !config_.lockPosition))
                config_.lockPosition = !config_.lockPosition;
            break;
        case ID_EXIT:
            DestroyWindow(hwnd_);
            break;
    }
}

void GadgetWindow::StartWatcher() {
    watchStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watchRun_.store(true);
    watchThread_ = std::thread(&GadgetWindow::WatchThreadProc, this);
}

void GadgetWindow::StopWatcher() {
    watchRun_.store(false);
    if (watchStop_) SetEvent(watchStop_);
    if (watchThread_.joinable()) watchThread_.join();
    if (watchStop_) { CloseHandle(watchStop_); watchStop_ = nullptr; }
}

// The watcher must outlive any error. The directory handle is opened with
// FILE_SHARE_DELETE, so the folder can be deleted or replaced under us; giving
// up then would leave every later save unapplied until the gadget restarts.
// Instead each failed session is followed by a backoff and a reopen. The stop
// event ends the backoff wait early, so shutdown never waits it out.
void GadgetWindow::WatchThreadProc() {
    int  failures     = 0;
    bool afterFailure = false;
    while (watchRun_.load()) {
        WatchDirectory(afterFailure, failures);
        if (!watchRun_.load()) break;
        afterFailure = true;
        if (WaitForSingleObject(watchStop_, WatchRetryDelayMs(failures++)) == WAIT_OBJECT_0)
            break;
    }
}

void GadgetWindow::WatchDirectory(bool afterFailure, int& failures) {
    const std::wstring dir = ConfigDir();   // recreates the folder if it was deleted
    HANDLE h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) { CloseHandle(h); return; }
    BYTE buf[4096];
    bool pending = false;
    bool armed   = false;

    while (watchRun_.load()) {
        DWORD bytes = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = ReadDirectoryChangesW(h, buf, sizeof(buf), FALSE,
                                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                                        &bytes, &ov, nullptr);
        if (!ok) break;   // reopen
        pending = true;

        // Nothing was watching between the failure and now, so a save made
        // in that gap produced no notification. Read the file once to catch it.
        if (!armed) {
            armed = true;
            if (afterFailure) RequestReload();
        }

        HANDLE waits[2] = { ov.hEvent, watchStop_ };
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0) break; // stop signaled

        DWORD transferred = 0;
        const BOOL done = GetOverlappedResult(h, &ov, &transferred, FALSE);
        pending = false;
        if (!done) {
            // An overflow can arrive as an error rather than as zero bytes;
            // the handle is still good, so treat it like the zero-byte case.
            if (!IsOverflowError(GetLastError())) break;   // reopen
            transferred = 0;
        }
        failures = 0;   // the watch works, so the next failure backs off from the start

        // Only the config file itself. Matching a substring would also fire on
        // the "config.json.*.tmp" the two writers replace through, turning one
        // save into several reloads -- the first of them reading the file
        // before the replace has landed. An overflow counts as a change.
        if (ContainsConfigChange(buf, transferred)) {
            Sleep(80); // let the writer finish the atomic replace
            RequestReload();
        }
    }
    // The loop can exit with a ReadDirectoryChangesW still pending, and both
    // `ov` and `buf` are stack locals of this frame. Wait for the cancellation
    // to land before returning, or the kernel writes into a frame that is
    // already gone. The directory handle closes first; closing the event while
    // an I/O could still signal it risks signalling a recycled handle.
    if (pending) {
        CancelIoEx(h, &ov);
        DWORD cancelled = 0;
        GetOverlappedResult(h, &ov, &cancelled, TRUE);
    }
    CloseHandle(h);
    CloseHandle(ov.hEvent);
}

// One save can arrive as several notification batches. While a reload is
// still queued it will read the latest file anyway, so a second request adds
// nothing but a second full reload.
void GadgetWindow::RequestReload() {
    if (reloadPending_.exchange(true)) return;
    if (!PostMessageW(hwnd_, WM_APP_RELOAD, 0, 0)) reloadPending_.store(false);
}
