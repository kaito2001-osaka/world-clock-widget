// Tests for config.json parsing, defaults and clamping (Config.hpp).
#include "test_framework.hpp"
#include "Config.hpp"

#include <string>

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
    Config c = ConfigFromJson(R"({
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
    CHECK_EQ(ConfigFromJson(R"({"opacity": 0})").opacity, 10);
    CHECK_EQ(ConfigFromJson(R"({"opacity": -50})").opacity, 10);
    CHECK_EQ(ConfigFromJson(R"({"opacity": 101})").opacity, 100);
    CHECK_EQ(ConfigFromJson(R"({"opacity": 100000})").opacity, 100);
    // In-range values pass through, including the boundaries.
    CHECK_EQ(ConfigFromJson(R"({"opacity": 10})").opacity, 10);
    CHECK_EQ(ConfigFromJson(R"({"opacity": 100})").opacity, 100);
    CHECK_EQ(ConfigFromJson(R"({"opacity": 55})").opacity, 55);
}

TEST(config_empty_city_list_falls_back_to_defaults) {
    Config c = ConfigFromJson(R"({"cities": []})");
    CHECK_EQ(c.cities.size(), (size_t)4);
}

TEST(config_drops_cities_without_a_timezone) {
    Config c = ConfigFromJson(
        R"({"cities":[{"label":"Good","tz":"Asia/Tokyo"},{"label":"NoTz"}]})");
    CHECK_EQ(c.cities.size(), (size_t)1);
    CHECK_EQ(c.cities[0].label, std::string("Good"));
}

TEST(config_all_cities_invalid_falls_back_to_defaults) {
    Config c = ConfigFromJson(R"({"cities":[{"label":"NoTz"}]})");
    CHECK_EQ(c.cities.size(), (size_t)4);
}

TEST(config_unknown_enum_values_use_the_default_arm) {
    CHECK_EQ(ConfigFromJson(R"({"displayMode":"wristwatch"})").displayMode == DisplayMode::Digital, true);
    CHECK_EQ(ConfigFromJson(R"({"layout":"diagonal"})").layout == LayoutDir::Vertical, true);
    CHECK_EQ(ConfigFromJson(R"({"size":"enormous"})").size == SizeClass::Medium, true);
}

TEST(config_hour_format_accepts_only_12_or_24) {
    CHECK_EQ(ConfigFromJson(R"({"hourFormat": 12})").hourFormat, 12);
    CHECK_EQ(ConfigFromJson(R"({"hourFormat": 24})").hourFormat, 24);
    CHECK_EQ(ConfigFromJson(R"({"hourFormat": 13})").hourFormat, 24);
    CHECK_EQ(ConfigFromJson(R"({"hourFormat": 0})").hourFormat, 24);
}

TEST(config_empty_date_format_falls_back) {
    CHECK_EQ(ConfigFromJson(R"({"dateFormat": ""})").dateFormat, std::string("ddd, MMM d"));
    CHECK_EQ(ConfigFromJson("{}").dateFormat, std::string("ddd, MMM d"));
}

TEST(config_malformed_json_yields_defaults_not_a_crash) {
    // A corrupt file must never take the gadget down; it falls back silently.
    Config a = ConfigFromJson("{ this is not json");
    CHECK_EQ(a.cities.size(), (size_t)4);
    CHECK_EQ(a.opacity, 85);

    Config b = ConfigFromJson("");
    CHECK_EQ(b.cities.size(), (size_t)4);

    // Valid JSON, but not an object.
    Config c = ConfigFromJson("[1, 2, 3]");
    CHECK_EQ(c.cities.size(), (size_t)4);
}

TEST(config_scale_follows_size_class) {
    Config c;
    c.size = SizeClass::Small;  CHECK_EQ(c.scale() == 0.8, true);
    c.size = SizeClass::Medium; CHECK_EQ(c.scale() == 1.0, true);
    c.size = SizeClass::Large;  CHECK_EQ(c.scale() == 1.3, true);
}
