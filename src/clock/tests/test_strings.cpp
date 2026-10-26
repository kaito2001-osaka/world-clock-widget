// Tests for picking the UI language and the string tables (Strings.hpp).
#include "test_framework.hpp"
#include "Strings.hpp"

namespace {

// Every field of UiStrings is a const wchar_t*, so walk it as an array.
bool AllFilled(const UiStrings& s) {
    auto p = reinterpret_cast<const wchar_t* const*>(&s);
    for (size_t i = 0; i < sizeof(UiStrings) / sizeof(const wchar_t*); ++i)
        if (!p[i] || !p[i][0]) return false;
    return true;
}

} // namespace

TEST(strings_japanese_display_language_picks_japanese) {
    CHECK(IsJapaneseUiLang(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)));
    CHECK(IsJapaneseUiLang(MAKELANGID(LANG_JAPANESE, SUBLANG_NEUTRAL)));
}

TEST(strings_other_display_languages_pick_english) {
    CHECK(!IsJapaneseUiLang(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)));
    CHECK(!IsJapaneseUiLang(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)));
    CHECK(!IsJapaneseUiLang(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)));
    CHECK(!IsJapaneseUiLang(MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)));
}

TEST(strings_every_entry_is_filled_in_both_languages) {
    CHECK(AllFilled(kUiJa));
    CHECK(AllFilled(kUiEn));
}
