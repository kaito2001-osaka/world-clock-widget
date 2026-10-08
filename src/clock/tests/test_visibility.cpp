// Tests for pausing the tick while nobody can see the gadget (Visibility.hpp).
#include "test_framework.hpp"
#include "Visibility.hpp"

namespace {

using E = VisibilityEvent;
using T = VisibilityTransition;

} // namespace

// ---- single causes --------------------------------------------------------

TEST(visibility_starts_visible) {
    CHECK(IsVisible(Visibility{}));
}

TEST(visibility_display_off_hides_and_on_shows) {
    Visibility v;
    CHECK(ApplyVisibilityEvent(v, E::DisplayOff) == T::Hidden);
    CHECK(!IsVisible(v));
    CHECK(ApplyVisibilityEvent(v, E::DisplayOn) == T::Shown);
    CHECK(IsVisible(v));
}

TEST(visibility_dimmed_keeps_rendering) {
    Visibility v;
    CHECK(ApplyVisibilityEvent(v, E::DisplayDimmed) == T::None);
    CHECK(IsVisible(v));
}

TEST(visibility_dimmed_after_off_shows) {
    // Off -> dimmed happens when input wakes the screen to the dimmed level.
    Visibility v;
    ApplyVisibilityEvent(v, E::DisplayOff);
    CHECK(ApplyVisibilityEvent(v, E::DisplayDimmed) == T::Shown);
}

TEST(visibility_lock_hides_and_unlock_shows) {
    Visibility v;
    CHECK(ApplyVisibilityEvent(v, E::SessionLock) == T::Hidden);
    CHECK(ApplyVisibilityEvent(v, E::SessionUnlock) == T::Shown);
}

TEST(visibility_disconnect_hides_and_connect_shows) {
    Visibility v;
    CHECK(ApplyVisibilityEvent(v, E::SessionDisconnect) == T::Hidden);
    CHECK(ApplyVisibilityEvent(v, E::SessionConnect) == T::Shown);
}

// ---- repeated notifications -----------------------------------------------

TEST(visibility_repeats_are_not_transitions) {
    // The current display state is also sent right after registering, so an
    // "on" while already on is routine.
    Visibility v;
    CHECK(ApplyVisibilityEvent(v, E::DisplayOn) == T::None);
    CHECK(ApplyVisibilityEvent(v, E::SessionUnlock) == T::None);
    ApplyVisibilityEvent(v, E::DisplayOff);
    CHECK(ApplyVisibilityEvent(v, E::DisplayOff) == T::None);
    ApplyVisibilityEvent(v, E::SessionLock);
    CHECK(ApplyVisibilityEvent(v, E::SessionLock) == T::None);
}

// ---- overlapping causes ---------------------------------------------------

TEST(visibility_display_on_behind_the_lock_screen_stays_hidden) {
    // Lock, the screen times out, then a key press lights it: the lock
    // screen is up, not the desktop.
    Visibility v;
    CHECK(ApplyVisibilityEvent(v, E::SessionLock) == T::Hidden);
    CHECK(ApplyVisibilityEvent(v, E::DisplayOff) == T::None);
    CHECK(ApplyVisibilityEvent(v, E::DisplayOn) == T::None);
    CHECK(!IsVisible(v));
    CHECK(ApplyVisibilityEvent(v, E::SessionUnlock) == T::Shown);
}

TEST(visibility_unlock_with_the_display_still_off_stays_hidden) {
    Visibility v;
    ApplyVisibilityEvent(v, E::DisplayOff);
    CHECK(ApplyVisibilityEvent(v, E::SessionLock) == T::None);
    CHECK(ApplyVisibilityEvent(v, E::SessionUnlock) == T::None);
    CHECK(ApplyVisibilityEvent(v, E::DisplayOn) == T::Shown);
}

TEST(visibility_remote_reconnect_to_a_locked_session_stays_hidden) {
    // An RDP client leaves a locked session; reconnecting lands on the lock
    // screen until the user signs in.
    Visibility v;
    ApplyVisibilityEvent(v, E::SessionLock);
    CHECK(ApplyVisibilityEvent(v, E::SessionDisconnect) == T::None);
    CHECK(ApplyVisibilityEvent(v, E::SessionConnect) == T::None);
    CHECK(ApplyVisibilityEvent(v, E::SessionUnlock) == T::Shown);
}

// ---- notification decoding ------------------------------------------------

TEST(visibility_display_status_values) {
    CHECK(DisplayStatusEvent(0) == E::DisplayOff);
    CHECK(DisplayStatusEvent(1) == E::DisplayOn);
    CHECK(DisplayStatusEvent(2) == E::DisplayDimmed);
}

TEST(visibility_unknown_display_status_is_ignored) {
    // Never stop the clock on a value we do not understand.
    CHECK(!DisplayStatusEvent(3).has_value());
    CHECK(!DisplayStatusEvent(0xFFFFFFFF).has_value());
}

TEST(visibility_session_change_codes) {
    CHECK(SessionChangeEvent(WTS_SESSION_LOCK) == E::SessionLock);
    CHECK(SessionChangeEvent(WTS_SESSION_UNLOCK) == E::SessionUnlock);
    CHECK(SessionChangeEvent(WTS_CONSOLE_DISCONNECT) == E::SessionDisconnect);
    CHECK(SessionChangeEvent(WTS_REMOTE_DISCONNECT) == E::SessionDisconnect);
    CHECK(SessionChangeEvent(WTS_CONSOLE_CONNECT) == E::SessionConnect);
    CHECK(SessionChangeEvent(WTS_REMOTE_CONNECT) == E::SessionConnect);
}

TEST(visibility_unrelated_session_codes_are_ignored) {
    CHECK(!SessionChangeEvent(WTS_SESSION_LOGON).has_value());
    CHECK(!SessionChangeEvent(WTS_SESSION_LOGOFF).has_value());
    CHECK(!SessionChangeEvent(WTS_SESSION_REMOTE_CONTROL).has_value());
}
