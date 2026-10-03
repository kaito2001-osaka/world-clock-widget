// Restoring a saved window position safely across monitor changes.
//
// The gadget is a frameless WS_EX_TOOLWINDOW: no taskbar button, no Alt-Tab
// entry. If it restores off-screen there is no way to drag it back, so a saved
// position must be validated against the monitors that actually exist now.
//
// The geometry is pure so it can be unit tested without real displays; the
// caller supplies the work areas (via EnumDisplayMonitors in the gadget).
#pragma once
#include <windows.h>
#include <optional>
#include <vector>

// How much of the window must remain on a monitor for the position to be
// considered usable, in pixels on each axis. Anything less and the user has no
// practical grab handle, so we reposition instead.
constexpr LONG kMinVisibleX = 48;
constexpr LONG kMinVisibleY = 24;

// Returns `desired` unchanged when enough of it is visible on some work area.
// Otherwise returns it moved (never resized) into the nearest work area.
// With no work areas at all -- an empty list -- `desired` is returned as-is,
// since there is nothing to clamp against.
RECT ClampToVisibleArea(RECT desired, const std::vector<RECT>& workAreas);

// True when `desired` overlaps some work area by at least the minimum on both
// axes. Exposed for tests and for callers that only want the predicate.
bool IsSufficientlyVisible(const RECT& desired, const std::vector<RECT>& workAreas);

// For a window that is already up when the monitors change: the origin to move
// it to, or nullopt when it should stay put -- it is still usable where it is,
// or there are no work areas yet (the topology is mid-change; moving it then
// would mean inventing a position).
std::optional<POINT> RescueOrigin(const RECT& current, const std::vector<RECT>& workAreas);
