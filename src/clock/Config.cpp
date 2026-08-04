#include "Config.hpp"
#include "json.hpp"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shell32.lib")

namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    // strip UTF-8 BOM if present
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    // Replace destination atomically.
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
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

Config LoadConfig() {
    std::string text;
    if (!ReadFileUtf8(ConfigPath(), text)) return DefaultConfig();
    try {
        json::Value root = json::parse(text);
        if (!root.isObject()) return DefaultConfig();

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
        return DefaultConfig();
    }
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
    return WriteConfigFull(DefaultConfig());
}

WindowState LoadState() {
    WindowState st;
    std::string text;
    if (!ReadFileUtf8(StatePath(), text)) return st;
    try {
        json::Value root = json::parse(text);
        if (!root.isObject()) return st;
        st.windowX = root.getInt("windowX", st.windowX);
        st.windowY = root.getInt("windowY", st.windowY);
        st.monitor = root.getString("monitor", "");
        st.hasPosition = true;
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
