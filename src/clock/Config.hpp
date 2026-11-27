// Configuration & state model for the World Clock gadget.
// config.json  -> owned by the settings app (cities, display options)
// state.json   -> owned by this gadget (window position)
#pragma once
#include <optional>
#include <string>
#include <vector>

struct CityEntry {
    std::string label;   // display name, e.g. "Tokyo"
    std::string tz;      // IANA tz id, e.g. "Asia/Tokyo"

    bool operator==(const CityEntry&) const = default;
};

enum class DisplayMode { Digital, Analog };
enum class SizeClass   { Small, Medium, Large };
enum class LayoutDir   { Vertical, Horizontal };

struct Config {
    std::vector<CityEntry> cities;
    DisplayMode displayMode = DisplayMode::Digital;
    LayoutDir layout = LayoutDir::Vertical;   // stack cities vertically or side by side
    int  hourFormat = 24;        // 12 | 24
    bool showDate   = true;
    // Date format pattern. Tokens: yyyy yy / MMMM MMM MM M / dddd ddd dd d /
    // aaaa aaa (Japanese weekday). Other characters are literal.
    std::string dateFormat = "ddd, MMM d";
    bool showSeconds = false;
    SizeClass size  = SizeClass::Medium;
    int  opacity    = 85;        // 0..100
    bool alwaysOnTop = false;
    bool lockPosition = false;
    bool launchAtStartup = false;   // register in HKCU\...\Run
    std::string theme = "dark";

    // Pixel scale derived from size class (logical, pre-DPI).
    double scale() const {
        switch (size) {
            case SizeClass::Small:  return 0.8;
            case SizeClass::Large:  return 1.3;
            default:                return 1.0;
        }
    }

    // Defaulted so a field added later cannot be left out of the comparison
    // and have its changes skipped on reload (see NeedsApply).
    bool operator==(const Config&) const = default;
};

struct WindowState {
    int windowX = 1200;
    int windowY = 80;
    std::string monitor;   // optional device name, informational
    bool hasPosition = false;
};

// The folder the settings live under: the roaming AppData known folder, or
// %APPDATA% when that cannot be resolved. Each must be an absolute path
// ("X:\..." or "\\server\..."); anything else is skipped. Empty when neither
// is usable. A trailing separator is dropped. Pure, so it is unit testable.
std::wstring ChooseAppDataDir(const std::wstring& knownFolder, const std::wstring& envAppData);

// Whether there is a settings folder at all. Without one the gadget runs on
// the defaults and reads and writes nothing (#56).
bool ConfigStorageAvailable();

// Returns %APPDATA%\WorldClockGadget\ (created if missing). Wide path. These
// three return an empty string when !ConfigStorageAvailable(), and the
// readers and writers below treat an empty path as "no file" / "cannot write".
std::wstring ConfigDir();
std::wstring ConfigPath();   // config.json
std::wstring StatePath();    // state.json

Config       DefaultConfig();

// Upper bounds ParseConfig enforces. The panel is sized from the worst-case
// string of every city, so without them a hand-edited config.json could ask
// for a bitmap of tens of MB, or one too large to create at all (#54). The
// settings app (ConfigModel.cs) enforces the same numbers. Lengths count code
// points; the settings app counts UTF-16 units, so what it saves always fits.
constexpr size_t kMaxLabelChars      = 64;
constexpr size_t kMaxDateFormatChars = 64;
constexpr size_t kMaxCities          = 32;

// Cut `s` to at most `maxCodePoints` code points, never inside a UTF-8
// sequence.
void TruncateUtf8(std::string& s, size_t maxCodePoints);

// Parse config.json text. Pure (no file system), so it is unit testable.
// Returns nullopt when the text is not a JSON object at all: a typo is the
// caller's to handle, not something to silently turn into the defaults.
// Inside a valid document, a field that is missing, mistyped or out of range
// falls back to its DefaultConfig() value on its own.
std::optional<Config> ParseConfig(const std::string& text);

// The largest config.json either app reads, in bytes on disk. Ours are a few
// hundred bytes; anything larger is not a settings file. The settings app
// (ConfigModel.MaxFileBytes) uses the same number, so both refuse the same
// files for the same reason (#87).
constexpr long long kMaxConfigFileBytes = 1 << 20;

// The bytes of a config file as UTF-8 text, the way the settings app's
// File.ReadAllText reads them: a UTF-8 BOM is dropped, and a UTF-32 or UTF-16
// BOM (either byte order) is decoded to UTF-8, so a file saved by Windows
// PowerShell 5.1's Out-File opens in both apps (#87). Without a BOM the bytes
// are read as UTF-8. A sequence that cannot be decoded (ill-formed UTF-8, an
// unpaired surrogate, a dangling odd byte, a code point past U+10FFFF) becomes
// U+FFFD, as in .NET. Pure, so it is unit testable.
std::string DecodeConfigBytes(std::string bytes);

enum class ConfigLoadStatus {
    Ok,           // parsed; `config` holds the file's settings
    Missing,      // there is no file
    ReadFailed,   // it exists but could not be read (locked, access denied)
    TooLarge,     // it is larger than kMaxConfigFileBytes; not read
    ParseFailed,  // it was read but is not a JSON object
};

struct ConfigLoadResult {
    ConfigLoadStatus status = ConfigLoadStatus::Missing;
    Config config;   // the file's settings when Ok, DefaultConfig() otherwise
};

ConfigLoadResult LoadConfigFile(const std::wstring& path);
ConfigLoadResult LoadConfig();          // LoadConfigFile(ConfigPath())

// config.json text with only `key` set to `value`, or nullopt when the text is
// not a JSON object (the same texts ParseConfig rejects). Pure, so it is unit
// testable. Edits the document itself, not a parsed Config: ParseConfig
// normalizes for display (drops cities without a zone and past kMaxCities,
// cuts long strings, defaults unknown values, ignores unknown keys), and
// writing that back would delete whatever it left out (#86). Every other key
// keeps its value and its place; a missing `key` is appended.
std::optional<std::string> EditConfigText(const std::string& text, const std::string& key, bool value);

enum class ConfigEditResult {
    Written,
    ReadFailed,    // the file exists but could not be read; left alone
    TooLarge,      // the file is larger than kMaxConfigFileBytes; left alone
    ParseFailed,   // the file is not a JSON object; left alone
    WriteFailed,   // the edit could not be written; the old file stands
};

// Set one boolean setting in the config file at `path`. A file that exists
// but could not be read or parsed still holds the user's settings; rewriting
// it from the defaults would destroy them, so the edit is refused instead
// (#23). A missing file has nothing to lose, so `displayed` is written with
// the field set.
ConfigEditResult EditConfigFileBool(const std::wstring& path, bool Config::* field, bool value,
                                    const Config& displayed);

// Whether a config that just loaded has to be applied. One save can reach the
// watcher as several notifications, and an overflow or a reopened watch asks
// for a reload with nothing changed; applying the same settings again rebuilds
// every text format for nothing. `shownIsFromFile` is false while the defaults
// stand in for a file that could not be read at startup: those have never been
// applied as the user's settings (the Run key was not reconciled), so the
// first good load is applied even when it equals them.
bool NeedsApply(bool shownIsFromFile, const Config& shown, const Config& loaded);

// Only writes when the file is known to be absent; any other failure to look
// at it leaves it alone.
bool         WriteDefaultConfigIfMissing();
// Serialize the full config and replace `path` with it. Leaves no temp file
// behind on failure.
bool         WriteConfigFile(const std::wstring& path, const Config& c);
bool         WriteConfigFull(const Config& c);  // WriteConfigFile(ConfigPath(), c)

// The largest |windowX| / |windowY| ParseState accepts. Real virtual desktops
// span tens of thousands of pixels; past this bound adding the window size to
// a coordinate could overflow int, which is undefined behaviour (#57).
constexpr int kMaxSavedCoord = 1'000'000;

// Parse state.json text. Pure (no file system), so it is unit testable. A
// position counts as saved only when both coordinates are present and within
// kMaxSavedCoord; otherwise hasPosition is false and the defaults stand.
WindowState  ParseState(const std::string& text);

WindowState  LoadState();            // ParseState of StatePath()'s contents
bool         SaveState(const WindowState& s);
