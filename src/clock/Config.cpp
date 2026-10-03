#include "Config.hpp"
#include "json.hpp"

#include <windows.h>
#include <shlobj.h>
#include <fstream>

#pragma comment(lib, "shell32.lib")

namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Both files are a few hundred bytes; anything this large is not ours.
constexpr LONGLONG kMaxFileBytes = 1 << 20;

enum class ReadResult { Ok, Missing, Failed };

bool IsNotFound(DWORD err) {
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
}

// Win32 rather than ifstream so "not there" can be told apart from "there but
// unreadable" -- the second must never be treated as permission to rewrite
// the file. FILE_SHARE_DELETE lets the settings app's replace go ahead while
// we are mid-read instead of failing on a sharing violation.
ReadResult ReadFileUtf8(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return IsNotFound(GetLastError()) ? ReadResult::Missing : ReadResult::Failed;

    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart <= kMaxFileBytes;
    if (ok && size.QuadPart > 0) {
        out.resize((size_t)size.QuadPart);
        DWORD got = 0;
        // A short read means the file changed under us; report it rather
        // than parse half a document.
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    if (!ok) { out.clear(); return ReadResult::Failed; }

    // strip UTF-8 BOM if present
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return ReadResult::Ok;
}

// The settings app writes config.json too. A replace can collide with its
// replace or with any reader that has the file open without FILE_SHARE_DELETE;
// both clear within milliseconds, so a few short retries ride them out.
constexpr int   kReplaceAttempts = 5;
constexpr DWORD kReplaceRetryMs  = 50;

bool IsTransientReplaceError(DWORD err) {
    return err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED ||
           err == ERROR_LOCK_VIOLATION;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
    // Our own temp name: the settings app replaces through
    // "config.json.settings.tmp", so neither writer can clobber the other's.
    std::wstring tmp = path + L".gadget.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    // Replace destination atomically.
    for (int attempt = 1;; ++attempt) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        if (attempt == kReplaceAttempts || !IsTransientReplaceError(GetLastError())) break;
        Sleep(kReplaceRetryMs);
    }
    DeleteFileW(tmp.c_str());
    return false;
}

} // namespace

std::wstring ConfigDir() {
    PWSTR appdata = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        dir = appdata;
        CoTaskMemFree(appdata);
    }
    dir += L"\\WorldClockGadget";
    CreateDirectoryW(dir.c_str(), nullptr); // ignore "already exists"
    dir += L"\\";
    return dir;
}

std::wstring ConfigPath() { return ConfigDir() + L"config.json"; }
std::wstring StatePath()  { return ConfigDir() + L"state.json"; }

Config DefaultConfig() {
    Config c;
    c.cities = {
        { "Tokyo",    "Asia/Tokyo" },
        { "Portland", "America/Los_Angeles" },
        { "London",   "Europe/London" },
        { "New York", "America/New_York" },
    };
    return c;
}

static DisplayMode ParseMode(const std::string& s) {
    return (s == "analog") ? DisplayMode::Analog : DisplayMode::Digital;
}
static SizeClass ParseSize(const std::string& s) {
    if (s == "small") return SizeClass::Small;
    if (s == "large") return SizeClass::Large;
    return SizeClass::Medium;
}
static LayoutDir ParseLayout(const std::string& s) {
    return (s == "horizontal") ? LayoutDir::Horizontal : LayoutDir::Vertical;
}

std::optional<Config> ParseConfig(const std::string& text) {
    try {
        json::Value root = json::parse(text);
        if (!root.isObject()) return std::nullopt;

        Config c = DefaultConfig();
        if (const json::Value* cities = root.find("cities"); cities && cities->isArray()) {
            c.cities.clear();
            for (const auto& cv : cities->arr) {
                CityEntry e;
                e.label = cv.getString("label", "");
                e.tz    = cv.getString("tz", "");
                if (!e.tz.empty()) c.cities.push_back(std::move(e));
            }
            if (c.cities.empty()) c.cities = DefaultConfig().cities;
        }
        c.displayMode  = ParseMode(root.getString("displayMode", "digital"));
        c.layout       = ParseLayout(root.getString("layout", "vertical"));
        c.hourFormat   = root.getInt("hourFormat", 24) == 12 ? 12 : 24;
        c.showDate     = root.getBool("showDate", true);
        c.dateFormat   = root.getString("dateFormat", "ddd, MMM d");
        if (c.dateFormat.empty()) c.dateFormat = "ddd, MMM d";
        c.showSeconds  = root.getBool("showSeconds", false);
        c.size         = ParseSize(root.getString("size", "medium"));
        c.opacity      = root.getInt("opacity", 85);
        if (c.opacity < 10) c.opacity = 10;     // keep it visible
        if (c.opacity > 100) c.opacity = 100;
        c.alwaysOnTop  = root.getBool("alwaysOnTop", false);
        c.lockPosition = root.getBool("lockPosition", false);
        c.launchAtStartup = root.getBool("launchAtStartup", false);
        c.theme        = root.getString("theme", "dark");
        return c;
    } catch (...) {
        return std::nullopt;
    }
}

ConfigLoadResult LoadConfigFile(const std::wstring& path) {
    ConfigLoadResult r;
    r.config = DefaultConfig();

    std::string text;
    switch (ReadFileUtf8(path, text)) {
        case ReadResult::Missing: r.status = ConfigLoadStatus::Missing;    return r;
        case ReadResult::Failed:  r.status = ConfigLoadStatus::ReadFailed; return r;
        case ReadResult::Ok:      break;
    }
    if (std::optional<Config> parsed = ParseConfig(text)) {
        r.status = ConfigLoadStatus::Ok;
        r.config = std::move(*parsed);
    } else {
        r.status = ConfigLoadStatus::ParseFailed;
    }
    return r;
}

ConfigLoadResult LoadConfig() { return LoadConfigFile(ConfigPath()); }

std::optional<Config> BaseForEdit(const ConfigLoadResult& onDisk, const Config& displayed) {
    switch (onDisk.status) {
        case ConfigLoadStatus::Ok:      return onDisk.config;
        case ConfigLoadStatus::Missing: return displayed;
        default:                        return std::nullopt;
    }
}

bool NeedsApply(bool shownIsFromFile, const Config& shown, const Config& loaded) {
    return !shownIsFromFile || shown != loaded;
}

static std::string ModeStr(DisplayMode m) { return m == DisplayMode::Analog ? "analog" : "digital"; }
static std::string LayoutStr(LayoutDir l) { return l == LayoutDir::Horizontal ? "horizontal" : "vertical"; }
static std::string SizeStr(SizeClass s) {
    switch (s) { case SizeClass::Small: return "small";
                 case SizeClass::Large: return "large";
                 default: return "medium"; }
}

bool WriteConfigFull(const Config& c) {
    json::Value root; root.type = json::Type::Object;
    json::Value arr;  arr.type  = json::Type::Array;
    for (auto& e : c.cities) {
        json::Value o; o.type = json::Type::Object;
        o.set("label", e.label);
        o.set("tz", e.tz);
        arr.arr.push_back(std::move(o));
    }
    root.set("cities", std::move(arr));
    root.set("displayMode", ModeStr(c.displayMode));
    root.set("layout", LayoutStr(c.layout));
    root.set("hourFormat", c.hourFormat);
    root.set("showDate", c.showDate);
    root.set("dateFormat", c.dateFormat);
    root.set("showSeconds", c.showSeconds);
    root.set("size", SizeStr(c.size));
    root.set("opacity", c.opacity);
    root.set("alwaysOnTop", c.alwaysOnTop);
    root.set("lockPosition", c.lockPosition);
    root.set("launchAtStartup", c.launchAtStartup);
    root.set("theme", c.theme);
    return WriteFileAtomic(ConfigPath(), json::dump(root));
}

bool WriteDefaultConfigIfMissing() {
    if (GetFileAttributesW(ConfigPath().c_str()) != INVALID_FILE_ATTRIBUTES)
        return true; // already exists
    if (!IsNotFound(GetLastError()))
        return false; // could not look; it may well exist, so never overwrite
    return WriteConfigFull(DefaultConfig());
}

WindowState LoadState() {
    WindowState st;
    std::string text;
    if (ReadFileUtf8(StatePath(), text) != ReadResult::Ok) return st;
    try {
        json::Value root = json::parse(text);
        if (!root.isObject()) return st;
        // Only treat this as a saved position when both coordinates are
        // really there; getInt would otherwise hand back its own default and
        // we would restore to a position nobody ever saved.
        const json::Value* px = root.find("windowX");
        const json::Value* py = root.find("windowY");
        st.windowX = root.getInt("windowX", st.windowX);
        st.windowY = root.getInt("windowY", st.windowY);
        st.monitor = root.getString("monitor", "");
        st.hasPosition = px && py;
    } catch (...) {}
    return st;
}

bool SaveState(const WindowState& s) {
    json::Value root; root.type = json::Type::Object;
    root.set("windowX", s.windowX);
    root.set("windowY", s.windowY);
    if (!s.monitor.empty()) root.set("monitor", s.monitor);
    return WriteFileAtomic(StatePath(), json::dump(root));
}
