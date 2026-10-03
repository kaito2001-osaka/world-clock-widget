// Tests for restoring a saved window position safely (WindowPlacement.hpp).
//
// The gadget has no taskbar button and no Alt-Tab entry, so a position that
// lands off-screen is unrecoverable without editing state.json by hand.
#include "test_framework.hpp"
#include "WindowPlacement.hpp"

namespace {

RECT R(LONG l, LONG t, LONG r, LONG b) { return RECT{ l, t, r, b }; }

// The gadget's seed size.
RECT Window(LONG x, LONG y) { return R(x, y, x + 240, y + 200); }

bool Same(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// A single 1920x1080 monitor with a taskbar along the bottom.
const std::vector<RECT> kOneMonitor = { R(0, 0, 1920, 1040) };

// Primary plus a second monitor to its right.
const std::vector<RECT> kTwoMonitors = { R(0, 0, 1920, 1040), R(1920, 0, 3840, 1040) };

} // namespace

TEST(placement_leaves_a_fully_visible_window_alone) {
    RECT w = Window(400, 300);
    CHECK(Same(ClampToVisibleArea(w, kOneMonitor), w));
    CHECK(Same(ClampToVisibleArea(Window(0, 0), kOneMonitor), Window(0, 0)));
}

TEST(placement_rescues_a_fully_offscreen_window) {
    // The reported case: state.json holding a position from a monitor layout
    // that no longer exists.
    RECT out = ClampToVisibleArea(Window(-5000, -5000), kOneMonitor);
    CHECK(IsSufficientlyVisible(out, kOneMonitor));
    CHECK(Same(out, Window(0, 0)));           // clamped to the work area origin
}

TEST(placement_rescues_a_window_past_the_far_edge) {
    RECT out = ClampToVisibleArea(Window(9000, 9000), kOneMonitor);
    CHECK(IsSufficientlyVisible(out, kOneMonitor));
    CHECK_EQ(out.right, 1920L);
    CHECK_EQ(out.bottom, 1040L);
}

TEST(placement_preserves_the_window_size) {
    // Clamping moves the window; it must never resize it.
    RECT out = ClampToVisibleArea(Window(-5000, -5000), kOneMonitor);
    CHECK_EQ(out.right - out.left, 240L);
    CHECK_EQ(out.bottom - out.top, 200L);
}

TEST(placement_keeps_a_window_that_is_only_partly_visible) {
    // Hanging off the right edge but with a usable grab area left: leave it.
    RECT w = Window(1820, 500);   // 100px still on screen, over kMinVisibleX
    CHECK(IsSufficientlyVisible(w, kOneMonitor));
    CHECK(Same(ClampToVisibleArea(w, kOneMonitor), w));
}

TEST(placement_rescues_a_window_with_only_a_sliver_showing) {
    // Less than kMinVisibleX on screen: no practical grab handle.
    RECT w = Window(1900, 500);   // 20px visible, under kMinVisibleX (48)
    CHECK_EQ(IsSufficientlyVisible(w, kOneMonitor), false);
    RECT out = ClampToVisibleArea(w, kOneMonitor);
    CHECK(IsSufficientlyVisible(out, kOneMonitor));
}

TEST(placement_requires_visibility_on_both_axes) {
    // Plenty of horizontal overlap, but almost entirely below the work area.
    RECT w = Window(400, 1030);   // 10px visible vertically, under kMinVisibleY
    CHECK_EQ(IsSufficientlyVisible(w, kOneMonitor), false);
    CHECK(IsSufficientlyVisible(ClampToVisibleArea(w, kOneMonitor), kOneMonitor));
}

TEST(placement_accepts_a_window_on_a_secondary_monitor) {
    RECT w = Window(2400, 300);
    CHECK(IsSufficientlyVisible(w, kTwoMonitors));
    CHECK(Same(ClampToVisibleArea(w, kTwoMonitors), w));
    // The same position with only the primary attached must be rescued --
    // exactly what happens when the second monitor is unplugged.
    CHECK_EQ(IsSufficientlyVisible(w, kOneMonitor), false);
    CHECK(IsSufficientlyVisible(ClampToVisibleArea(w, kOneMonitor), kOneMonitor));
}

TEST(placement_picks_the_nearest_monitor) {
    // Monitors with a gap between them; a window in the gap goes to whichever
    // is closer, so a shrunken display keeps the widget roughly in place.
    const std::vector<RECT> split = { R(0, 0, 1000, 1000), R(5000, 0, 6000, 1000) };
    RECT nearLeft = ClampToVisibleArea(Window(1500, 200), split);
    CHECK_EQ(nearLeft.right, 1000L);          // pulled back onto the left one
    RECT nearRight = ClampToVisibleArea(Window(4500, 200), split);
    CHECK_EQ(nearRight.left, 5000L);          // pulled onto the right one
}

TEST(placement_pins_an_oversized_window_to_the_origin) {
    // A window taller than the work area must not be pushed to a negative
    // top just to fit its bottom edge on screen.
    const std::vector<RECT> small = { R(100, 100, 400, 300) };
    RECT huge = R(-9000, -9000, -9000 + 800, -9000 + 600);
    RECT out = ClampToVisibleArea(huge, small);
    CHECK_EQ(out.left, 100L);
    CHECK_EQ(out.top, 100L);
    CHECK_EQ(out.right - out.left, 800L);     // still not resized
    CHECK_EQ(out.bottom - out.top, 600L);
}

TEST(placement_with_no_monitors_is_a_no_op) {
    // Nothing to clamp against; returning the input beats inventing a position.
    RECT w = Window(-5000, -5000);
    CHECK(Same(ClampToVisibleArea(w, {}), w));
    CHECK_EQ(IsSufficientlyVisible(w, {}), false);
}

TEST(placement_handles_negative_monitor_origins) {
    // A secondary monitor placed to the left of the primary has negative
    // coordinates; that is a valid position, not an off-screen one.
    const std::vector<RECT> leftOf = { R(-1920, 0, 0, 1040), R(0, 0, 1920, 1040) };
    RECT w = Window(-1500, 200);
    CHECK(IsSufficientlyVisible(w, leftOf));
    CHECK(Same(ClampToVisibleArea(w, leftOf), w));
}

TEST(placement_is_idempotent) {
    // Clamping an already-clamped position changes nothing further.
    RECT once = ClampToVisibleArea(Window(-5000, -5000), kOneMonitor);
    RECT twice = ClampToVisibleArea(once, kOneMonitor);
    CHECK(Same(once, twice));
}

// --- RescueOrigin: re-checking a running window after a monitor change (#26) ---

TEST(rescue_moves_a_window_off_an_unplugged_monitor) {
    // The reported case: the gadget sat on the second monitor, which is gone.
    RECT w = Window(2400, 300);
    std::optional<POINT> to = RescueOrigin(w, kOneMonitor);
    CHECK(to.has_value());
    CHECK(IsSufficientlyVisible(Window(to->x, to->y), kOneMonitor));
    CHECK_EQ(to->x, 1920L - 240L);   // pulled to the near edge of the primary
    CHECK_EQ(to->y, 300L);           // the other axis is kept
}

TEST(rescue_leaves_a_usable_window_alone) {
    // Display notifications fire for many reasons; most must not move it.
    CHECK(!RescueOrigin(Window(400, 300), kOneMonitor).has_value());
    CHECK(!RescueOrigin(Window(2400, 300), kTwoMonitors).has_value());
    CHECK(!RescueOrigin(Window(1820, 500), kOneMonitor).has_value());   // partly off, still grabbable
}

TEST(rescue_waits_while_no_monitor_is_reported) {
    // Mid-change the list can come back empty; there is nowhere to move to.
    CHECK(!RescueOrigin(Window(-5000, -5000), {}).has_value());
}

TEST(rescue_moves_a_window_left_as_a_sliver_by_a_shrunk_work_area) {
    // Resolution dropped from 1920 wide to 1280: the window at x=1250 now shows
    // 30px, under kMinVisibleX.
    const std::vector<RECT> shrunk = { R(0, 0, 1280, 720) };
    std::optional<POINT> to = RescueOrigin(Window(1250, 300), shrunk);
    CHECK(to.has_value());
    CHECK(IsSufficientlyVisible(Window(to->x, to->y), shrunk));
}

TEST(rescue_uses_the_real_window_size) {
    // The running window is whatever size the renderer made it, not the seed.
    RECT wide = R(1900, 300, 1900 + 400, 300 + 150);
    std::optional<POINT> to = RescueOrigin(wide, kOneMonitor);
    CHECK(to.has_value());
    CHECK_EQ(to->x, 1920L - 400L);   // the right edge lands on the work area edge
}
