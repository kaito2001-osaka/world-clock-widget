// Tests for the hand-rolled JSON parser / serializer (json.hpp).
#include "test_framework.hpp"
#include "json.hpp"

#include <string>

namespace {

// Hex dump of the UTF-8 bytes, so encoding expectations read unambiguously.
std::string Bytes(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (!out.empty()) out += ' ';
        out += kHex[c >> 4];
        out += kHex[c & 0xF];
    }
    return out;
}

} // namespace

TEST(json_parses_scalars) {
    json::Value v = json::parse("{\"n\": 42, \"f\": 1.5, \"t\": true, \"g\": false,"
                                " \"s\": \"hi\", \"z\": null}");
    CHECK(v.isObject());
    CHECK_EQ(v.getInt("n", -1), 42);
    CHECK_EQ(v.getNumber("f", 0.0) == 1.5, true);
    CHECK_EQ(v.getBool("t", false), true);
    CHECK_EQ(v.getBool("g", true), false);
    CHECK_EQ(v.getString("s", ""), std::string("hi"));
    CHECK(v.find("z") != nullptr);
}

TEST(json_parses_negative_and_exponent) {
    json::Value v = json::parse("{\"a\": -7, \"b\": 2e3, \"c\": -1.25e-2}");
    CHECK_EQ(v.getInt("a", 0), -7);
    CHECK_EQ(v.getNumber("b", 0.0) == 2000.0, true);
    CHECK_EQ(v.getNumber("c", 0.0) == -0.0125, true);
}

TEST(json_parses_nested_structures) {
    json::Value v = json::parse(
        "{\"cities\":[{\"label\":\"Tokyo\",\"tz\":\"Asia/Tokyo\"},"
        "{\"label\":\"London\",\"tz\":\"Europe/London\"}]}");
    const json::Value* cities = v.find("cities");
    CHECK(cities != nullptr);
    CHECK(cities->isArray());
    CHECK_EQ(cities->arr.size(), (size_t)2);
    CHECK_EQ(cities->arr[0].getString("label", ""), std::string("Tokyo"));
    CHECK_EQ(cities->arr[1].getString("tz", ""), std::string("Europe/London"));
}

TEST(json_handles_empty_containers) {
    json::Value a = json::parse("{\"x\":[]}");
    CHECK(a.find("x")->isArray());
    CHECK_EQ(a.find("x")->arr.size(), (size_t)0);
    json::Value o = json::parse("{\"x\":{}}");
    CHECK(o.find("x")->isObject());
    CHECK_EQ(o.find("x")->obj.size(), (size_t)0);
}

TEST(json_getters_fall_back_on_missing_or_wrong_type) {
    json::Value v = json::parse("{\"s\": \"text\", \"n\": 5, \"b\": true}");
    // Missing keys.
    CHECK_EQ(v.getInt("nope", 99), 99);
    CHECK_EQ(v.getString("nope", "dflt"), std::string("dflt"));
    CHECK_EQ(v.getBool("nope", true), true);
    // Present but of the wrong type -- must still yield the default.
    CHECK_EQ(v.getInt("s", 99), 99);
    CHECK_EQ(v.getString("n", "dflt"), std::string("dflt"));
    CHECK_EQ(v.getBool("s", false), false);
    CHECK_EQ(v.getString("b", "dflt"), std::string("dflt"));
}

TEST(json_find_on_non_object_returns_null) {
    json::Value arr = json::parse("[1,2,3]");
    CHECK(arr.find("anything") == nullptr);
}

TEST(json_preserves_key_insertion_order) {
    // Order matters: config.json is human-editable and should not get shuffled.
    json::Value v = json::parse("{\"zeta\":1,\"alpha\":2,\"mid\":3}");
    CHECK_EQ(v.obj.size(), (size_t)3);
    CHECK_EQ(v.obj[0].first, std::string("zeta"));
    CHECK_EQ(v.obj[1].first, std::string("alpha"));
    CHECK_EQ(v.obj[2].first, std::string("mid"));
}

TEST(json_set_overwrites_in_place_keeping_order) {
    json::Value v;
    v.set("a", 1);
    v.set("b", 2);
    v.set("a", 3);
    CHECK_EQ(v.obj.size(), (size_t)2);
    CHECK_EQ(v.obj[0].first, std::string("a"));
    CHECK_EQ(v.getInt("a", 0), 3);
    CHECK_EQ(v.getInt("b", 0), 2);
}

TEST(json_decodes_string_escapes) {
    json::Value v = json::parse("{\"s\": \"a\\\"b\\\\c\\/d\\ne\\tf\\r\\b\\f\"}");
    CHECK_EQ(v.getString("s", ""), std::string("a\"b\\c/d\ne\tf\r\b\f"));
}

TEST(json_decodes_bmp_unicode_escapes) {
    // System.Text.Json escapes all non-ASCII, so Japanese labels arrive as \uXXXX.
    json::Value v = json::parse("{\"s\": \"\\u6771\\u4EAC\"}");
    CHECK_EQ(Bytes(v.getString("s", "")), std::string("E6 9D B1 E4 BA AC"));
}

TEST(json_round_trips_through_dump) {
    const std::string src =
        "{\"cities\":[{\"label\":\"Tokyo\",\"tz\":\"Asia/Tokyo\"}],"
        "\"opacity\":85,\"showDate\":true,\"dateFormat\":\"ddd, MMM d\"}";
    json::Value a = json::parse(src);
    std::string dumped = json::dump(a);
    json::Value b = json::parse(dumped);
    // Dumping twice must be stable, and the values must survive.
    CHECK_EQ(json::dump(b), dumped);
    CHECK_EQ(b.getInt("opacity", 0), 85);
    CHECK_EQ(b.getBool("showDate", false), true);
    CHECK_EQ(b.getString("dateFormat", ""), std::string("ddd, MMM d"));
    CHECK_EQ(b.find("cities")->arr[0].getString("label", ""), std::string("Tokyo"));
}

TEST(json_dump_prints_integral_numbers_without_decimals) {
    json::Value v;
    v.set("a", 85);
    v.set("b", 1.5);
    const std::string s = json::dump(v);
    CHECK(s.find("\"a\": 85") != std::string::npos);
    CHECK(s.find("\"a\": 85.0") == std::string::npos);
    CHECK(s.find("\"b\": 1.5") != std::string::npos);
}

TEST(json_dump_escapes_specials) {
    json::Value v;
    v.set("s", std::string("a\"b\\c\nd\te"));
    const std::string s = json::dump(v);
    CHECK(s.find("a\\\"b\\\\c\\nd\\te") != std::string::npos);
}

TEST(json_rejects_malformed_input) {
    CHECK_THROWS(json::parse("{\"s\": \"unterminated"));
    CHECK_THROWS(json::parse("{\"s\": \"bad \\q escape\"}"));
    CHECK_THROWS(json::parse("{\"s\": \"\\uZZZZ\"}"));
    CHECK_THROWS(json::parse("{\"a\" 1}"));       // missing colon
    CHECK_THROWS(json::parse("{\"a\": 1 \"b\": 2}"));  // missing comma
    CHECK_THROWS(json::parse("[1, 2"));           // unterminated array
    CHECK_THROWS(json::parse("tru"));             // truncated literal
}
