// Tests for redraw tick scheduling (TimerCadence.hpp).
#include "test_framework.hpp"
#include "TimerCadence.hpp"

#include <chrono>

using namespace std::chrono;

namespace {

// An instant `ms` milliseconds past a known whole minute.
// 2026-06-12T00:00:00Z is a whole minute, so offsets from it are unambiguous.
system_clock::time_point AtOffset(long long ms) {
    auto base = sys_days{ year{ 2026 } / month{ 6 } / day{ 12 } };
    return system_clock::time_point{ base.time_since_epoch() } + milliseconds{ ms };
}

} // namespace

// ---- second boundary ------------------------------------------------------

TEST(cadence_seconds_on_the_boundary_waits_a_full_second) {
    // Never 0: SetTimer with a zero delay fires immediately and spins.
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(0), true), 1000u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(1000), true), 1000u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(37000), true), 1000u);
}

TEST(cadence_seconds_just_past_a_boundary) {
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(1), true), 999u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(500), true), 500u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(999), true), 1u);
}

TEST(cadence_seconds_ignores_which_second_it_is) {
    // Only the sub-second remainder matters.
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(45250), true), 750u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(59250), true), 750u);
}

// ---- minute boundary ------------------------------------------------------

TEST(cadence_minutes_on_the_boundary_waits_a_full_minute) {
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(0), false), 60000u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(60000), false), 60000u);
}

TEST(cadence_minutes_counts_down_across_the_whole_minute) {
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(1), false), 59999u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(30000), false), 30000u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(59500), false), 500u);
    CHECK_EQ(MillisecondsToNextBoundary(AtOffset(59999), false), 1u);
}

// ---- properties -----------------------------------------------------------

TEST(cadence_never_returns_zero) {
    // A zero delay would make the timer fire immediately, over and over.
    for (long long ms = 0; ms < 60000; ms += 137) {
        CHECK(MillisecondsToNextBoundary(AtOffset(ms), true) > 0);
        CHECK(MillisecondsToNextBoundary(AtOffset(ms), false) > 0);
    }
}

TEST(cadence_stays_within_one_period) {
    // Sweeping a whole minute: second mode never exceeds 1000, minute mode
    // never exceeds 60000, so a tick is never missed or over-delayed.
    for (long long ms = 0; ms < 60000; ms += 97) {
        CHECK(MillisecondsToNextBoundary(AtOffset(ms), true) <= 1000u);
        CHECK(MillisecondsToNextBoundary(AtOffset(ms), false) <= 60000u);
    }
}

TEST(cadence_lands_exactly_on_a_boundary) {
    // The point of the whole exercise: now + result must be a boundary.
    for (long long ms = 0; ms < 60000; ms += 89) {
        const long long sec = MillisecondsToNextBoundary(AtOffset(ms), true);
        CHECK_EQ((ms + sec) % 1000, 0LL);
        const long long min = MillisecondsToNextBoundary(AtOffset(ms), false);
        CHECK_EQ((ms + min) % 60000, 0LL);
    }
}

TEST(cadence_minute_wait_is_never_shorter_than_the_second_wait) {
    for (long long ms = 0; ms < 60000; ms += 331)
        CHECK(MillisecondsToNextBoundary(AtOffset(ms), false) >=
              MillisecondsToNextBoundary(AtOffset(ms), true));
}

TEST(cadence_handles_pre_epoch_instants) {
    // system_clock can hold instants before 1970; a naive % would go negative
    // and produce a delay larger than the period.
    auto preEpoch = sys_days{ year{ 1969 } / month{ 7 } / day{ 20 } };
    for (long long ms = 0; ms < 3000; ms += 251) {
        auto t = system_clock::time_point{ preEpoch.time_since_epoch() } + milliseconds{ ms };
        const UINT sec = MillisecondsToNextBoundary(t, true);
        CHECK(sec > 0 && sec <= 1000u);
        const UINT min = MillisecondsToNextBoundary(t, false);
        CHECK(min > 0 && min <= 60000u);
    }
}
