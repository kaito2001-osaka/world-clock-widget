// Tests for UTC -> local time conversion (TimeEngine.hpp, WindowsZoneMap.hpp).
//
// Every case pins a fixed UTC instant so the expectations are stable forever.
#include "test_framework.hpp"
#include "TimeEngine.hpp"
#include "WindowsZoneMap.hpp"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace std::chrono;

namespace {

// Build a UTC instant from calendar fields.
system_clock::time_point Utc(int y, unsigned mo, unsigned d, int h, int mi, int s) {
    auto date = sys_days{ year{ y } / month{ mo } / day{ d } };
    return system_clock::time_point{ date.time_since_epoch() }
         + hours{ h } + minutes{ mi } + seconds{ s };
}

sys_seconds UtcS(int y, unsigned mo, unsigned d, int h, int mi, int s) {
    return floor<seconds>(Utc(y, mo, d, h, mi, s));
}

// Compact assertion: the local wall clock in `tz` at `utc`.
void ExpectLocal(const char* tz, system_clock::time_point utc,
                 int y, unsigned mo, unsigned d, int h, int mi) {
    LocalTimeFields f = ComputeLocal(tz, utc);
    CHECK(f.valid);
    if (!f.valid) return;
    CHECK_EQ(f.year, y);
    CHECK_EQ(f.month, mo);
    CHECK_EQ(f.day, d);
    CHECK_EQ(f.hour, h);
    CHECK_EQ(f.minute, mi);
}

} // namespace

TEST(compute_local_basic_offsets) {
    auto t = Utc(2026, 1, 15, 0, 0, 0);   // 2026-01-15T00:00:00Z
    ExpectLocal("Etc/UTC", t, 2026, 1, 15, 0, 0);
    ExpectLocal("Asia/Tokyo", t, 2026, 1, 15, 9, 0);          // +09:00
    ExpectLocal("Europe/London", t, 2026, 1, 15, 0, 0);       // GMT in January
    ExpectLocal("America/Los_Angeles", t, 2026, 1, 14, 16, 0); // -08:00, previous day
    ExpectLocal("America/New_York", t, 2026, 1, 14, 19, 0);    // -05:00, previous day
}

TEST(compute_local_half_and_quarter_hour_offsets) {
    auto t = Utc(2026, 1, 15, 0, 0, 0);
    ExpectLocal("Asia/Kolkata", t, 2026, 1, 15, 5, 30);   // +05:30
    ExpectLocal("Asia/Kathmandu", t, 2026, 1, 15, 5, 45); // +05:45
}

TEST(compute_local_northern_dst_transition) {
    // US spring forward 2026-03-08: 02:00 EST -> 03:00 EDT (07:00Z).
    ExpectLocal("America/New_York", Utc(2026, 3, 8, 6, 59, 0), 2026, 3, 8, 1, 59);
    ExpectLocal("America/New_York", Utc(2026, 3, 8, 7, 0, 0), 2026, 3, 8, 3, 0);

    // US fall back 2026-11-01: 02:00 EDT -> 01:00 EST (06:00Z).
    ExpectLocal("America/New_York", Utc(2026, 11, 1, 5, 59, 0), 2026, 11, 1, 1, 59);
    ExpectLocal("America/New_York", Utc(2026, 11, 1, 6, 0, 0), 2026, 11, 1, 1, 0);
}

TEST(compute_local_southern_hemisphere_dst) {
    // Sydney is +11:00 (DST) in January and +10:00 in July.
    ExpectLocal("Australia/Sydney", Utc(2026, 1, 15, 0, 0, 0), 2026, 1, 15, 11, 0);
    ExpectLocal("Australia/Sydney", Utc(2026, 7, 15, 0, 0, 0), 2026, 7, 15, 10, 0);
}

TEST(compute_local_london_summer_time) {
    // BST is +01:00 in July.
    ExpectLocal("Europe/London", Utc(2026, 7, 15, 12, 0, 0), 2026, 7, 15, 13, 0);
}

TEST(compute_local_zones_whose_rules_changed_after_2022) {
    // #49: ICU's tz 2022g had these an hour off. The values are the real ones
    // (and the taskbar clock's) at 2026-10-23 11:37Z.
    auto t = Utc(2026, 10, 23, 11, 37, 0);
    ExpectLocal("Africa/Cairo", t, 2026, 10, 23, 14, 37);        // +03:00, DST again since 2023
    ExpectLocal("America/Nuuk", t, 2026, 10, 23, 10, 37);        // -01:00, DST since 2024
    ExpectLocal("Asia/Almaty", t, 2026, 10, 23, 16, 37);         // +05:00 since 2024-03-01
    ExpectLocal("America/Coyhaique", t, 2026, 10, 23, 8, 37);    // -03:00, new in 2025
    ExpectLocal("Asia/Tokyo", t, 2026, 10, 23, 20, 37);
}

TEST(compute_local_seconds_are_carried_through) {
    LocalTimeFields f = ComputeLocal("Asia/Tokyo", Utc(2026, 6, 12, 0, 5, 37));
    CHECK(f.valid);
    CHECK_EQ(f.hour, 9);
    CHECK_EQ(f.minute, 5);
    CHECK_EQ(f.second, 37);
}

TEST(compute_local_weekday_encoding_is_sunday_zero) {
    // 2026-06-07 is a Sunday, 2026-06-12 a Friday.
    CHECK_EQ(ComputeLocal("Etc/UTC", Utc(2026, 6, 7, 12, 0, 0)).weekday, 0u);
    CHECK_EQ(ComputeLocal("Etc/UTC", Utc(2026, 6, 8, 12, 0, 0)).weekday, 1u);
    CHECK_EQ(ComputeLocal("Etc/UTC", Utc(2026, 6, 12, 12, 0, 0)).weekday, 5u);
    CHECK_EQ(ComputeLocal("Etc/UTC", Utc(2026, 6, 13, 12, 0, 0)).weekday, 6u);
}

TEST(compute_local_month_and_day_are_one_based) {
    LocalTimeFields f = ComputeLocal("Etc/UTC", Utc(2026, 1, 1, 0, 0, 0));
    CHECK_EQ(f.year, 2026);
    CHECK_EQ(f.month, 1u);
    CHECK_EQ(f.day, 1u);
}

TEST(compute_local_rejects_unknown_zone) {
    CHECK_EQ(ComputeLocal("Not/AZone", Utc(2026, 1, 15, 0, 0, 0)).valid, false);
    CHECK_EQ(ComputeLocal("", Utc(2026, 1, 15, 0, 0, 0)).valid, false);
    // Names are matched exactly, as config.json stores them.
    CHECK_EQ(ComputeLocal("asia/tokyo", Utc(2026, 1, 15, 0, 0, 0)).valid, false);
    // A Windows id is not an IANA id.
    CHECK_EQ(ComputeLocal("Tokyo Standard Time", Utc(2026, 1, 15, 0, 0, 0)).valid, false);
}

TEST(compute_local_is_repeatable) {
    // The zone lookup is cached; repeated calls must agree.
    auto t = Utc(2026, 6, 12, 3, 21, 9);
    LocalTimeFields a = ComputeLocal("Asia/Tokyo", t);
    LocalTimeFields b = ComputeLocal("Asia/Tokyo", t);
    CHECK_EQ(a.valid, b.valid);
    CHECK_EQ(a.hour, b.hour);
    CHECK_EQ(a.minute, b.minute);
    CHECK_EQ(a.second, b.second);
    CHECK_EQ(a.day, b.day);
}

TEST(compute_local_does_not_mix_up_zones) {
    // Interleaved lookups must not bleed one zone's offset into another.
    auto t = Utc(2026, 1, 15, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        ExpectLocal("Asia/Tokyo", t, 2026, 1, 15, 9, 0);
        ExpectLocal("America/New_York", t, 2026, 1, 14, 19, 0);
        ExpectLocal("Europe/London", t, 2026, 1, 15, 0, 0);
    }
}

// ---- OffsetInYear --------------------------------------------------------------
// The transition arithmetic on hand-built rules, the way Windows writes them in
// TIME_ZONE_INFORMATION, so these hold whatever the registry says.

namespace {

TzTransition Nth(int mo, int dow, int week, int h, int mi = 0, int s = 0, int ms = 0) {
    return { 0, mo, dow, week, h, mi, s, ms };
}

TzYearRule Rule(int bias, int daylightBias, TzTransition standardDate, TzTransition daylightDate) {
    return { bias, 0, daylightBias, standardDate, daylightDate };
}

// US Eastern: DST from the 2nd Sunday of March 02:00 to the 1st Sunday of
// November 02:00.
const TzYearRule kEastern = Rule(300, -60, Nth(11, 0, 1, 2), Nth(3, 0, 2, 2));

void ExpectSpan(const OffsetSpan& s, minutes offset, sys_seconds begin, sys_seconds end) {
    CHECK_EQ(s.offset.count(), seconds{ offset }.count());
    CHECK_EQ(s.begin.time_since_epoch().count(), begin.time_since_epoch().count());
    CHECK_EQ(s.end.time_since_epoch().count(), end.time_since_epoch().count());
}

} // namespace

TEST(offset_northern_dst_and_its_spans) {
    // The year runs from local (standard) midnight: 05:00Z.
    const sys_seconds jan1 = UtcS(2026, 1, 1, 5, 0, 0), next = UtcS(2027, 1, 1, 5, 0, 0);
    const sys_seconds spring = UtcS(2026, 3, 8, 7, 0, 0), fall = UtcS(2026, 11, 1, 6, 0, 0);
    ExpectSpan(OffsetInYear(kEastern, 2026, jan1), -5h, jan1, spring);
    ExpectSpan(OffsetInYear(kEastern, 2026, spring - 1s), -5h, jan1, spring);
    ExpectSpan(OffsetInYear(kEastern, 2026, spring), -4h, spring, fall);
    ExpectSpan(OffsetInYear(kEastern, 2026, fall - 1s), -4h, spring, fall);
    ExpectSpan(OffsetInYear(kEastern, 2026, fall), -5h, fall, next);
    ExpectSpan(OffsetInYear(kEastern, 2026, next - 1s), -5h, fall, next);
}

TEST(offset_southern_dst_spans_the_new_year) {
    // Sydney: +10, DST (+11) from the 1st Sunday of October 02:00 to the 1st
    // Sunday of April 03:00. In 2026: ends 04-04 16:00Z, starts 10-03 16:00Z.
    const TzYearRule sydney = Rule(-600, -60, Nth(4, 0, 1, 3), Nth(10, 0, 1, 2));
    const sys_seconds jan1 = UtcS(2025, 12, 31, 14, 0, 0), next = UtcS(2026, 12, 31, 14, 0, 0);
    const sys_seconds end = UtcS(2026, 4, 4, 16, 0, 0), start = UtcS(2026, 10, 3, 16, 0, 0);
    ExpectSpan(OffsetInYear(sydney, 2026, UtcS(2026, 1, 15, 0, 0, 0)), 11h, jan1, end);
    ExpectSpan(OffsetInYear(sydney, 2026, end - 1s), 11h, jan1, end);
    ExpectSpan(OffsetInYear(sydney, 2026, end), 10h, end, start);
    ExpectSpan(OffsetInYear(sydney, 2026, start), 11h, start, next);
}

TEST(offset_half_hour_daylight_bias) {
    // Lord Howe: +10:30, DST adds only 30 minutes. Starts 2026-10-04 02:00
    // local (10-03 15:30Z), ends 2026-04-05 02:00 local daylight (04-04 15:00Z).
    const TzYearRule lordHowe = Rule(-630, -30, Nth(4, 0, 1, 2), Nth(10, 0, 1, 2));
    const sys_seconds end = UtcS(2026, 4, 4, 15, 0, 0), start = UtcS(2026, 10, 3, 15, 30, 0);
    CHECK_EQ(OffsetInYear(lordHowe, 2026, end - 1s).offset.count(), (11h).count() * 3600);
    CHECK_EQ(OffsetInYear(lordHowe, 2026, end).offset.count(), (10h + 30min).count() * 60);
    CHECK_EQ(OffsetInYear(lordHowe, 2026, start - 1s).offset.count(), (10h + 30min).count() * 60);
    CHECK_EQ(OffsetInYear(lordHowe, 2026, start).offset.count(), (11h).count() * 3600);
}

TEST(offset_cairo_2026_ends_at_23_59_59_999) {
    // Egypt's 2026 rule as Windows stores it: DST from the last Friday of
    // April 00:00 (04-24, so 04-23 22:00Z) to the last Thursday of October
    // 23:59:59.999 (10-29, so 10-29 20:59:59.999Z, shown from 21:00:00Z).
    const TzYearRule cairo = Rule(-120, -60, Nth(10, 4, 5, 23, 59, 59, 999), Nth(4, 5, 5, 0));
    const sys_seconds start = UtcS(2026, 4, 23, 22, 0, 0), end = UtcS(2026, 10, 29, 21, 0, 0);
    ExpectSpan(OffsetInYear(cairo, 2026, UtcS(2026, 10, 23, 11, 37, 0)), 3h, start, end);
    ExpectSpan(OffsetInYear(cairo, 2026, end - 1s), 3h, start, end);
    ExpectSpan(OffsetInYear(cairo, 2026, end), 2h, end, UtcS(2026, 12, 31, 22, 0, 0));
    ExpectSpan(OffsetInYear(cairo, 2026, start - 1s), 2h, UtcS(2025, 12, 31, 22, 0, 0), start);
}

TEST(offset_last_week_in_a_month_with_four_or_five) {
    // London: last Sunday of March 01:00 GMT, last Sunday of October 02:00 BST.
    // March 2026 has five Sundays (the 29th), October four (the 25th).
    const TzYearRule london = Rule(0, -60, Nth(10, 0, 5, 2), Nth(3, 0, 5, 1));
    const sys_seconds start = UtcS(2026, 3, 29, 1, 0, 0), end = UtcS(2026, 10, 25, 1, 0, 0);
    ExpectSpan(OffsetInYear(london, 2026, UtcS(2026, 7, 15, 12, 0, 0)), 1h, start, end);
}

TEST(offset_without_dst_covers_the_whole_year) {
    const TzYearRule tokyo = Rule(-540, 0, {}, {});
    ExpectSpan(OffsetInYear(tokyo, 2026, UtcS(2026, 6, 1, 0, 0, 0)), 9h,
               UtcS(2025, 12, 31, 15, 0, 0), UtcS(2026, 12, 31, 15, 0, 0));
    // Standard bias counts too, though Windows leaves it at 0 in practice.
    TzYearRule shifted = tokyo;
    shifted.standardBias = -60;
    CHECK_EQ(OffsetInYear(shifted, 2026, UtcS(2026, 6, 1, 0, 0, 0)).offset.count(), (10h).count() * 3600);
}

TEST(offset_fixed_date_rules) {
    // wYear != 0: the month and day are a date, not "the n-th weekday".
    TzYearRule fixed = Rule(0, -60, { 2026, 9, 0, 20, 2, 0, 0, 0 }, { 2026, 3, 0, 15, 1, 0, 0, 0 });
    const sys_seconds start = UtcS(2026, 3, 15, 1, 0, 0), end = UtcS(2026, 9, 20, 1, 0, 0);
    ExpectSpan(OffsetInYear(fixed, 2026, UtcS(2026, 6, 1, 0, 0, 0)), 1h, start, end);
}

TEST(offset_ignores_malformed_rules) {
    // A month or week Windows never writes must not walk off the calendar.
    TzYearRule bad = kEastern;
    bad.daylightDate.month = 13;
    CHECK_EQ(OffsetInYear(bad, 2026, UtcS(2026, 7, 1, 0, 0, 0)).offset.count(), (-5h).count() * 3600);
    bad = kEastern;
    bad.standardDate.dayOfWeek = 9;
    CHECK_EQ(OffsetInYear(bad, 2026, UtcS(2026, 7, 1, 0, 0, 0)).offset.count(), (-5h).count() * 3600);
}

TEST(offset_switch_to_permanent_dst) {
    // British Columbia's 2026 rule as Windows stores it: DST "ends" on the
    // first Thursday of January 00:00 (2026-01-01) and starts on the second
    // Sunday of March, so from 03-08 10:00Z the zone stays at -07:00 for good.
    const TzYearRule bc = Rule(480, -60, Nth(1, 4, 1, 0), Nth(3, 0, 2, 2));
    const sys_seconds jan1 = UtcS(2026, 1, 1, 8, 0, 0), start = UtcS(2026, 3, 8, 10, 0, 0);
    ExpectSpan(OffsetInYear(bc, 2026, jan1), -8h, jan1, start);
    ExpectSpan(OffsetInYear(bc, 2026, start), -7h, start, UtcS(2027, 1, 1, 8, 0, 0));
}

TEST(rule_year_is_the_local_standard_year) {
    // West of UTC the new UTC year comes first: until local midnight the old
    // year's rules still hold.
    const TzYearRule pacific = Rule(480, -60, Nth(11, 0, 1, 2), Nth(3, 0, 2, 2));
    RuleYearSpan r = RuleYearAt(pacific, UtcS(2026, 1, 1, 3, 0, 0));
    CHECK_EQ(r.year, 2025);
    CHECK_EQ(r.begin.time_since_epoch().count(), UtcS(2026, 1, 1, 0, 0, 0).time_since_epoch().count());
    CHECK_EQ(r.end.time_since_epoch().count(), UtcS(2026, 1, 1, 8, 0, 0).time_since_epoch().count());
    r = RuleYearAt(pacific, UtcS(2026, 1, 1, 8, 0, 0));
    CHECK_EQ(r.year, 2026);
    CHECK_EQ(r.end.time_since_epoch().count(), UtcS(2027, 1, 1, 0, 0, 0).time_since_epoch().count());

    // East of UTC the local new year comes first, inside the old UTC year.
    const TzYearRule tokyo = Rule(-540, 0, {}, {});
    r = RuleYearAt(tokyo, UtcS(2026, 12, 31, 16, 0, 0));
    CHECK_EQ(r.year, 2027);
    CHECK_EQ(r.begin.time_since_epoch().count(), UtcS(2026, 12, 31, 15, 0, 0).time_since_epoch().count());
    CHECK_EQ(r.end.time_since_epoch().count(), UtcS(2027, 1, 1, 0, 0, 0).time_since_epoch().count());
    r = RuleYearAt(tokyo, UtcS(2026, 6, 1, 0, 0, 0));
    CHECK_EQ(r.year, 2026);

    // At UTC+0 the two years coincide.
    const TzYearRule utc = Rule(0, 0, {}, {});
    r = RuleYearAt(utc, UtcS(2026, 6, 1, 0, 0, 0));
    CHECK_EQ(r.year, 2026);
    CHECK_EQ(r.begin.time_since_epoch().count(), UtcS(2026, 1, 1, 0, 0, 0).time_since_epoch().count());
    CHECK_EQ(r.end.time_since_epoch().count(), UtcS(2027, 1, 1, 0, 0, 0).time_since_epoch().count());
}

// ---- WindowsZoneMap ------------------------------------------------------------

namespace {
std::wstring MapOf(const char* iana) {
    const wchar_t* id = WindowsZoneId(iana);
    return id ? id : L"(none)";
}
} // namespace

TEST(zone_map_resolves_names_and_aliases) {
    CHECK_EQ(MapOf("Africa/Cairo"), std::wstring(L"Egypt Standard Time"));
    CHECK_EQ(MapOf("Asia/Tokyo"), std::wstring(L"Tokyo Standard Time"));
    CHECK_EQ(MapOf("Etc/UTC"), std::wstring(L"UTC"));
    // CLDR keys some zones by their old names; both spellings resolve.
    CHECK_EQ(MapOf("Asia/Kolkata"), std::wstring(L"India Standard Time"));
    CHECK_EQ(MapOf("Asia/Calcutta"), std::wstring(L"India Standard Time"));
    CHECK_EQ(MapOf("America/Nuuk"), std::wstring(L"Greenland Standard Time"));
    CHECK_EQ(MapOf("America/Godthab"), std::wstring(L"Greenland Standard Time"));
}

TEST(zone_map_rejects_unknown_names) {
    CHECK(WindowsZoneId("Not/AZone") == nullptr);
    CHECK(WindowsZoneId("") == nullptr);
    CHECK(WindowsZoneId("asia/tokyo") == nullptr);
    CHECK(WindowsZoneId("Tokyo Standard Time") == nullptr);
    // Neighbours of real entries in the sorted table.
    CHECK(WindowsZoneId("Asia/Tokyo ") == nullptr);
    CHECK(WindowsZoneId("Asia/Toky") == nullptr);
}

TEST(zone_map_is_sorted_for_binary_search) {
    auto all = AllWindowsZones();
    CHECK(all.size() > 400);
    for (size_t i = 1; i < all.size(); ++i)
        CHECK(all[i - 1].iana < all[i].iana);
    for (const WindowsZoneEntry& e : all)
        CHECK(WindowsZoneId(e.iana) == e.windows);
}

// ---- against Windows' own conversion ---------------------------------------------
// SystemTimeToTzSpecificLocalTimeEx is what the taskbar clock shows, so it is
// the reference: the cached path, ComputeLocal and ZoneClock alike, must match
// it at every instant, across transitions and new years, for every zone.

namespace {

const DYNAMIC_TIME_ZONE_INFORMATION* WindowsZone(const wchar_t* id) {
    static std::map<std::wstring, DYNAMIC_TIME_ZONE_INFORMATION> zones = [] {
        std::map<std::wstring, DYNAMIC_TIME_ZONE_INFORMATION> m;
        DYNAMIC_TIME_ZONE_INFORMATION z{};
        for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &z) == ERROR_SUCCESS; ++i)
            m[z.TimeZoneKeyName] = z;
        return m;
    }();
    auto it = zones.find(id);
    return it == zones.end() ? nullptr : &it->second;
}

LocalTimeFields Reference(const char* tz, system_clock::time_point utc) {
    LocalTimeFields f;
    const wchar_t* id = WindowsZoneId(tz);
    const DYNAMIC_TIME_ZONE_INFORMATION* zone = id ? WindowsZone(id) : nullptr;
    if (!zone) return f;
    const sys_days date = floor<days>(utc);
    const year_month_day ymd{ date };
    const hh_mm_ss hms{ floor<seconds>(utc - date) };
    SYSTEMTIME u{};
    u.wYear   = WORD(int(ymd.year()));
    u.wMonth  = WORD(unsigned(ymd.month()));
    u.wDay    = WORD(unsigned(ymd.day()));
    u.wHour   = WORD(hms.hours().count());
    u.wMinute = WORD(hms.minutes().count());
    u.wSecond = WORD(hms.seconds().count());
    SYSTEMTIME l{};
    if (!SystemTimeToTzSpecificLocalTimeEx(zone, &u, &l)) return f;
    const sys_days localDate{ year{ l.wYear } / month{ l.wMonth } / day{ l.wDay } };
    f.valid   = true;
    f.year    = l.wYear;
    f.month   = l.wMonth;
    f.day     = l.wDay;
    f.weekday = weekday{ localDate }.c_encoding();
    f.hour    = l.wHour;
    f.minute  = l.wMinute;
    f.second  = l.wSecond;
    return f;
}

bool SameFields(const LocalTimeFields& a, const LocalTimeFields& b) {
    return a.valid == b.valid && a.year == b.year && a.month == b.month &&
           a.day == b.day && a.weekday == b.weekday && a.hour == b.hour &&
           a.minute == b.minute && a.second == b.second;
}

// Names the zone and instant on a mismatch; a bare CHECK in a sweep would not.
void CheckMatches(const char* tz, system_clock::time_point t, const LocalTimeFields& got) {
    ::t::CountCheck();
    const LocalTimeFields want = Reference(tz, t);
    if (SameFields(got, want)) return;
    auto show = [](const LocalTimeFields& f) {
        if (!f.valid) return std::string("invalid");
        char buf[64];
        snprintf(buf, sizeof buf, "%04d-%02u-%02u %02d:%02d:%02d", f.year, f.month, f.day,
                 f.hour, f.minute, f.second);
        return std::string(buf);
    };
    const sys_seconds s = floor<seconds>(t);
    ::t::Fail("matches SystemTimeToTzSpecificLocalTimeEx", __FILE__, __LINE__,
              std::string("  zone: ") + tz + " at unix " + std::to_string(s.time_since_epoch().count()) +
              "\n  actual:   " + show(got) + "\n  expected: " + show(want));
}

// Step through a window and compare ComputeLocal with Windows.
void SweepAgainstReference(const char* tz, system_clock::time_point from,
                           system_clock::time_point to, seconds step) {
    for (auto t = from; t < to; t += step)
        CheckMatches(tz, t, ComputeLocal(tz, t));
}

// The same with one ZoneClock, stepping through time the way the renderer
// does, so the cached offset is actually reused (and has to be dropped).
void SweepClockAgainstReference(const char* tz, system_clock::time_point from,
                                system_clock::time_point to, seconds step) {
    ZoneClock clock{ tz };
    for (auto t = from; t < to; t += step)
        CheckMatches(tz, t, clock.At(t));
}

} // namespace

TEST(compute_local_matches_windows_across_us_spring_forward) {
    // 2026-03-08 07:00Z: 02:00 EST becomes 03:00 EDT.
    SweepAgainstReference("America/New_York",
                          Utc(2026, 3, 8, 6, 58, 0), Utc(2026, 3, 8, 7, 2, 0), seconds{ 1 });
}

TEST(compute_local_matches_windows_across_us_fall_back) {
    // 2026-11-01 06:00Z: 02:00 EDT becomes 01:00 EST.
    SweepAgainstReference("America/New_York",
                          Utc(2026, 11, 1, 5, 58, 0), Utc(2026, 11, 1, 6, 2, 0), seconds{ 1 });
}

TEST(compute_local_matches_windows_across_southern_transition) {
    // Sydney leaves DST on 2026-04-05.
    SweepAgainstReference("Australia/Sydney",
                          Utc(2026, 4, 4, 15, 58, 0), Utc(2026, 4, 4, 16, 2, 0), seconds{ 1 });
}

TEST(compute_local_matches_windows_across_cairo_dst_end) {
    // 2026-10-29 23:59:59.999 EEST (20:59:59.999Z) becomes EET.
    SweepAgainstReference("Africa/Cairo",
                          Utc(2026, 10, 29, 20, 58, 0), Utc(2026, 10, 29, 21, 2, 0), seconds{ 1 });
}

TEST(compute_local_is_repeatable_and_survives_reversed_queries) {
    // Walking backwards leaves the cached window just as much as walking
    // forwards does; the offset must still be re-queried.
    const char* tz = "America/New_York";
    for (auto t = Utc(2026, 11, 1, 6, 2, 0); t > Utc(2026, 11, 1, 5, 58, 0); t -= seconds{ 1 })
        CheckMatches(tz, t, ComputeLocal(tz, t));
    // And repeating one instant many times stays stable.
    auto fixed = Utc(2026, 6, 12, 3, 21, 9);
    LocalTimeFields first = ComputeLocal(tz, fixed);
    for (int i = 0; i < 50; ++i)
        CHECK(SameFields(ComputeLocal(tz, fixed), first));
}

TEST(compute_local_still_rejects_unknown_zones_after_use) {
    // A failed lookup is cached too; it must keep failing rather than
    // returning a neighbouring zone's offset.
    auto t = Utc(2026, 6, 12, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(ComputeLocal("Not/AZone", t).valid, false);
        CHECK_EQ(ComputeLocal("Asia/Tokyo", t).valid, true);
        CHECK_EQ(ComputeLocal("", t).valid, false);
    }
}

// ---- ZoneClock ---------------------------------------------------------------

TEST(zone_clock_first_call_matches_reference) {
    // Nothing is cached yet, so the first answer has to come from a lookup.
    auto t = Utc(2026, 1, 15, 3, 29, 47);
    for (const char* z : { "Asia/Tokyo", "America/Los_Angeles", "Europe/London", "Etc/UTC" }) {
        ZoneClock clock{ z };
        CheckMatches(z, t, clock.At(t));
    }
}

TEST(zone_clock_across_us_spring_forward) {
    // 2026-03-08 07:00Z: 02:00 EST becomes 03:00 EDT.
    SweepClockAgainstReference("America/New_York",
                               Utc(2026, 3, 8, 6, 58, 0), Utc(2026, 3, 8, 7, 2, 0), seconds{ 1 });
    ZoneClock clock{ "America/New_York" };
    LocalTimeFields before = clock.At(Utc(2026, 3, 8, 6, 59, 59));
    LocalTimeFields after  = clock.At(Utc(2026, 3, 8, 7, 0, 0));
    CHECK_EQ(before.hour, 1);
    CHECK_EQ(before.second, 59);
    CHECK_EQ(after.hour, 3);
    CHECK_EQ(after.minute, 0);
}

TEST(zone_clock_across_us_fall_back) {
    // 2026-11-01 06:00Z: 02:00 EDT becomes 01:00 EST.
    SweepClockAgainstReference("America/New_York",
                               Utc(2026, 11, 1, 5, 58, 0), Utc(2026, 11, 1, 6, 2, 0), seconds{ 1 });
    ZoneClock clock{ "America/New_York" };
    CHECK_EQ(clock.At(Utc(2026, 11, 1, 5, 59, 59)).hour, 1);
    CHECK_EQ(clock.At(Utc(2026, 11, 1, 6, 0, 0)).hour, 1);   // the hour repeats
    CHECK_EQ(clock.At(Utc(2026, 11, 1, 6, 0, 0)).minute, 0);
}

TEST(zone_clock_across_london_spring_forward) {
    // 2026-03-29 01:00Z: 01:00 GMT becomes 02:00 BST.
    SweepClockAgainstReference("Europe/London",
                               Utc(2026, 3, 29, 0, 58, 0), Utc(2026, 3, 29, 1, 2, 0), seconds{ 1 });
}

TEST(zone_clock_across_half_hour_dst) {
    // Lord Howe moves by 30 minutes; 2026-10-04 02:00 +10:30 becomes 02:30 +11:00
    // (2026-10-03 15:30Z).
    SweepClockAgainstReference("Australia/Lord_Howe",
                               Utc(2026, 10, 3, 15, 28, 0), Utc(2026, 10, 3, 15, 32, 0), seconds{ 1 });
}

TEST(zone_clock_across_the_new_year) {
    // A new year can bring new rules, so the cached period ends there; zones
    // on either side of UTC reach local midnight at different instants.
    // Vancouver also changes rules at its new year 2026 (to permanent DST).
    for (const char* z : { "Pacific/Kiritimati", "Pacific/Auckland", "Asia/Tokyo",
                           "America/Santiago", "Pacific/Pago_Pago", "America/Vancouver" })
        SweepClockAgainstReference(z, Utc(2025, 12, 31, 8, 0, 0), Utc(2026, 1, 1, 14, 0, 0),
                                   seconds{ 61 });
}

TEST(zone_clock_when_time_runs_backwards) {
    // A clock set back across a transition leaves the cached window from
    // below; the offset must be looked up again, not reused.
    const char* tz = "America/New_York";
    ZoneClock clock{ tz };
    for (auto t = Utc(2026, 3, 8, 7, 2, 0); t > Utc(2026, 3, 8, 6, 58, 0); t -= seconds{ 1 })
        CheckMatches(tz, t, clock.At(t));
    for (auto t = Utc(2026, 11, 1, 6, 2, 0); t > Utc(2026, 11, 1, 5, 58, 0); t -= seconds{ 1 })
        CheckMatches(tz, t, clock.At(t));
}

TEST(zone_clock_after_large_jumps) {
    // Resume after months asleep, or a clock set years off, in both directions.
    const char* tz = "Europe/London";
    const system_clock::time_point stops[] = {
        Utc(2026, 7, 15, 12, 0, 0), Utc(2030, 1, 15, 12, 0, 0), Utc(2026, 7, 15, 12, 0, 1),
        Utc(2001, 12, 31, 23, 59, 59), Utc(2026, 1, 15, 0, 0, 0), Utc(2026, 7, 15, 12, 0, 2),
    };
    ZoneClock clock{ tz };
    for (auto t : stops)
        CheckMatches(tz, t, clock.At(t));
}

TEST(zone_clock_rejects_unknown_zones) {
    auto t = Utc(2026, 6, 12, 0, 0, 0);
    ZoneClock bad{ "Not/AZone" };
    ZoneClock empty{ "" };
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(bad.At(t).valid, false);
        CHECK_EQ(empty.At(t).valid, false);
    }
}

TEST(zone_clock_matches_windows_for_every_zone) {
    // One IANA name per Windows zone, over three years -- wide enough to cross
    // every transition, two new years and the dynamic-DST years in between.
    std::set<std::wstring> seen;
    for (const WindowsZoneEntry& e : AllWindowsZones()) {
        if (!seen.insert(e.windows).second) continue;
        const std::string tz{ e.iana };
        SweepClockAgainstReference(tz.c_str(), Utc(2025, 1, 1, 0, 0, 0), Utc(2028, 1, 1, 0, 0, 0),
                                   seconds{ 3607 });   // prime-ish, so it lands all over the clock
    }
}
