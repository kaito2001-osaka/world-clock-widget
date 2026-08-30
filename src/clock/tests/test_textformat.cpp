// Tests for the clock string and date pattern formatting (TextFormat.hpp).
#include "test_framework.hpp"
#include "TextFormat.hpp"

namespace {

// 2026-06-12 is a Friday; a fixed date keeps the expectations readable.
LocalTimeFields Fields(int y, unsigned mo, unsigned d, unsigned wd,
                       int h, int mi, int s) {
    LocalTimeFields f;
    f.valid = true;
    f.year = y; f.month = mo; f.day = d; f.weekday = wd;
    f.hour = h; f.minute = mi; f.second = s;
    return f;
}

LocalTimeFields Friday() { return Fields(2026, 6, 12, 5, 9, 5, 30); }

Config Cfg(int hourFormat, bool showSeconds) {
    Config c;
    c.hourFormat = hourFormat;
    c.showSeconds = showSeconds;
    return c;
}

} // namespace

// ---- FormatTime -----------------------------------------------------------

TEST(format_time_24h) {
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 9, 5, 30), Cfg(24, false)), std::wstring(L"9:05"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 9, 5, 30), Cfg(24, true)), std::wstring(L"9:05:30"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 0, 0, 0), Cfg(24, false)), std::wstring(L"0:00"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 23, 59, 59), Cfg(24, true)), std::wstring(L"23:59:59"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 10, 0, 0), Cfg(24, false)), std::wstring(L"10:00"));
}

TEST(format_time_12h) {
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 0, 30, 0), Cfg(12, false)), std::wstring(L"12:30 AM"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 12, 30, 0), Cfg(12, false)), std::wstring(L"12:30 PM"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 13, 0, 0), Cfg(12, false)), std::wstring(L"1:00 PM"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 11, 59, 0), Cfg(12, false)), std::wstring(L"11:59 AM"));
    CHECK_EQ(FormatTime(Fields(2026, 6, 12, 5, 23, 5, 7), Cfg(12, true)), std::wstring(L"11:05:07 PM"));
}

TEST(format_time_invalid_fields) {
    LocalTimeFields bad;   // valid == false
    CHECK_EQ(FormatTime(bad, Cfg(24, false)), std::wstring(L"--:--"));
    CHECK_EQ(FormatTime(bad, Cfg(12, true)), std::wstring(L"--:--"));
}

// ---- FormatDatePattern ----------------------------------------------------

TEST(date_pattern_year_tokens) {
    LocalTimeFields f = Friday();
    CHECK_EQ(FormatDatePattern(f, L"yyyy"), std::wstring(L"2026"));
    CHECK_EQ(FormatDatePattern(f, L"yy"), std::wstring(L"26"));
    CHECK_EQ(FormatDatePattern(f, L"yyyyy"), std::wstring(L"2026"));  // run >= 4
}

TEST(date_pattern_month_tokens) {
    LocalTimeFields f = Friday();
    CHECK_EQ(FormatDatePattern(f, L"MMMM"), std::wstring(L"June"));
    CHECK_EQ(FormatDatePattern(f, L"MMM"), std::wstring(L"Jun"));
    CHECK_EQ(FormatDatePattern(f, L"MM"), std::wstring(L"06"));
    CHECK_EQ(FormatDatePattern(f, L"M"), std::wstring(L"6"));
    CHECK_EQ(FormatDatePattern(Fields(2026, 12, 1, 2, 0, 0, 0), L"MMMM"), std::wstring(L"December"));
}

TEST(date_pattern_day_and_weekday_tokens) {
    LocalTimeFields f = Friday();
    CHECK_EQ(FormatDatePattern(f, L"dddd"), std::wstring(L"Friday"));
    CHECK_EQ(FormatDatePattern(f, L"ddd"), std::wstring(L"Fri"));
    CHECK_EQ(FormatDatePattern(f, L"dd"), std::wstring(L"12"));
    CHECK_EQ(FormatDatePattern(f, L"d"), std::wstring(L"12"));
    CHECK_EQ(FormatDatePattern(Fields(2026, 6, 7, 0, 0, 0, 0), L"d"), std::wstring(L"7"));
    CHECK_EQ(FormatDatePattern(Fields(2026, 6, 7, 0, 0, 0, 0), L"dd"), std::wstring(L"07"));
    CHECK_EQ(FormatDatePattern(Fields(2026, 6, 7, 0, 0, 0, 0), L"dddd"), std::wstring(L"Sunday"));
}

TEST(date_pattern_japanese_weekday_tokens) {
    LocalTimeFields f = Friday();
    CHECK_EQ(FormatDatePattern(f, L"aaa"), std::wstring(L"金"));
    CHECK_EQ(FormatDatePattern(f, L"a"), std::wstring(L"金"));
    CHECK_EQ(FormatDatePattern(f, L"aaaa"), std::wstring(L"金曜日"));
}

TEST(date_pattern_passes_literals_through) {
    LocalTimeFields f = Friday();
    CHECK_EQ(FormatDatePattern(f, L"ddd, MMM d"), std::wstring(L"Fri, Jun 12"));
    CHECK_EQ(FormatDatePattern(f, L"yyyy-MM-dd"), std::wstring(L"2026-06-12"));
    CHECK_EQ(FormatDatePattern(f, L"MM/dd/yyyy"), std::wstring(L"06/12/2026"));
    CHECK_EQ(FormatDatePattern(f, L"M/d"), std::wstring(L"6/12"));
    CHECK_EQ(FormatDatePattern(f, L"[none]"), std::wstring(L"[none]"));
}

TEST(date_pattern_japanese_presets) {
    LocalTimeFields f = Friday();
    CHECK_EQ(FormatDatePattern(f, L"yyyy年M月d日"), std::wstring(L"2026年6月12日"));
    CHECK_EQ(FormatDatePattern(f, L"M月d日(aaa)"), std::wstring(L"6月12日(金)"));
    CHECK_EQ(FormatDatePattern(f, L"yyyy年M月d日 aaaa"), std::wstring(L"2026年6月12日 金曜日"));
}

TEST(date_pattern_invalid_fields_and_empty) {
    LocalTimeFields bad;
    CHECK_EQ(FormatDatePattern(bad, L"yyyy-MM-dd"), std::wstring(L""));
    CHECK_EQ(FormatDatePattern(Friday(), L""), std::wstring(L""));
}

// ---- WidenDigits ----------------------------------------------------------

TEST(widen_digits_replaces_only_digits) {
    CHECK_EQ(WidenDigits(L"9:05:30", L'8'), std::wstring(L"8:88:88"));
    CHECK_EQ(WidenDigits(L"Fri, Jun 12", L'8'), std::wstring(L"Fri, Jun 88"));
    CHECK_EQ(WidenDigits(L"2026年6月12日", L'0'), std::wstring(L"0000年0月00日"));
    CHECK_EQ(WidenDigits(L"", L'8'), std::wstring(L""));
    CHECK_EQ(WidenDigits(L"no digits here", L'8'), std::wstring(L"no digits here"));
}

TEST(widen_digits_preserves_length) {
    const std::wstring src = L"12:34:56 AM";
    CHECK_EQ(WidenDigits(src, L'8').size(), src.size());
}
