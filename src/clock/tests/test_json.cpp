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

// Structural UTF-8 validation: correct lead/continuation bytes, minimal
// encoding, no surrogates, nothing past U+10FFFF.
bool IsValidUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        int extra; unsigned cp;
        if (c < 0x80)             { extra = 0; cp = c; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
        else return false;                       // stray continuation / 5-byte lead
        if (i + extra >= n) return false;        // truncated sequence
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (extra == 1 && cp < 0x80) return false;      // overlong
        if (extra == 2 && cp < 0x800) return false;     // overlong
        if (extra == 3 && cp < 0x10000) return false;   // overlong
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false; // encoded surrogate
        i += extra + 1;
    }
    return true;
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

// ---- surrogate pairs ------------------------------------------------------
// System.Text.Json escapes every non-ASCII character, so a non-BMP character
// only ever reaches the parser as a \uD8xx\uDCxx pair.

TEST(json_joins_surrogate_pairs_into_one_code_point) {
    // U+1F5FC TOKYO TOWER -> F0 9F 97 BC
    json::Value v = json::parse("{\"s\": \"\\uD83D\\uDDFC\"}");
    CHECK_EQ(Bytes(v.getString("s", "")), std::string("F0 9F 97 BC"));

    // U+1F600 GRINNING FACE -> F0 9F 98 80
    json::Value w = json::parse("{\"s\": \"\\uD83D\\uDE00\"}");
    CHECK_EQ(Bytes(w.getString("s", "")), std::string("F0 9F 98 80"));
}

TEST(json_surrogate_pair_in_context) {
    // The realistic case: a city label with an emoji in it.
    json::Value v = json::parse("{\"label\": \"Tokyo \\uD83D\\uDDFC\"}");
    CHECK_EQ(Bytes(v.getString("label", "")),
             std::string("54 6F 6B 79 6F 20 F0 9F 97 BC"));
}

TEST(json_lone_surrogates_become_replacement_char) {
    // U+FFFD -> EF BF BD. A lone surrogate has no valid UTF-8 encoding.
    CHECK_EQ(Bytes(json::parse("{\"s\": \"\\uD83D\"}").getString("s", "")),
             std::string("EF BF BD"));                       // high, nothing after
    CHECK_EQ(Bytes(json::parse("{\"s\": \"\\uDE00\"}").getString("s", "")),
             std::string("EF BF BD"));                       // low on its own
    CHECK_EQ(Bytes(json::parse("{\"s\": \"\\uDDFC\\uD83D\"}").getString("s", "")),
             std::string("EF BF BD EF BF BD"));              // reversed order
}

TEST(json_high_surrogate_followed_by_a_normal_escape) {
    // The high surrogate is unpaired, but the escape after it is still valid
    // and must be decoded on its own rather than swallowed.
    json::Value v = json::parse("{\"s\": \"\\uD83D\\u0041\"}");
    CHECK_EQ(Bytes(v.getString("s", "")), std::string("EF BF BD 41"));

    json::Value w = json::parse("{\"s\": \"\\uD83D\\n\"}");
    CHECK_EQ(Bytes(w.getString("s", "")), std::string("EF BF BD 0A"));
}

TEST(json_high_surrogate_followed_by_a_literal_character) {
    json::Value v = json::parse("{\"s\": \"\\uD83DZ\"}");
    CHECK_EQ(Bytes(v.getString("s", "")), std::string("EF BF BD 5A"));
}

TEST(json_encodes_utf8_length_boundaries) {
    // One case per length class, on both sides of each boundary.
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\u0000\"}").getString("s", "")), std::string("00"));
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\u007F\"}").getString("s", "")), std::string("7F"));
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\u0080\"}").getString("s", "")), std::string("C2 80"));
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\u07FF\"}").getString("s", "")), std::string("DF BF"));
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\u0800\"}").getString("s", "")), std::string("E0 A0 80"));
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\uFFFF\"}").getString("s", "")), std::string("EF BF BF"));
    // U+10000, the first non-BMP code point.
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\uD800\\uDC00\"}").getString("s", "")),
             std::string("F0 90 80 80"));
    // U+10FFFF, the last code point there is.
    CHECK_EQ(Bytes(json::parse("{\"s\":\"\\uDBFF\\uDFFF\"}").getString("s", "")),
             std::string("F4 8F BF BF"));
}

TEST(json_decoded_output_is_always_valid_utf8) {
    // Whatever the input, the bytes handed to MultiByteToWideChar must be
    // well-formed -- that is what stops labels rendering as replacement boxes.
    const char* inputs[] = {
        "{\"s\":\"\\uD83D\\uDDFC\"}", "{\"s\":\"\\uD83D\"}", "{\"s\":\"\\uDE00\"}",
        "{\"s\":\"\\uD83D\\u0041\"}", "{\"s\":\"\\u6771\\u4EAC\"}",
        "{\"s\":\"\\uDBFF\\uDFFF\"}", "{\"s\":\"plain ascii\"}",
    };
    for (const char* in : inputs)
        CHECK(IsValidUtf8(json::parse(in).getString("s", "")));
}

TEST(json_surrogate_pair_survives_a_dump_round_trip) {
    json::Value a = json::parse("{\"label\":\"Tokyo \\uD83D\\uDDFC\"}");
    json::Value b = json::parse(json::dump(a));
    CHECK_EQ(b.getString("label", ""), a.getString("label", ""));
    CHECK(IsValidUtf8(b.getString("label", "")));
}

TEST(json_validator_rejects_the_old_broken_encoding) {
    // Guards the guard: the CESU-8 the parser used to emit for an emoji
    // (two lone surrogates, three bytes each) must fail IsValidUtf8, otherwise
    // the test above would pass even with the bug reintroduced.
    CHECK_EQ(IsValidUtf8(std::string("\xED\xA0\xBD\xED\xB7\xBC")), false);
    CHECK_EQ(IsValidUtf8(std::string("\xC0\x80")), false);       // overlong NUL
    CHECK_EQ(IsValidUtf8(std::string("\xF0\x9F\x97")), false);   // truncated
    CHECK_EQ(IsValidUtf8(std::string("\x80")), false);           // stray continuation
    CHECK_EQ(IsValidUtf8(std::string("\xF0\x9F\x97\xBC")), true);// the real thing
}

