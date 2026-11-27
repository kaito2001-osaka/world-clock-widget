#include "Config.hpp"
#include "json.hpp"

#include <windows.h>
#include <shlobj.h>
#include <climits>

#pragma comment(lib, "shell32.lib")

namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

enum class ReadResult { Ok, Missing, Failed, TooLarge };

bool IsNotFound(DWORD err) {
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
}

// Win32 rather than ifstream so "not there" can be told apart from "there but
// unreadable" -- the second must never be treated as permission to rewrite
// the file. FILE_SHARE_DELETE lets the settings app's replace go ahead while
// we are mid-read instead of failing on a sharing violation.
ReadResult ReadFileUtf8(const std::wstring& path, std::string& out) {
    out.clear();
    // No settings folder (see ConfigStorageAvailable): there is nothing to
    // read, and opening "" must not be left to whatever the API makes of it.
    if (path.empty()) return ReadResult::Missing;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return IsNotFound(GetLastError()) ? ReadResult::Missing : ReadResult::Failed;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) { CloseHandle(h); return ReadResult::Failed; }
    // Its own result: reporting it as "locked or access denied" sends the
    // user looking for a cause that is not there (#87).
    if (size.QuadPart > kMaxConfigFileBytes) { CloseHandle(h); return ReadResult::TooLarge; }
    bool ok = true;
    if (size.QuadPart > 0) {
        out.resize((size_t)size.QuadPart);
        DWORD got = 0;
        // A short read means the file changed under us; report it rather
        // than parse half a document.
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    if (!ok) { out.clear(); return ReadResult::Failed; }

    out = DecodeConfigBytes(std::move(out));
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
    if (path.empty()) return false;   // no settings folder; write nowhere
    // Our own temp name: the settings app replaces through
    // "config.json.settings.tmp", so neither writer can clobber the other's.
    std::wstring tmp = path + L".gadget.tmp";
    {
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        // Flush before the rename: MOVEFILE_WRITE_THROUGH flushes only a
        // copy-and-delete move, so after a crash or power cut a same-volume
        // rename can be on disk while the temp file's data is not, leaving
        // the real file empty or zero-filled.
        const bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) &&
                        written == data.size() && FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok) { DeleteFileW(tmp.c_str()); return false; }
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

std::wstring EnvAppData() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
}

// The roaming AppData folder, resolved on first use and then kept, or empty
// when there is none. It is the gadget's only use of shell32, so after this
// the exe can unload it. The known-folder API comes first because that is what
// the settings app's Environment.GetFolderPath uses; the two must agree.
// %APPDATA% only stands in when that call fails.
const std::wstring& RoamingAppData() {
    static const std::wstring dir = [] {
        PWSTR appdata = nullptr;
        std::wstring known;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
            known = appdata;
        }
        CoTaskMemFree(appdata);   // allocated even on failure; null is fine
        return ChooseAppDataDir(known, EnvAppData());
    }();
    return dir;
}

bool IsAbsolutePath(const std::wstring& p) {
    const auto isSep = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    if (p.size() >= 3 && ((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) &&
        p[1] == L':' && isSep(p[2]))
        return true;                                   // C:\...
    return p.size() >= 3 && isSep(p[0]) && isSep(p[1]) && !isSep(p[2]);   // \\server\...
}

} // namespace

static bool StartsWith(const std::string& s, const char* bom, size_t n) {
    return s.size() >= n && s.compare(0, n, bom, n) == 0;
}

// UTF-8 with every ill-formed sequence replaced by one U+FFFD per maximal
// subpart (Unicode 3.9, U+FFFD substitution), which is what .NET's UTF-8
// decoder does. Without this, a stray byte would reach a label in the gadget
// as is while the settings app read U+FFFD (#87). Well-formed text is
// returned unchanged.
static std::string ReplaceIllFormedUtf8(std::string bytes, size_t start) {
    const auto at = [&](size_t i) { return (unsigned)(unsigned char)bytes[i]; };
    std::string out;
    out.reserve(bytes.size() - start);
    size_t i = start;
    while (i < bytes.size()) {
        const unsigned b0 = at(i);
        if (b0 < 0x80) { out += (char)b0; ++i; continue; }

        // Table 3-7: the length a lead byte asks for, and the range its
        // second byte must fall in (it rules out overlongs, surrogates and
        // code points past U+10FFFF). Later bytes are all 80..BF.
        size_t need = 0;
        unsigned lo = 0x80, hi = 0xBF;
        if (b0 >= 0xC2 && b0 <= 0xDF)      need = 1;
        else if (b0 == 0xE0)               { need = 2; lo = 0xA0; }
        else if (b0 == 0xED)               { need = 2; hi = 0x9F; }
        else if (b0 >= 0xE1 && b0 <= 0xEF) need = 2;
        else if (b0 == 0xF0)               { need = 3; lo = 0x90; }
        else if (b0 == 0xF4)               { need = 3; hi = 0x8F; }
        else if (b0 >= 0xF1 && b0 <= 0xF3) need = 3;

        size_t got = 0;   // continuation bytes that fit
        while (got < need && i + 1 + got < bytes.size()) {
            const unsigned b = at(i + 1 + got);
            const bool fits = got == 0 ? (b >= lo && b <= hi) : (b >= 0x80 && b <= 0xBF);
            if (!fits) break;
            ++got;
        }
        if (need != 0 && got == need) {
            out.append(bytes, i, need + 1);
        } else {
            // The lead byte and the continuation bytes that fit are one
            // maximal subpart; the byte that did not fit starts afresh.
            json::appendUtf8(out, 0xFFFD);
        }
        i += 1 + got;
    }
    return out;
}

std::string DecodeConfigBytes(std::string bytes) {
    const auto at = [&](size_t i) { return (unsigned)(unsigned char)bytes[i]; };

    if (StartsWith(bytes, "\xEF\xBB\xBF", 3)) return ReplaceIllFormedUtf8(std::move(bytes), 3);

    // UTF-32LE's BOM begins with UTF-16LE's, so it is checked first; .NET
    // decides the same way.
    const bool utf32le = StartsWith(bytes, "\xFF\xFE\0\0", 4);
    const bool utf32be = StartsWith(bytes, "\0\0\xFE\xFF", 4);
    if (utf32le || utf32be) {
        std::string out;
        size_t i = 4;
        for (; i + 4 <= bytes.size(); i += 4) {
            json::appendUtf8(out, utf32le
                ? (at(i + 3) << 24 | at(i + 2) << 16 | at(i + 1) << 8 | at(i))
                : (at(i) << 24 | at(i + 1) << 16 | at(i + 2) << 8 | at(i + 3)));
        }
        if (i < bytes.size()) json::appendUtf8(out, 0xFFFD);   // 1-3 bytes left over
        return out;
    }

    const bool utf16le = StartsWith(bytes, "\xFF\xFE", 2);
    const bool utf16be = StartsWith(bytes, "\xFE\xFF", 2);
    if (utf16le || utf16be) {
        const auto unit = [&](size_t i) {
            return utf16le ? (at(i + 1) << 8 | at(i)) : (at(i) << 8 | at(i + 1));
        };
        std::string out;
        size_t i = 2;
        while (i + 2 <= bytes.size()) {
            unsigned cp = unit(i);
            i += 2;
            // A high surrogate joins only with a low one right after it;
            // anything else is unpaired, and appendUtf8 makes it U+FFFD.
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 2 <= bytes.size()) {
                const unsigned lo = unit(i);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            json::appendUtf8(out, cp);
        }
        if (i < bytes.size()) json::appendUtf8(out, 0xFFFD);   // odd byte left over
        return out;
    }
    return ReplaceIllFormedUtf8(std::move(bytes), 0);
}

std::wstring ChooseAppDataDir(const std::wstring& knownFolder, const std::wstring& envAppData) {
    for (const std::wstring* candidate : { &knownFolder, &envAppData }) {
        if (!IsAbsolutePath(*candidate)) continue;
        std::wstring d = *candidate;
        // Keep the root's separator ("C:\"), drop any other trailing one, so
        // appending "\WorldClockGadget" never doubles it.
        while (d.size() > 3 && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
        if (d.size() == 3 && d[1] == L':') d.pop_back();
        return d;
    }
    return {};
}

bool ConfigStorageAvailable() { return !RoamingAppData().empty(); }

std::wstring ConfigDir() {
    // Without AppData, "" + "\WorldClockGadget" would be a folder at the root of
    // the current drive, which other users can write to. Use nothing instead.
    if (!ConfigStorageAvailable()) return {};
    std::wstring dir = RoamingAppData() + L"\\WorldClockGadget";
    // On every call, not once: the folder can be deleted while we run, and the
    // writers and the watcher's reopen rely on getting it back here.
    CreateDirectoryW(dir.c_str(), nullptr); // ignore "already exists"
    dir += L"\\";
    return dir;
}

// Empty, not a bare relative "config.json", when there is no folder.
static std::wstring InConfigDir(const wchar_t* name) {
    std::wstring dir = ConfigDir();
    return dir.empty() ? dir : dir + name;
}

std::wstring ConfigPath() { return InConfigDir(L"config.json"); }
std::wstring StatePath()  { return InConfigDir(L"state.json"); }

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

void TruncateUtf8(std::string& s, size_t maxCodePoints) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        // Every byte that is not a continuation byte (10xxxxxx) starts a code
        // point, so cutting before one never splits a sequence.
        if (((unsigned char)s[i] & 0xC0) != 0x80 && count++ == maxCodePoints) {
            s.resize(i);
            return;
        }
    }
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
                if (c.cities.size() == kMaxCities) break;
                CityEntry e;
                e.label = cv.getString("label", "");
                e.tz    = cv.getString("tz", "");
                TruncateUtf8(e.label, kMaxLabelChars);
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
        TruncateUtf8(c.dateFormat, kMaxDateFormatChars);
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
        case ReadResult::TooLarge: r.status = ConfigLoadStatus::TooLarge;  return r;
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

bool NeedsApply(bool shownIsFromFile, const Config& shown, const Config& loaded) {
    return !shownIsFromFile || shown != loaded;
}

// The file's key for each boolean setting, kept in one place so a one-key edit
// (EditConfigFileBool) and the full writer cannot drift apart. Null for a
// member that is not written to the file.
static const char* BoolKey(bool Config::* field) {
    if (field == &Config::showDate)        return "showDate";
    if (field == &Config::showSeconds)     return "showSeconds";
    if (field == &Config::alwaysOnTop)     return "alwaysOnTop";
    if (field == &Config::lockPosition)    return "lockPosition";
    if (field == &Config::launchAtStartup) return "launchAtStartup";
    return nullptr;
}

static std::string ModeStr(DisplayMode m) { return m == DisplayMode::Analog ? "analog" : "digital"; }
static std::string LayoutStr(LayoutDir l) { return l == LayoutDir::Horizontal ? "horizontal" : "vertical"; }
static std::string SizeStr(SizeClass s) {
    switch (s) { case SizeClass::Small: return "small";
                 case SizeClass::Large: return "large";
                 default: return "medium"; }
}

bool WriteConfigFile(const std::wstring& path, const Config& c) {
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
    root.set(BoolKey(&Config::showDate), c.showDate);
    root.set("dateFormat", c.dateFormat);
    root.set(BoolKey(&Config::showSeconds), c.showSeconds);
    root.set("size", SizeStr(c.size));
    root.set("opacity", c.opacity);
    root.set(BoolKey(&Config::alwaysOnTop), c.alwaysOnTop);
    root.set(BoolKey(&Config::lockPosition), c.lockPosition);
    root.set(BoolKey(&Config::launchAtStartup), c.launchAtStartup);
    root.set("theme", c.theme);
    return WriteFileAtomic(path, json::dump(root));
}

bool WriteConfigFull(const Config& c) { return WriteConfigFile(ConfigPath(), c); }

std::optional<std::string> EditConfigText(const std::string& text, const std::string& key, bool value) {
    try {
        json::Value root = json::parse(text);
        if (!root.isObject()) return std::nullopt;
        root.set(key, value);
        return json::dump(root);
    } catch (...) {
        return std::nullopt;
    }
}

ConfigEditResult EditConfigFileBool(const std::wstring& path, bool Config::* field, bool value,
                                    const Config& displayed) {
    const char* key = BoolKey(field);
    if (!key) return ConfigEditResult::WriteFailed;   // not a field the file has

    std::string text;
    switch (ReadFileUtf8(path, text)) {
        case ReadResult::Failed:
            return ConfigEditResult::ReadFailed;
        case ReadResult::TooLarge:
            return ConfigEditResult::TooLarge;
        case ReadResult::Missing: {
            Config c = displayed;
            c.*field = value;
            return WriteConfigFile(path, c) ? ConfigEditResult::Written
                                            : ConfigEditResult::WriteFailed;
        }
        case ReadResult::Ok:
            break;
    }
    const std::optional<std::string> edited = EditConfigText(text, key, value);
    if (!edited) return ConfigEditResult::ParseFailed;
    return WriteFileAtomic(path, *edited) ? ConfigEditResult::Written
                                          : ConfigEditResult::WriteFailed;
}

bool WriteDefaultConfigIfMissing() {
    const std::wstring path = ConfigPath();
    if (path.empty()) return false;   // no settings folder
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true; // already exists
    if (!IsNotFound(GetLastError()))
        return false; // could not look; it may well exist, so never overwrite
    return WriteConfigFull(DefaultConfig());
}

WindowState ParseState(const std::string& text) {
    WindowState st;
    try {
        json::Value root = json::parse(text);
        if (!root.isObject()) return st;
        st.monitor = root.getString("monitor", "");
        // Only treat this as a saved position when both coordinates are
        // really there; getInt would otherwise hand back its own default and
        // we would restore to a position nobody ever saved.
        const json::Value* px = root.find("windowX");
        const json::Value* py = root.find("windowY");
        if (!px || !py) return st;
        // INT_MIN cannot be a saved coordinate here, so it marks "not an
        // in-range number" without a second lookup.
        const int x = root.getInt("windowX", INT_MIN);
        const int y = root.getInt("windowY", INT_MIN);
        auto inRange = [](int v) { return v >= -kMaxSavedCoord && v <= kMaxSavedCoord; };
        if (!inRange(x) || !inRange(y)) return st;
        st.windowX = x;
        st.windowY = y;
        st.hasPosition = true;
    } catch (...) {}
    return st;
}

WindowState LoadState() {
    std::string text;
    if (ReadFileUtf8(StatePath(), text) != ReadResult::Ok) return WindowState{};
    return ParseState(text);
}

bool SaveState(const WindowState& s) {
    json::Value root; root.type = json::Type::Object;
    root.set("windowX", s.windowX);
    root.set("windowY", s.windowY);
    if (!s.monitor.empty()) root.set("monitor", s.monitor);
    return WriteFileAtomic(StatePath(), json::dump(root));
}
