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

// ---- cadence follows the displayed resolution -----------------------------

namespace {

Config WithSeconds(bool on) {
    Config c;
    c.showSeconds = on;
    return c;
}

// Replay the actual re-arm loop -- wake, advance by whatever interval was
// returned, wake again -- and count the wakeups over `span`.
int WakeupsOver(minutes span, const Config& cfg, long long startOffsetMs) {
    auto now = AtOffset(startOffsetMs);
    const auto end = now + span;
    int wakeups = 0;
    while (now < end) {
        now += milliseconds{ MillisecondsToNextTick(now, cfg) };
        ++wakeups;
    }
    return wakeups;
}

} // namespace

TEST(cadence_tick_picks_the_boundary_from_the_config) {
    for (long long ms = 0; ms < 60000; ms += 1013) {
        CHECK_EQ(MillisecondsToNextTick(AtOffset(ms), WithSeconds(true)),
                 MillisecondsToNextBoundary(AtOffset(ms), true));
        CHECK_EQ(MillisecondsToNextTick(AtOffset(ms), WithSeconds(false)),
                 MillisecondsToNextBoundary(AtOffset(ms), false));
    }
}

TEST(cadence_hiding_seconds_sleeps_a_whole_minute) {
    // The default configuration. Waking once a second to redraw a minute hand
    // is 59 wasted wakeups out of every 60.
    Config dflt;
    CHECK_EQ(dflt.showSeconds, false);
    CHECK_EQ(MillisecondsToNextTick(AtOffset(0), dflt), 60000u);
    CHECK_EQ(MillisecondsToNextTick(AtOffset(1000), dflt), 59000u);
}

TEST(cadence_wakeup_count_drops_from_60_per_minute_to_1) {
    // The whole point of the change, measured on the real re-arm loop.
    CHECK_EQ(WakeupsOver(minutes{ 1 }, WithSeconds(false), 0), 1);
    CHECK_EQ(WakeupsOver(minutes{ 1 }, WithSeconds(true), 0), 60);

    CHECK_EQ(WakeupsOver(minutes{ 60 }, WithSeconds(false), 0), 60);
    CHECK_EQ(WakeupsOver(minutes{ 60 }, WithSeconds(true), 0), 3600);
}

TEST(cadence_wakeup_count_holds_from_any_starting_offset) {
    // Starting mid-period, the first interval is short and every one after it
    // is a full period, so the final wakeup lands just past the window: one
    // extra at most, never fewer, and never a burst.
    for (long long start : { 0LL, 1LL, 250LL, 999LL, 30000LL, 59999LL }) {
        const int mins = WakeupsOver(minutes{ 60 }, WithSeconds(false), start);
        CHECK(mins >= 60 && mins <= 61);
        const int secs = WakeupsOver(minutes{ 60 }, WithSeconds(true), start);
        CHECK(secs >= 3600 && secs <= 3601);
        // The reduction itself, whatever the phase.
        CHECK(secs / mins >= 59);
    }
}

TEST(cadence_showing_seconds_still_ticks_every_second) {
    // The reduction must not cost resolution when seconds are on display.
    Config on = WithSeconds(true);
    for (long long ms = 0; ms < 60000; ms += 331)
        CHECK(MillisecondsToNextTick(AtOffset(ms), on) <= 1000u);
}

// ---- redraw gate ----------------------------------------------------------
//
// The gate used to compare only tm_sec / tm_min, so a jump by an exact minute
// (seconds shown) or an exact hour (minutes shown) -- resume from sleep, a
// clock change -- read as "no change" and left the old time up (#25).

TEST(redraw_displayed_instant_floors_to_the_resolution) {
    CHECK(DisplayedInstant(AtOffset(1999), true) == AtOffset(1000));
    CHECK(DisplayedInstant(AtOffset(59999), true) == AtOffset(59000));
    CHECK(DisplayedInstant(AtOffset(59999), false) == AtOffset(0));
    CHECK(DisplayedInstant(AtOffset(60000), false) == AtOffset(60000));
}

TEST(redraw_first_frame_always_draws) {
    CHECK(NeedsRedraw(std::nullopt, AtOffset(0), true));
    CHECK(NeedsRedraw(std::nullopt, AtOffset(0), false));
}

TEST(redraw_skips_when_the_displayed_instant_is_unchanged) {
    CHECK(!NeedsRedraw(DisplayedInstant(AtOffset(1000), true), AtOffset(1999), true));
    CHECK(!NeedsRedraw(DisplayedInstant(AtOffset(0), false), AtOffset(59999), false));
}

TEST(redraw_draws_on_the_next_second_or_minute) {
    CHECK(NeedsRedraw(DisplayedInstant(AtOffset(1999), true), AtOffset(2000), true));
    CHECK(NeedsRedraw(DisplayedInstant(AtOffset(59999), false), AtOffset(60000), false));
}

TEST(redraw_draws_after_an_exact_minute_jump_with_seconds_shown) {
    // Same tm_sec on both sides.
    const auto drawn = DisplayedInstant(AtOffset(5000), true);
    CHECK(NeedsRedraw(drawn, AtOffset(5000 + 60000), true));
    CHECK(NeedsRedraw(drawn, AtOffset(5000 + 3 * 60000), true));
}

TEST(redraw_draws_after_an_exact_hour_jump_with_minutes_shown) {
    // Sleep at 14:05, wake in 17:05: same tm_min on both sides.
    const long long hour = 60LL * 60000;
    const auto drawn = DisplayedInstant(AtOffset(5 * 60000), false);
    CHECK(NeedsRedraw(drawn, AtOffset(5 * 60000 + hour), false));
    CHECK(NeedsRedraw(drawn, AtOffset(5 * 60000 + 3 * hour + 30000), false));
}

TEST(redraw_draws_after_a_backwards_jump) {
    const long long hour = 60LL * 60000;
    const auto lastMin = DisplayedInstant(AtOffset(2 * hour), false);
    CHECK(NeedsRedraw(lastMin, AtOffset(hour), false));
    const auto lastSec = DisplayedInstant(AtOffset(2 * hour), true);
    CHECK(NeedsRedraw(lastSec, AtOffset(2 * hour - 60000), true));
}

TEST(redraw_floors_pre_epoch_instants_down) {
    // duration_cast would round toward zero, i.e. up, before 1970.
    const auto preEpoch = system_clock::time_point{
        sys_days{ year{ 1969 } / month{ 12 } / day{ 31 } }.time_since_epoch() };
    const auto t = preEpoch + milliseconds{ 1500 };
    CHECK(DisplayedInstant(t, true) == preEpoch + seconds{ 1 });
    CHECK(DisplayedInstant(t, false) == preEpoch);
    CHECK(DisplayedInstant(preEpoch - milliseconds{ 1 }, false) == preEpoch - minutes{ 1 });
}
