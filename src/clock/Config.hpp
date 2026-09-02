// Configuration & state model for the World Clock gadget.
// config.json  -> owned by the settings app (cities, display options)
// state.json   -> owned by this gadget (window position)
#pragma once
#include <string>
#include <vector>

struct CityEntry {
    std::string label;   // display name, e.g. "Tokyo"
    std::string tz;      // IANA tz id, e.g. "Asia/Tokyo"
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
};

struct WindowState {
    int windowX = 1200;
    int windowY = 80;
    std::string monitor;   // optional device name, informational
    bool hasPosition = false;
};

// Returns %APPDATA%\WorldClockGadget\ (created if missing). Wide path.
std::wstring ConfigDir();
std::wstring ConfigPath();   // config.json
std::wstring StatePath();    // state.json

Config       DefaultConfig();
// Parse config.json text. Pure (no file system), so it is unit testable;
// anything missing, malformed or out of range falls back to DefaultConfig().
Config       ConfigFromJson(const std::string& text);
Config       LoadConfig();              // reads config.json (defaults if absent)
bool         WriteDefaultConfigIfMissing();
bool         WriteConfigFull(const Config& c);  // serialize full config -> config.json

WindowState  LoadState();
bool         SaveState(const WindowState& s);
