#include "WindowPlacement.hpp"

#include <algorithm>

namespace {

LONG Width(const RECT& r)  { return r.right - r.left; }
LONG Height(const RECT& r) { return r.bottom - r.top; }

// Overlap of two rects on one axis; 0 when they do not touch.
LONG Overlap(LONG aLo, LONG aHi, LONG bLo, LONG bHi) {
    LONG lo = (std::max)(aLo, bLo);
    LONG hi = (std::min)(aHi, bHi);
    return hi > lo ? hi - lo : 0;
}

// A window narrower than the threshold can never show `kMinVisibleX` pixels,
// so for small windows the requirement is the whole window.
LONG RequiredX(const RECT& r) { return (std::min)(kMinVisibleX, Width(r)); }
LONG RequiredY(const RECT& r) { return (std::min)(kMinVisibleY, Height(r)); }

// Squared gap between two rects (0 when they overlap). long long because the
// virtual desktop can span tens of thousands of pixels.
long long GapSquared(const RECT& a, const RECT& b) {
    long long dx = (std::max)({ (LONG)0, b.left - a.right, a.left - b.right });
    long long dy = (std::max)({ (LONG)0, b.top - a.bottom, a.top - b.bottom });
    return dx * dx + dy * dy;
}

} // namespace

bool IsSufficientlyVisible(const RECT& desired, const std::vector<RECT>& workAreas) {
    const LONG needX = RequiredX(desired);
    const LONG needY = RequiredY(desired);
    for (const RECT& wa : workAreas) {
        if (Overlap(desired.left, desired.right, wa.left, wa.right) >= needX &&
            Overlap(desired.top, desired.bottom, wa.top, wa.bottom) >= needY)
            return true;
    }
    return false;
}

RECT ClampToVisibleArea(RECT desired, const std::vector<RECT>& workAreas) {
    if (workAreas.empty()) return desired;              // nothing to clamp against
    if (IsSufficientlyVisible(desired, workAreas)) return desired;

    // Pick the monitor whose work area is closest to where the window wanted
    // to be, so a display that merely shrank keeps the widget roughly in place.
    const RECT* best = &workAreas.front();
    long long bestGap = GapSquared(desired, *best);
    for (const RECT& wa : workAreas) {
        long long gap = GapSquared(desired, wa);
        if (gap < bestGap) { bestGap = gap; best = &wa; }
    }

    // Move (never resize) the window inside that work area. A window larger
    // than the work area is pinned to its origin rather than pushed negative.
    const LONG w = Width(desired), h = Height(desired);
    LONG x = desired.left, y = desired.top;
    x = (w >= Width(*best))  ? best->left : (std::min)((std::max)(x, best->left), best->right - w);
    y = (h >= Height(*best)) ? best->top  : (std::min)((std::max)(y, best->top), best->bottom - h);

    return RECT{ x, y, x + w, y + h };
}

std::optional<POINT> RescueOrigin(const RECT& current, const std::vector<RECT>& workAreas) {
    const RECT placed = ClampToVisibleArea(current, workAreas);
    if (placed.left == current.left && placed.top == current.top) return std::nullopt;
    return POINT{ placed.left, placed.top };
}
