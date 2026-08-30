#include "GadgetWindow.hpp"
#include "ConfigWatch.hpp"
#include "Startup.hpp"
#include "TimerCadence.hpp"
#include "WindowPlacement.hpp"
#include "resource.h"

#include <shellapi.h>
#include <string>
#include <chrono>
#include <ctime>
#include <vector>

namespace {

constexpr UINT     WM_APP_RELOAD = WM_APP + 1;
constexpr UINT_PTR TIMER_ID      = 1;

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
    config_ = LoadConfig();
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
    ApplyStartupRegistry(config_.launchAtStartup);
    RenderNow();   // build the layered surface before first show

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    StartTimer();
    StartWatcher();
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
            if (wParam != TIMER_ID) break;
            auto now = std::chrono::system_clock::now();
            time_t tt = std::chrono::system_clock::to_time_t(now);
            tm utc; gmtime_s(&utc, &tt);
            bool changed = config_.showSeconds ? (utc.tm_sec != lastSecond_)
                                               : (utc.tm_min != lastMinute_);
            if (changed || lastMinute_ < 0) {
                lastSecond_ = utc.tm_sec;
                lastMinute_ = utc.tm_min;
                RenderNow();
            }
            ArmTimer();   // re-aim at the next boundary
            return 0;
        }

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

void GadgetWindow::StartTimer() {
    ArmTimer();
}

// A fixed 1000 ms SetTimer drifts: WM_TIMER is only delivered when the queue
// is empty, it coalesces, and its period is always >= the one requested. Two
// fires then land inside the same second, the redraw gate skips one, and the
// clock shows that second twice before jumping by two. Re-arming to the next
// boundary keeps every fire just after a real rollover.
void GadgetWindow::ArmTimer() {
    const UINT ms = MillisecondsToNextBoundary(std::chrono::system_clock::now(), true);
    SetTimer(hwnd_, TIMER_ID, ms, nullptr);
}

void GadgetWindow::ReloadConfig() {
    config_ = LoadConfig();
    renderer_.SetConfig(config_);
    ApplyTopmost();
    ApplyStartupRegistry(config_.launchAtStartup);
    lastMinute_ = lastSecond_ = -1; // force redraw
    RenderNow();
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

void GadgetWindow::PersistToggle(bool Config::* field, bool value) {
    Config c = LoadConfig();
    c.*field = value;
    WriteConfigFull(c);
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
            config_.alwaysOnTop = !config_.alwaysOnTop;
            ApplyTopmost();
            PersistToggle(&Config::alwaysOnTop, config_.alwaysOnTop);
            break;
        case ID_LOCK:
            config_.lockPosition = !config_.lockPosition;
            PersistToggle(&Config::lockPosition, config_.lockPosition);
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

void GadgetWindow::WatchThreadProc() {
    std::wstring dir = ConfigDir();
    HANDLE h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BYTE buf[4096];

    while (watchRun_.load()) {
        DWORD bytes = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = ReadDirectoryChangesW(h, buf, sizeof(buf), FALSE,
                                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                                        &bytes, &ov, nullptr);
        if (!ok) break;

        HANDLE waits[2] = { ov.hEvent, watchStop_ };
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0) break; // stop signaled

        DWORD transferred = 0;
        if (!GetOverlappedResult(h, &ov, &transferred, FALSE)) break;

        // Only the config file itself. Matching a substring would also fire on
        // the "config.json.tmp" that both writers replace through, turning one
        // save into several reloads -- the first of them reading the file
        // before the replace has landed.
        if (ContainsConfigChange(buf, transferred)) {
            Sleep(80); // let the writer finish the atomic replace
            PostMessageW(hwnd_, WM_APP_RELOAD, 0, 0);
        }
    }
    // The loop can exit with a ReadDirectoryChangesW still pending, and both
    // `ov` and `buf` are stack locals of this frame. Wait for the cancellation
    // to land before returning, or the kernel writes into a frame that is
    // already gone. The directory handle closes first; closing the event while
    // an I/O could still signal it risks signalling a recycled handle.
    CancelIoEx(h, &ov);
    DWORD cancelled = 0;
    GetOverlappedResult(h, &ov, &cancelled, TRUE);
    CloseHandle(h);
    CloseHandle(ov.hEvent);
}
