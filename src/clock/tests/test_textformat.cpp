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

// ---- worst-case sizing candidates -----------------------------------------
// The invariant behind the panel-jitter fix: whatever the clock actually
// shows, some candidate string is at least as long. Length is what the old
// code got wrong (WidenDigits equalised digit *glyphs* but not digit *counts*),
// so that is what these assert; the renderer measures the candidates for the
// remaining glyph-width differences.

namespace {

size_t LongestOf(const std::vector<std::wstring>& v) {
    size_t n = 0;
    for (const auto& s : v) n = (std::max)(n, s.size());
    return n;
}

} // namespace

TEST(measurement_time_covers_every_hour_and_minute) {
    for (int hf : { 12, 24 }) {
        for (bool secs : { false, true }) {
            Config cfg = Cfg(hf, secs);
            const size_t worst = LongestOf(MeasurementTimeCandidates(cfg));
            for (int h = 0; h < 24; ++h) {
                for (int m : { 0, 9, 59 }) {
                    for (int s : { 0, 9, 59 }) {
                        const std::wstring live =
                            FormatTime(Fields(2026, 6, 12, 5, h, m, s), cfg);
                        CHECK(live.size() <= worst);
                    }
                }
            }
        }
    }
}

TEST(measurement_time_is_two_digit_hour) {
    // The actual defect: "9:59" measured narrower than the "10:00" that
    // followed it, so the panel widened at the rollover.
    Config c24 = Cfg(24, false);
    CHECK_EQ(MeasurementTimeCandidates(c24).size(), (size_t)1);
    CHECK_EQ(MeasurementTimeCandidates(c24)[0], std::wstring(L"23:59"));
    CHECK(FormatTime(Fields(2026, 6, 12, 5, 9, 59, 0), c24).size()
          <= MeasurementTimeCandidates(c24)[0].size());

    Config c24s = Cfg(24, true);
    CHECK_EQ(MeasurementTimeCandidates(c24s)[0], std::wstring(L"23:59:59"));
}

TEST(measurement_time_covers_both_meridiems) {
    // AM and PM are different glyphs, so both must be offered for measurement.
    Config c12 = Cfg(12, false);
    auto cands = MeasurementTimeCandidates(c12);
    CHECK_EQ(cands.size(), (size_t)2);
    CHECK_EQ(cands[0], std::wstring(L"12:59 AM"));
    CHECK_EQ(cands[1], std::wstring(L"12:59 PM"));
}

TEST(measurement_date_covers_every_day_of_a_leap_year) {
    // Sweep all 366 days of a leap year against all 7 weekdays for each
    // preset: no rendering may ever exceed the longest candidate.
    const wchar_t* patterns[] = {
        L"ddd, MMM d", L"dddd, MMMM d", L"yyyy-MM-dd", L"MM/dd/yyyy",
        L"dd/MM/yyyy", L"M/d", L"d MMM yyyy",
        L"yyyy年M月d日", L"M月d日(aaa)", L"yyyy年M月d日 aaaa",
    };
    static const unsigned kDaysIn[13] =
        { 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };   // 2024, a leap year

    for (const wchar_t* p : patterns) {
        const std::wstring pat(p);
        const size_t worst = LongestOf(MeasurementDateCandidates(pat));
        CHECK(worst > 0);
        for (unsigned mo = 1; mo <= 12; ++mo) {
            for (unsigned d = 1; d <= kDaysIn[mo]; ++d) {
                for (unsigned wd = 0; wd < 7; ++wd) {
                    const std::wstring live =
                        FormatDatePattern(Fields(2024, mo, d, wd, 0, 0, 0), pat);
                    CHECK(live.size() <= worst);
                }
            }
        }
    }
}

TEST(measurement_date_varies_only_the_tokens_that_matter) {
    // A pattern with no month or weekday token needs exactly one candidate;
    // adding each token family widens the sweep. Keeping the list small is
    // what makes measuring all of them affordable.
    CHECK_EQ(MeasurementDateCandidates(L"yyyy").size(), (size_t)1);
    CHECK_EQ(MeasurementDateCandidates(L"MMMM").size(), (size_t)12);
    CHECK_EQ(MeasurementDateCandidates(L"dddd").size(), (size_t)7);
    CHECK_EQ(MeasurementDateCandidates(L"aaaa").size(), (size_t)7);
    CHECK_EQ(MeasurementDateCandidates(L"dddd, MMMM d").size(), (size_t)84);
}

TEST(measurement_date_includes_the_longest_names) {
    auto has = [](const std::vector<std::wstring>& v, const std::wstring& needle) {
        for (const auto& s : v) if (s.find(needle) != std::wstring::npos) return true;
        return false;
    };
    CHECK(has(MeasurementDateCandidates(L"MMMM"), L"September"));
    CHECK(has(MeasurementDateCandidates(L"dddd"), L"Wednesday"));
    CHECK(has(MeasurementDateCandidates(L"aaaa"), L"曜日"));
}

TEST(measurement_date_uses_two_digit_day_and_four_digit_year) {
    // The day rollover -- "Jun 9" to "Jun 10" -- was the other half of the bug.
    auto cands = MeasurementDateCandidates(L"yyyy-MM-dd");
    CHECK_EQ(cands.size(), (size_t)12);
    CHECK_EQ(cands[0], std::wstring(L"2000-01-28"));
    auto numeric = MeasurementDateCandidates(L"M/d");
    CHECK(LongestOf(numeric) >= std::wstring(L"12/28").size());
}

TEST(measurement_date_preserves_pattern_literals) {
    // The candidates must keep the separators; eating them would under-measure.
    for (const auto& s : MeasurementDateCandidates(L"ddd, MMM d"))
        CHECK(s.find(L", ") != std::wstring::npos);
    for (const auto& s : MeasurementDateCandidates(L"yyyy-MM-dd"))
        CHECK(s.find(L"-") != std::wstring::npos);
    for (const auto& s : MeasurementDateCandidates(L"yyyy年M月d日"))
        CHECK(s.find(L"年") != std::wstring::npos && s.find(L"日") != std::wstring::npos);
}

TEST(measurement_date_handles_an_empty_pattern) {
    auto cands = MeasurementDateCandidates(L"");
    CHECK_EQ(cands.size(), (size_t)1);
    CHECK_EQ(cands[0], std::wstring(L""));
}
