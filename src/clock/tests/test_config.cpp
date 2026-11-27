// Tests for config.json parsing, defaults and clamping (Config.hpp).
#include "test_framework.hpp"
#include "Config.hpp"
#include "json.hpp"

#include <windows.h>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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
    std::string Read() const {
        std::string out;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return out;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
        CloseHandle(h);
        return out;
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
    // Too large for an int at all: treated as missing rather than cast.
    CHECK_EQ(Parsed(R"({"opacity": 1e10})").opacity, 85);
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
    // Anything after the root object means the file is corrupt.
    CHECK(!ParseConfig(R"({"opacity": 40}xyz)").has_value());
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

// ---- encodings and the size cap, as the settings app reads them (#87) -----

namespace {
std::string Bin(std::initializer_list<int> bytes) {
    std::string s;
    for (int b : bytes) s += (char)b;
    return s;
}

// `ascii` in UTF-16 (each byte widened), after the given BOM.
std::string Utf16(const std::string& bom, const std::string& ascii, bool bigEndian) {
    std::string s = bom;
    for (char c : ascii) {
        if (bigEndian) { s += '\0'; s += c; }
        else           { s += c; s += '\0'; }
    }
    return s;
}

// A valid document of exactly `bytes` bytes, padded with whitespace.
std::string DocumentOfSize(size_t bytes) {
    const std::string head = R"({"cities":[{"label":"Osaka","tz":"Asia/Tokyo"}]})";
    return head + std::string(bytes - head.size(), ' ');
}
} // namespace

TEST(config_decode_leaves_plain_utf8_alone_and_drops_its_bom) {
    CHECK_EQ(DecodeConfigBytes("{\"a\":1}"), std::string("{\"a\":1}"));
    CHECK_EQ(DecodeConfigBytes("\xEF\xBB\xBF{}"), std::string("{}"));
    CHECK_EQ(DecodeConfigBytes(""), std::string());
    // Only a BOM says UTF-16; without one the bytes are not guessed at.
    const std::string noBom = Utf16("", "{}", false);
    CHECK_EQ(DecodeConfigBytes(noBom), noBom);
}

TEST(config_decode_reads_utf16_in_both_byte_orders) {
    CHECK_EQ(DecodeConfigBytes(Utf16(Bin({ 0xFF, 0xFE }), "{\"a\":1}", false)), std::string("{\"a\":1}"));
    CHECK_EQ(DecodeConfigBytes(Utf16(Bin({ 0xFE, 0xFF }), "{\"a\":1}", true)), std::string("{\"a\":1}"));
    // U+3042 and U+1F5FC (a surrogate pair) come out as UTF-8.
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFF, 0xFE, 0x42, 0x30, 0x3D, 0xD8, 0xFC, 0xDD })),
             std::string("\xE3\x81\x82\xF0\x9F\x97\xBC"));
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFE, 0xFF, 0x30, 0x42, 0xD8, 0x3D, 0xDD, 0xFC })),
             std::string("\xE3\x81\x82\xF0\x9F\x97\xBC"));
}

TEST(config_decode_replaces_what_utf16_cannot_decode) {
    const std::string fffd = "\xEF\xBF\xBD";
    // A high surrogate before a non-surrogate, and one at the end.
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFF, 0xFE, 0x3D, 0xD8, 0x41, 0x00, 0x3D, 0xD8 })),
             fffd + "A" + fffd);
    // A low surrogate on its own.
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFF, 0xFE, 0xFC, 0xDD, 0x41, 0x00 })), fffd + "A");
    // An odd byte left over.
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFF, 0xFE, 0x41, 0x00, 0x42 })), "A" + fffd);
}

TEST(config_decode_reads_utf32_in_both_byte_orders) {
    // FF FE 00 00 is UTF-32LE, not UTF-16LE followed by U+0000.
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFF, 0xFE, 0, 0, '{', 0, 0, 0, '}', 0, 0, 0 })), std::string("{}"));
    CHECK_EQ(DecodeConfigBytes(Bin({ 0, 0, 0xFE, 0xFF, 0, 0, 0, '{', 0, 0, 0, '}' })), std::string("{}"));
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFF, 0xFE, 0, 0, 0xFC, 0xF5, 0x01, 0 })),
             std::string("\xF0\x9F\x97\xBC"));
    // Past U+10FFFF, a surrogate code point, and a partial unit.
    CHECK_EQ(DecodeConfigBytes(Bin({ 0xFF, 0xFE, 0, 0, 0, 0, 0x11, 0, 0x00, 0xD8, 0, 0, 'A' })),
             std::string("\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"));
}

TEST(config_load_file_reads_a_utf16_file_with_a_bom) {
    // What Windows PowerShell 5.1's `>` and Out-File write by default.
    TempFile f("utf16");
    f.Write(Utf16(Bin({ 0xFF, 0xFE }),
                  R"({"cities":[{"label":"Osaka","tz":"Asia/Tokyo"}],"opacity":40})", false));
    ConfigLoadResult r = LoadConfigFile(f.path);
    CHECK(r.status == ConfigLoadStatus::Ok);
    CHECK_EQ(r.config.cities.size(), (size_t)1);
    CHECK_EQ(r.config.cities[0].label, std::string("Osaka"));
    CHECK_EQ(r.config.opacity, 40);
}

TEST(config_load_file_reports_a_file_over_1_mib_as_too_large) {
    TempFile f("size_cap");
    f.Write(DocumentOfSize((size_t)kMaxConfigFileBytes));
    ConfigLoadResult atCap = LoadConfigFile(f.path);
    CHECK(atCap.status == ConfigLoadStatus::Ok);
    CHECK_EQ(atCap.config.cities[0].label, std::string("Osaka"));

    f.Write(DocumentOfSize((size_t)kMaxConfigFileBytes + 1));
    ConfigLoadResult over = LoadConfigFile(f.path);
    CHECK(over.status == ConfigLoadStatus::TooLarge);
    CHECK(over.config == DefaultConfig());
}

// ---- writing (Win32 file I/O, #22) ----------------------------------------

namespace {
bool Exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
} // namespace

TEST(config_write_file_round_trips_every_field) {
    TempFile f("write_round_trip");
    Config c = DefaultConfig();
    c.cities = { { "Osaka \"Kansai\"", "Asia/Tokyo" }, { "\xE6\x9D\xB1\xE4\xBA\xAC", "Asia/Tokyo" } };
    c.displayMode = DisplayMode::Analog;
    c.layout = LayoutDir::Horizontal;
    c.hourFormat = 12;
    c.showDate = false;
    c.dateFormat = "yyyy-MM-dd";
    c.showSeconds = true;
    c.size = SizeClass::Large;
    c.opacity = 42;
    c.alwaysOnTop = true;
    c.lockPosition = true;
    c.launchAtStartup = true;
    c.theme = "light";

    CHECK(WriteConfigFile(f.path, c));
    ConfigLoadResult r = LoadConfigFile(f.path);
    CHECK(r.status == ConfigLoadStatus::Ok);
    CHECK(r.config == c);
    CHECK(!Exists(f.path + L".gadget.tmp"));
}

TEST(config_write_file_replaces_a_longer_file_entirely) {
    TempFile f("write_replace");
    // Longer than what replaces it: a leftover tail would be a syntax error.
    f.Write(std::string(4096, ' ') + R"({"opacity": 40})");
    CHECK(WriteConfigFile(f.path, DefaultConfig()));
    ConfigLoadResult r = LoadConfigFile(f.path);
    CHECK(r.status == ConfigLoadStatus::Ok);
    CHECK(r.config == DefaultConfig());
    CHECK(!Exists(f.path + L".gadget.tmp"));
}

TEST(config_write_file_fails_cleanly_without_its_folder) {
    TempFile f("write_no_folder");
    const std::wstring path = f.path + L".missing\\config.json";
    CHECK(!WriteConfigFile(path, DefaultConfig()));
    CHECK(!Exists(path));
    CHECK(!Exists(path + L".gadget.tmp"));
}

TEST(config_write_file_leaves_the_old_file_when_it_cannot_replace_it) {
    TempFile f("write_locked");
    f.Write(R"({"opacity": 40})");
    // Held without FILE_SHARE_DELETE, so the replace keeps failing.
    HANDLE lock = CreateFileW(f.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    const bool written = WriteConfigFile(f.path, DefaultConfig());
    CloseHandle(lock);

    CHECK(!written);
    CHECK_EQ(LoadConfigFile(f.path).config.opacity, 40);
    CHECK(!Exists(f.path + L".gadget.tmp"));
}

// ---- one-key edits from the menu (#86) -------------------------------------

namespace {

// The issue's reproduction: everything ParseConfig drops, cuts or defaults.
// 34 valid cities (past kMaxCities), an 80-character label, a city with
// "timezone" instead of "tz", an unknown display mode, a 70-character date
// format and a key the gadget does not know.
std::string LossyInput() {
    std::string s = "{\n  \"cities\": [\n";
    for (int k = 0; k < 34; ++k)
        s += "    {\"label\": \"City " + std::to_string(k) + "\", \"tz\": \"Asia/Tokyo\"},\n";
    s += "    {\"label\": \"" + std::string(80, 'L') + "\", \"tz\": \"Europe/London\"},\n";
    s += "    {\"label\": \"Osaka\", \"timezone\": \"Asia/Tokyo\"}\n  ],\n";
    s += "  \"displayMode\": \"Analog\",\n";
    s += "  \"dateFormat\": \"" + std::string(70, 'd') + "\",\n";
    s += "  \"lockPosition\": false,\n";
    s += "  \"futureOption\": {\"nested\": [1, 2.5, null]}\n}\n";
    return s;
}

std::vector<std::string> Keys(const json::Value& v) {
    std::vector<std::string> keys;
    for (const auto& kv : v.obj) keys.push_back(kv.first);
    return keys;
}

// What the edit must produce: the input document with one key set.
std::string WithKey(const std::string& text, const char* key, bool value) {
    json::Value root = json::parse(text);
    root.set(key, value);
    return json::dump(root);
}

void CheckNothingLost(const std::string& text) {
    json::Value root = json::parse(text);
    const json::Value* cities = root.find("cities");
    CHECK(cities != nullptr);
    if (!cities) return;
    CHECK_EQ(cities->arr.size(), (size_t)36);
    if (cities->arr.size() != 36) return;
    CHECK_EQ(cities->arr[34].getString("label", "").size(), (size_t)80);
    CHECK_EQ(cities->arr[35].getString("label", ""), std::string("Osaka"));
    CHECK_EQ(cities->arr[35].getString("timezone", ""), std::string("Asia/Tokyo"));
    CHECK_EQ(root.getString("displayMode", ""), std::string("Analog"));
    CHECK_EQ(root.getString("dateFormat", "").size(), (size_t)70);
    CHECK(root.find("futureOption") != nullptr);
}

} // namespace

TEST(config_edit_text_changes_only_the_toggled_key) {
    const std::string in = LossyInput();
    std::optional<std::string> out = EditConfigText(in, "lockPosition", true);
    CHECK(out.has_value());
    if (!out) return;
    CHECK_EQ(*out, WithKey(in, "lockPosition", true));
    CheckNothingLost(*out);

    json::Value root = json::parse(*out);
    CHECK(root.getBool("lockPosition", false));
    // Replaced in place: the keys keep the file's order.
    CHECK(Keys(root) == Keys(json::parse(in)));
}

TEST(config_edit_text_appends_a_missing_key_and_replaces_a_mistyped_one) {
    std::optional<std::string> added = EditConfigText(R"({"opacity": 40})", "alwaysOnTop", true);
    CHECK(added.has_value());
    if (added) {
        json::Value root = json::parse(*added);
        CHECK(Keys(root) == (std::vector<std::string>{ "opacity", "alwaysOnTop" }));
        CHECK_EQ(root.getInt("opacity", 0), 40);
        CHECK(root.getBool("alwaysOnTop", false));
    }

    std::optional<std::string> fixed = EditConfigText(R"({"lockPosition": "yes"})", "lockPosition", true);
    CHECK(fixed.has_value());
    if (fixed) {
        const json::Value* v = json::parse(*fixed).find("lockPosition");
        CHECK(v != nullptr && v->type == json::Type::Bool && v->b);
    }
}

TEST(config_edit_text_refuses_what_parse_config_refuses) {
    for (const char* text : { "", "{", "{\"opacity\": 40,}", "[]", "\"x\"", "42" }) {
        CHECK(!EditConfigText(text, "lockPosition", true).has_value());
        CHECK(!ParseConfig(text).has_value());
    }
}

TEST(config_edit_file_keeps_everything_the_gadget_skipped) {
    TempFile f("edit_lossy");
    const std::string in = LossyInput();
    f.Write(in);
    Config displayed = Parsed(in.c_str());

    CHECK(EditConfigFileBool(f.path, &Config::lockPosition, true, displayed) ==
          ConfigEditResult::Written);
    const std::string after = f.Read();
    CHECK_EQ(after, WithKey(in, "lockPosition", true));
    CheckNothingLost(after);
    CHECK(LoadConfigFile(f.path).config.lockPosition);
    CHECK(!Exists(f.path + L".gadget.tmp"));

    // Toggling back leaves the document as it was, key for key.
    CHECK(EditConfigFileBool(f.path, &Config::lockPosition, false, displayed) ==
          ConfigEditResult::Written);
    CHECK_EQ(f.Read(), WithKey(in, "lockPosition", false));
}

TEST(config_edit_file_writes_what_is_displayed_when_there_is_no_file) {
    TempFile f("edit_missing");
    Config displayed = DefaultConfig();
    displayed.cities = { { "Osaka", "Asia/Tokyo" } };

    CHECK(EditConfigFileBool(f.path, &Config::alwaysOnTop, true, displayed) ==
          ConfigEditResult::Written);
    ConfigLoadResult r = LoadConfigFile(f.path);
    CHECK(r.status == ConfigLoadStatus::Ok);
    Config expected = displayed;
    expected.alwaysOnTop = true;
    CHECK(r.config == expected);
}

TEST(config_edit_file_leaves_a_malformed_file_alone) {
    // The #23 data-loss case: a toggle over a malformed file used to write the
    // defaults, replacing the user's cities.
    TempFile f("edit_malformed");
    const std::string in = R"({"cities": [{"label": "Osaka", "tz": "Asia/Tokyo"}],)";
    f.Write(in);
    CHECK(EditConfigFileBool(f.path, &Config::lockPosition, true, DefaultConfig()) ==
          ConfigEditResult::ParseFailed);
    CHECK_EQ(f.Read(), in);
    CHECK(!Exists(f.path + L".gadget.tmp"));
}

TEST(config_edit_file_leaves_an_unreadable_file_alone) {
    TempFile f("edit_locked");
    const std::string in = R"({"opacity": 40})";
    f.Write(in);
    // No sharing at all, so the gadget's read is refused.
    HANDLE lock = CreateFileW(f.path.c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    const ConfigEditResult r = EditConfigFileBool(f.path, &Config::lockPosition, true, DefaultConfig());
    CloseHandle(lock);

    CHECK(r == ConfigEditResult::ReadFailed);
    CHECK_EQ(f.Read(), in);
}

TEST(config_edit_file_leaves_a_file_over_1_mib_alone) {
    TempFile f("edit_too_large");
    const std::string in = DocumentOfSize((size_t)kMaxConfigFileBytes + 1);
    f.Write(in);
    CHECK(EditConfigFileBool(f.path, &Config::lockPosition, true, DefaultConfig()) ==
          ConfigEditResult::TooLarge);
    CHECK(f.Read() == in);
    CHECK(!Exists(f.path + L".gadget.tmp"));
}

TEST(config_edit_file_rewrites_a_utf16_file_as_utf8_keeping_its_keys) {
    TempFile f("edit_utf16");
    const std::string doc = R"({"cities":[{"label":"Osaka","tz":"Asia/Tokyo"}],"custom":"kept"})";
    f.Write(Utf16(Bin({ 0xFF, 0xFE }), doc, false));
    CHECK(EditConfigFileBool(f.path, &Config::lockPosition, true, DefaultConfig()) ==
          ConfigEditResult::Written);
    const std::string after = f.Read();
    CHECK_EQ(after, WithKey(doc, "lockPosition", true));
    CHECK(LoadConfigFile(f.path).config.lockPosition);
}

// ---- reload no-op (#27) ---------------------------------------------------

TEST(config_equality_sees_every_kind_of_field) {
    CHECK(DefaultConfig() == DefaultConfig());

    // Each of these is a save the reload must not skip as "unchanged".
    Config c = DefaultConfig(); c.cities[0].label = "Osaka";            CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.cities[0].tz = "Asia/Seoul";                 CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.cities.pop_back();                           CHECK(c != DefaultConfig());
    c = DefaultConfig(); std::swap(c.cities[0], c.cities[1]);           CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.displayMode = DisplayMode::Analog;           CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.layout = LayoutDir::Horizontal;              CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.hourFormat = 12;                             CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.showDate = false;                            CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.dateFormat = "yyyy-MM-dd";                   CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.showSeconds = true;                          CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.size = SizeClass::Large;                     CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.opacity = 84;                                CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.alwaysOnTop = true;                          CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.lockPosition = true;                         CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.launchAtStartup = true;                      CHECK(c != DefaultConfig());
    c = DefaultConfig(); c.theme = "light";                             CHECK(c != DefaultConfig());
}

TEST(config_reformatted_file_parses_equal) {
    // The same settings written with other whitespace and key order -- e.g.
    // the second notification of one save -- must compare equal, or the
    // duplicate reload is not skipped.
    Config a = Parsed(R"({"cities":[{"label":"Tokyo","tz":"Asia/Tokyo"}],"opacity":70,"showSeconds":true})");
    Config b = Parsed("{\n  \"showSeconds\": true,\n  \"opacity\": 70,\n"
                      "  \"cities\": [ { \"tz\": \"Asia/Tokyo\", \"label\": \"Tokyo\" } ]\n}\n");
    CHECK(a == b);
}

TEST(config_reload_applies_only_what_changed) {
    Config shown = DefaultConfig();
    Config same  = DefaultConfig();
    Config other = DefaultConfig(); other.opacity = 50;

    CHECK_EQ(NeedsApply(true, shown, same), false);   // duplicate / overflow reload
    CHECK_EQ(NeedsApply(true, shown, other), true);
}

TEST(config_reload_applies_the_first_good_load_after_a_failed_start) {
    // Started on the defaults because the file was unreadable; the file is
    // then fixed to settings that happen to equal them. Still apply once: the
    // Run key was never reconciled against the user's file.
    CHECK_EQ(NeedsApply(false, DefaultConfig(), DefaultConfig()), true);
}

TEST(config_scale_follows_size_class) {
    Config c;
    c.size = SizeClass::Small;  CHECK_EQ(c.scale() == 0.8, true);
    c.size = SizeClass::Medium; CHECK_EQ(c.scale() == 1.0, true);
    c.size = SizeClass::Large;  CHECK_EQ(c.scale() == 1.3, true);
}

namespace {

// A document with one city labelled `label` and the given date format.
std::string LabelAndFormatDoc(const std::string& label, const std::string& dateFormat) {
    return R"({"cities":[{"label":")" + label + R"(","tz":"Asia/Tokyo"}],"dateFormat":")" +
           dateFormat + R"("})";
}

// A document with `n` valid cities, labelled "C0", "C1", ...; `leading` adds
// a city without a time zone in front of them.
std::string CitiesDoc(size_t n, bool leading = false) {
    std::string doc = R"({"cities":[)";
    if (leading) doc += R"({"label":"none","tz":""},)";
    for (size_t i = 0; i < n; ++i) {
        if (i) doc += ",";
        doc += R"({"label":"C)" + std::to_string(i) + R"(","tz":"Asia/Tokyo"})";
    }
    return doc + "]}";
}

} // namespace

TEST(config_truncates_a_long_label_and_date_format) {
    Config c = Parsed(LabelAndFormatDoc(std::string(65, 'a'), std::string(65, 'd')).c_str());
    CHECK_EQ(c.cities[0].label, std::string(kMaxLabelChars, 'a'));
    CHECK_EQ(c.dateFormat, std::string(kMaxDateFormatChars, 'd'));
}

TEST(config_keeps_a_label_and_date_format_at_the_limit) {
    const std::string label(kMaxLabelChars, 'a');
    const std::string format(kMaxDateFormatChars, 'd');
    Config c = Parsed(LabelAndFormatDoc(label, format).c_str());
    CHECK_EQ(c.cities[0].label, label);
    CHECK_EQ(c.dateFormat, format);
}

TEST(config_truncation_counts_code_points_and_keeps_sequences_whole) {
    const std::string kanji = "\xE6\x97\xA5";            // U+65E5, 3 bytes
    const std::string globe = "\xF0\x9F\x8C\x8D";        // U+1F30D, 4 bytes
    std::string label, format;
    for (size_t i = 0; i < kMaxLabelChars + 1; ++i) label += kanji;
    for (size_t i = 0; i < kMaxDateFormatChars + 1; ++i) format += globe;
    Config c = Parsed(LabelAndFormatDoc(label, format).c_str());
    CHECK_EQ(c.cities[0].label.size(), kMaxLabelChars * kanji.size());
    CHECK_EQ(c.dateFormat.size(), kMaxDateFormatChars * globe.size());
    CHECK_EQ(c.cities[0].label, label.substr(0, kMaxLabelChars * kanji.size()));

    // A mix: 63 ASCII then a 4-byte code point is exactly 64 and stays.
    std::string mixed = std::string(kMaxLabelChars - 1, 'a') + globe;
    std::string cut = mixed;
    TruncateUtf8(cut, kMaxLabelChars);
    CHECK_EQ(cut, mixed);
    // One more ASCII in front pushes the whole 4-byte sequence out.
    cut = "a" + mixed;
    TruncateUtf8(cut, kMaxLabelChars);
    CHECK_EQ(cut, std::string(kMaxLabelChars, 'a'));
    // Zero leaves nothing; an empty string stays empty.
    cut = mixed;
    TruncateUtf8(cut, 0);
    CHECK_EQ(cut, std::string());
    TruncateUtf8(cut, kMaxLabelChars);
    CHECK_EQ(cut, std::string());
}

TEST(config_keeps_at_most_the_first_32_cities) {
    Config c = Parsed(CitiesDoc(kMaxCities + 1).c_str());
    CHECK_EQ(c.cities.size(), kMaxCities);
    CHECK_EQ(c.cities.front().label, std::string("C0"));
    CHECK_EQ(c.cities.back().label, "C" + std::to_string(kMaxCities - 1));

    CHECK_EQ(Parsed(CitiesDoc(kMaxCities).c_str()).cities.size(), kMaxCities);
}

TEST(config_city_limit_counts_only_cities_with_a_timezone) {
    Config c = Parsed(CitiesDoc(kMaxCities, true).c_str());
    CHECK_EQ(c.cities.size(), kMaxCities);
    CHECK_EQ(c.cities.front().label, std::string("C0"));
    CHECK_EQ(c.cities.back().label, "C" + std::to_string(kMaxCities - 1));
}

TEST(config_truncated_values_round_trip_unchanged) {
    Config c = Parsed(LabelAndFormatDoc(std::string(100, 'a'), std::string(100, 'd')).c_str());
    TempFile f("truncated_round_trip");
    CHECK(WriteConfigFile(f.path, c));
    ConfigLoadResult r = LoadConfigFile(f.path);
    CHECK(r.status == ConfigLoadStatus::Ok);
    CHECK(r.config == c);
}

// --- Where the settings folder comes from (#56) ---

TEST(config_appdata_prefers_the_known_folder) {
    CHECK(ChooseAppDataDir(L"C:\\Users\\a\\AppData\\Roaming", L"D:\\Other")
          == L"C:\\Users\\a\\AppData\\Roaming");
}

TEST(config_appdata_falls_back_to_the_environment_variable) {
    CHECK(ChooseAppDataDir(L"", L"D:\\Profiles\\a\\Roaming") == L"D:\\Profiles\\a\\Roaming");
}

TEST(config_appdata_is_empty_when_neither_is_set) {
    CHECK(ChooseAppDataDir(L"", L"").empty());
}

TEST(config_appdata_rejects_relative_and_drive_relative_paths) {
    // Each of these would land the folder somewhere that depends on the
    // current directory or drive -- the drive root in the worst case.
    CHECK(ChooseAppDataDir(L"", L"AppData").empty());
    CHECK(ChooseAppDataDir(L"", L"\\AppData").empty());
    CHECK(ChooseAppDataDir(L"", L"C:AppData").empty());
    CHECK(ChooseAppDataDir(L"", L"\\\\").empty());
    CHECK(ChooseAppDataDir(L"", L"\\\\\\x").empty());
    CHECK(ChooseAppDataDir(L"", L"1:\\x").empty());
    // A bad known folder is skipped in favour of a good variable.
    CHECK(ChooseAppDataDir(L"Roaming", L"E:\\Roaming") == L"E:\\Roaming");
}

TEST(config_appdata_accepts_unc_and_forward_slashes) {
    CHECK(ChooseAppDataDir(L"", L"\\\\server\\share\\a") == L"\\\\server\\share\\a");
    CHECK(ChooseAppDataDir(L"", L"c:/Users/a/AppData/Roaming") == L"c:/Users/a/AppData/Roaming");
}

TEST(config_appdata_drops_trailing_separators) {
    CHECK(ChooseAppDataDir(L"", L"D:\\Roaming\\") == L"D:\\Roaming");
    CHECK(ChooseAppDataDir(L"", L"D:\\Roaming\\\\/") == L"D:\\Roaming");
    // A bare root keeps no separator either, so "\WorldClockGadget" is appended once.
    CHECK(ChooseAppDataDir(L"", L"D:\\") == L"D:");
}

// --- Reading state.json (#57) ---

TEST(state_reads_a_saved_position) {
    WindowState s = ParseState(R"({"windowX": -1700, "windowY": 240, "monitor": "\\\\.\\DISPLAY2"})");
    CHECK(s.hasPosition);
    CHECK_EQ(s.windowX, -1700);
    CHECK_EQ(s.windowY, 240);
    CHECK(s.monitor == "\\\\.\\DISPLAY2");
}

TEST(state_needs_both_coordinates) {
    CHECK(!ParseState(R"({"windowX": 300})").hasPosition);
    CHECK(!ParseState(R"({"windowY": 300})").hasPosition);
    CHECK(!ParseState(R"({"windowX": "300", "windowY": 300})").hasPosition);
}

TEST(state_accepts_coordinates_at_the_bound) {
    WindowState s = ParseState(R"({"windowX": 1000000, "windowY": -1000000})");
    CHECK(s.hasPosition);
    CHECK_EQ(s.windowX, kMaxSavedCoord);
    CHECK_EQ(s.windowY, -kMaxSavedCoord);
}

TEST(state_rejects_coordinates_past_the_bound) {
    // 2147483600 is the reported case: within int, but x + width overflows.
    const char* cases[] = {
        R"({"windowX": 1000001, "windowY": 0})",
        R"({"windowX": 0, "windowY": -1000001})",
        R"({"windowX": 2147483600, "windowY": 80})",
        R"({"windowX": -2147483648, "windowY": 80})",
        R"({"windowX": 1e300, "windowY": 80})",
    };
    for (const char* text : cases) {
        WindowState s = ParseState(text);
        CHECK(!s.hasPosition);
        CHECK_EQ(s.windowX, WindowState{}.windowX);
        CHECK_EQ(s.windowY, WindowState{}.windowY);
    }
}

TEST(state_malformed_text_gives_the_defaults) {
    for (const char* text : { "", "{ not json", "[1, 2]", "42" }) {
        WindowState s = ParseState(text);
        CHECK(!s.hasPosition);
        CHECK(s.monitor.empty());
    }
}
