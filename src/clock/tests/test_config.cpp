// Tests for config.json parsing, defaults and clamping (Config.hpp).
#include "test_framework.hpp"
#include "Config.hpp"

#include <windows.h>
#include <cstring>
#include <string>

namespace {

// ParseConfig for documents the test expects to be valid. A nullopt fails the
// test here instead of being dereferenced.
Config Parsed(const char* text) {
    std::optional<Config> c = ParseConfig(text);
    CHECK(c.has_value());
    return c ? *c : DefaultConfig();
}

// A scratch file under %TEMP% that removes itself.
struct TempFile {
    std::wstring path;
    explicit TempFile(const char* tag) {
        wchar_t dir[MAX_PATH];
        GetTempPathW(MAX_PATH, dir);
        path = std::wstring(dir) + L"worldclock_test_" + std::to_wstring(GetCurrentProcessId())
             + L"_" + std::wstring(tag, tag + strlen(tag)) + L".json";
        DeleteFileW(path.c_str());
    }
    ~TempFile() { DeleteFileW(path.c_str()); }
    void Write(const std::string& bytes) const {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD n = 0;
        WriteFile(h, bytes.data(), (DWORD)bytes.size(), &n, nullptr);
        CloseHandle(h);
    }
};

} // namespace

TEST(config_defaults_have_four_cities) {
    Config c = DefaultConfig();
    CHECK_EQ(c.cities.size(), (size_t)4);
    CHECK_EQ(c.cities[0].label, std::string("Tokyo"));
    CHECK_EQ(c.cities[0].tz, std::string("Asia/Tokyo"));
    CHECK_EQ(c.hourFormat, 24);
    CHECK_EQ(c.opacity, 85);
    CHECK_EQ(c.showDate, true);
    CHECK_EQ(c.showSeconds, false);
}

TEST(config_parses_a_full_document) {
    Config c = Parsed(R"({
        "cities": [{"label":"Osaka","tz":"Asia/Tokyo"}],
        "displayMode": "analog",
        "layout": "horizontal",
        "hourFormat": 12,
        "showDate": false,
        "dateFormat": "yyyy-MM-dd",
        "showSeconds": true,
        "size": "large",
        "opacity": 40,
        "alwaysOnTop": true,
        "lockPosition": true,
        "launchAtStartup": true,
        "theme": "light"
    })");
    CHECK_EQ(c.cities.size(), (size_t)1);
    CHECK_EQ(c.cities[0].label, std::string("Osaka"));
    CHECK_EQ(c.displayMode == DisplayMode::Analog, true);
    CHECK_EQ(c.layout == LayoutDir::Horizontal, true);
    CHECK_EQ(c.hourFormat, 12);
    CHECK_EQ(c.showDate, false);
    CHECK_EQ(c.dateFormat, std::string("yyyy-MM-dd"));
    CHECK_EQ(c.showSeconds, true);
    CHECK_EQ(c.size == SizeClass::Large, true);
    CHECK_EQ(c.opacity, 40);
    CHECK_EQ(c.alwaysOnTop, true);
    CHECK_EQ(c.lockPosition, true);
    CHECK_EQ(c.launchAtStartup, true);
    CHECK_EQ(c.theme, std::string("light"));
}

TEST(config_clamps_opacity_into_visible_range) {
    CHECK_EQ(Parsed(R"({"opacity": 0})").opacity, 10);
    CHECK_EQ(Parsed(R"({"opacity": -50})").opacity, 10);
    CHECK_EQ(Parsed(R"({"opacity": 101})").opacity, 100);
    CHECK_EQ(Parsed(R"({"opacity": 100000})").opacity, 100);
    // In-range values pass through, including the boundaries.
    CHECK_EQ(Parsed(R"({"opacity": 10})").opacity, 10);
    CHECK_EQ(Parsed(R"({"opacity": 100})").opacity, 100);
    CHECK_EQ(Parsed(R"({"opacity": 55})").opacity, 55);
}

TEST(config_empty_city_list_falls_back_to_defaults) {
    Config c = Parsed(R"({"cities": []})");
    CHECK_EQ(c.cities.size(), (size_t)4);
}

TEST(config_drops_cities_without_a_timezone) {
    Config c = Parsed(
        R"({"cities":[{"label":"Good","tz":"Asia/Tokyo"},{"label":"NoTz"}]})");
    CHECK_EQ(c.cities.size(), (size_t)1);
    CHECK_EQ(c.cities[0].label, std::string("Good"));
}

TEST(config_all_cities_invalid_falls_back_to_defaults) {
    Config c = Parsed(R"({"cities":[{"label":"NoTz"}]})");
    CHECK_EQ(c.cities.size(), (size_t)4);
}

TEST(config_unknown_enum_values_use_the_default_arm) {
    CHECK_EQ(Parsed(R"({"displayMode":"wristwatch"})").displayMode == DisplayMode::Digital, true);
    CHECK_EQ(Parsed(R"({"layout":"diagonal"})").layout == LayoutDir::Vertical, true);
    CHECK_EQ(Parsed(R"({"size":"enormous"})").size == SizeClass::Medium, true);
}

TEST(config_hour_format_accepts_only_12_or_24) {
    CHECK_EQ(Parsed(R"({"hourFormat": 12})").hourFormat, 12);
    CHECK_EQ(Parsed(R"({"hourFormat": 24})").hourFormat, 24);
    CHECK_EQ(Parsed(R"({"hourFormat": 13})").hourFormat, 24);
    CHECK_EQ(Parsed(R"({"hourFormat": 0})").hourFormat, 24);
}

TEST(config_empty_date_format_falls_back) {
    CHECK_EQ(Parsed(R"({"dateFormat": ""})").dateFormat, std::string("ddd, MMM d"));
    CHECK_EQ(Parsed("{}").dateFormat, std::string("ddd, MMM d"));
}

TEST(config_malformed_json_is_reported_not_turned_into_defaults) {
    // Reporting the failure (rather than returning the defaults) is what lets
    // callers keep the user's file instead of rewriting it from the defaults.
    CHECK(!ParseConfig("{ this is not json").has_value());
    CHECK(!ParseConfig("").has_value());
    // Valid JSON, but not an object.
    CHECK(!ParseConfig("[1, 2, 3]").has_value());
    // The reported case: a hand edit that leaves a trailing comma.
    CHECK(!ParseConfig(R"({"cities":[{"label":"Osaka","tz":"Asia/Tokyo"}],})").has_value());
}

TEST(config_load_file_tells_missing_unreadable_and_malformed_apart) {
    TempFile f("load_status");

    ConfigLoadResult missing = LoadConfigFile(f.path);
    CHECK(missing.status == ConfigLoadStatus::Missing);
    CHECK_EQ(missing.config.cities.size(), (size_t)4);

    f.Write(R"({"cities":[{"label":"Osaka","tz":"Asia/Tokyo"}]})");
    ConfigLoadResult ok = LoadConfigFile(f.path);
    CHECK(ok.status == ConfigLoadStatus::Ok);
    CHECK_EQ(ok.config.cities.size(), (size_t)1);
    CHECK_EQ(ok.config.cities[0].label, std::string("Osaka"));

    // A UTF-8 BOM (Notepad's default) is not a syntax error.
    f.Write("\xEF\xBB\xBF{\"opacity\": 40}");
    ConfigLoadResult bom = LoadConfigFile(f.path);
    CHECK(bom.status == ConfigLoadStatus::Ok);
    CHECK_EQ(bom.config.opacity, 40);

    f.Write(R"({"cities":[{"label":"Osaka","tz":"Asia/Tokyo"}],})");
    CHECK(LoadConfigFile(f.path).status == ConfigLoadStatus::ParseFailed);

    // An empty file is a document with a syntax error, not a missing one.
    f.Write("");
    CHECK(LoadConfigFile(f.path).status == ConfigLoadStatus::ParseFailed);
}

TEST(config_load_file_reports_a_locked_file_as_unreadable) {
    TempFile f("locked");
    f.Write(R"({"opacity": 40})");

    // Held with no sharing at all, as an exclusive writer or scanner would.
    HANDLE lock = CreateFileW(f.path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    ConfigLoadResult r = LoadConfigFile(f.path);
    CloseHandle(lock);

    CHECK(r.status == ConfigLoadStatus::ReadFailed);
    CHECK_EQ(r.config.opacity, 85);   // defaults, but flagged as not the file's
    CHECK(LoadConfigFile(f.path).status == ConfigLoadStatus::Ok);
}

TEST(config_edit_is_refused_when_the_file_could_not_be_read) {
    Config displayed = DefaultConfig();
    displayed.cities = { { "Osaka", "Asia/Tokyo" } };

    // The data-loss case: a menu toggle over a malformed file used to write
    // the defaults, replacing the user's cities.
    ConfigLoadResult malformed;
    malformed.status = ConfigLoadStatus::ParseFailed;
    CHECK(!BaseForEdit(malformed, displayed).has_value());

    ConfigLoadResult locked;
    locked.status = ConfigLoadStatus::ReadFailed;
    CHECK(!BaseForEdit(locked, displayed).has_value());
}

TEST(config_edit_builds_on_the_file_or_on_what_is_displayed) {
    Config displayed = DefaultConfig();
    displayed.cities = { { "Osaka", "Asia/Tokyo" } };

    // A readable file wins over the displayed copy: it may hold a save the
    // watcher has not reloaded yet.
    ConfigLoadResult onDisk;
    onDisk.status = ConfigLoadStatus::Ok;
    onDisk.config = DefaultConfig();
    onDisk.config.cities = { { "Sapporo", "Asia/Tokyo" } };
    std::optional<Config> a = BaseForEdit(onDisk, displayed);
    CHECK(a.has_value());
    if (a) CHECK_EQ(a->cities[0].label, std::string("Sapporo"));

    // No file means nothing to lose: recreate it from what is on screen.
    ConfigLoadResult missing;
    missing.status = ConfigLoadStatus::Missing;
    std::optional<Config> b = BaseForEdit(missing, displayed);
    CHECK(b.has_value());
    if (b) CHECK_EQ(b->cities[0].label, std::string("Osaka"));
}

TEST(config_scale_follows_size_class) {
    Config c;
    c.size = SizeClass::Small;  CHECK_EQ(c.scale() == 0.8, true);
    c.size = SizeClass::Medium; CHECK_EQ(c.scale() == 1.0, true);
    c.size = SizeClass::Large;  CHECK_EQ(c.scale() == 1.3, true);
}
