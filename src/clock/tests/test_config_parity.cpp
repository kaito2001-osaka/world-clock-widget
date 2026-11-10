// The settings console tests consume the same inputs and expected values.
#include "test_framework.hpp"
#include "Config.hpp"
#include "json.hpp"
#include <fstream>
#include <iterator>
#include <cstdio>

TEST(config_shared_parsing_cases) {
    std::ifstream file(CONFIG_PARSING_FIXTURES, std::ios::binary);
    CHECK(file.is_open());
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto fixtures = json::parse(text);
    CHECK(fixtures.isArray());
    for (const auto& test : fixtures.arr) {
        std::printf("  config case: %s\n", test.getString("name", "").c_str());
        auto actual = ParseConfig(test.getString("input", ""));
        CHECK_EQ(actual.has_value(), test.getBool("accepted", false));
        if (!actual) continue;
        const auto* expected = test.find("expected");
        CHECK(expected != nullptr);
        if (!expected) continue;
        auto parsedExpected = ParseConfig(json::dump(*expected));
        CHECK(parsedExpected.has_value());
        if (parsedExpected) CHECK(*actual == *parsedExpected);
    }
}
