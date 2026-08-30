// Tests for UTC -> IANA-zone local time conversion (TimeEngine.hpp).
//
// Every case pins a fixed UTC instant so the expectations are stable forever.
#include "test_framework.hpp"
#include "TimeEngine.hpp"

#include <chrono>

using namespace std::chrono;

namespace {

// Build a UTC instant from calendar fields.
system_clock::time_point Utc(int y, unsigned mo, unsigned d, int h, int mi, int s) {
    auto date = sys_days{ year{ y } / month{ mo } / day{ d } };
    return system_clock::time_point{ date.time_since_epoch() }
         + hours{ h } + minutes{ mi } + seconds{ s };
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

TEST(tz_database_is_available) {
    // Everything else here is meaningless without it.
    CHECK(TzDatabaseAvailable());
}

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
