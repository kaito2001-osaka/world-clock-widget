#include "PanelLayout.hpp"

#include <algorithm>
#include <cmath>

namespace {

// Grow a box by `mx` sideways and `my` up and down, round out to whole pixels
// and clamp to the panel.
RECT DirtyRect(float l, float t, float r, float b, float mx, float my, SIZE panel) {
    RECT rc;
    rc.left   = (std::max)(0L, (LONG)std::floor(l - mx));
    rc.top    = (std::max)(0L, (LONG)std::floor(t - my));
    rc.right  = (std::min)(panel.cx, (LONG)std::ceil(r + mx));
    rc.bottom = (std::min)(panel.cy, (LONG)std::ceil(b + my));
    if (rc.right < rc.left) rc.right = rc.left;
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    return rc;
}

} // namespace

std::vector<CityGeometry> ComputePanelLayout(const PanelLayoutInput& in) {
    const size_t n = in.blockH.size();
    std::vector<CityGeometry> out(n);

    // Half the gap between cities, less the pixel that rounding out can add:
    // a dirty rect then stays strictly inside its own half of the gap, so no
    // two overlap and none reaches a neighbour's ink.
    const float margin = (std::max)(0.f, in.cityGap / 2.f - 1.f);
    const float r = in.faceD / 2.f;

    float x = in.pad, y = in.pad;
    for (size_t i = 0; i < n; ++i) {
        CityGeometry& g = out[i];
        const float blockW = in.horizontal ? in.blockW[i] : in.maxW;
        const float left = in.horizontal ? x : in.pad;
        g.left = left;
        g.top = y;
        g.width = blockW;
        g.height = in.blockH[i];

        if (in.analog) {
            g.faceR = r;
            g.faceCx = left + blockW / 2.f;
            g.faceCy = y + r;
            float yy = y + in.faceD;
            if (in.showDate) {
                yy += in.lineGap;
                g.dateY = yy;
                yy += in.dateH;
            }
            yy += in.lineGap;
            g.labelY = yy;
            g.dirty = DirtyRect(g.faceCx - r, g.faceCy - r, g.faceCx + r, g.faceCy + r,
                                margin, margin, in.panel);
        } else {
            float yy = y;
            g.labelY = yy;
            yy += in.labelH[i] + in.lineGap;
            g.timeY = yy;
            yy += in.timeH;
            if (in.showDate) {
                yy += in.lineGap;
                g.dateY = yy;
            }
            g.dirty = DirtyRect(left, g.timeY, left + blockW, g.timeY + in.timeH,
                                margin, in.lineGap / 2.f, in.panel);
        }
        g.dirtyAvoidsCorners = AvoidsCorners(g.dirty, in.panel, in.corner);

        if (in.horizontal) x += blockW + in.cityGap;
        else               y += in.blockH[i] + in.cityGap;
    }
    return out;
}

RECT DirtyUnion(const std::vector<CityGeometry>& cities) {
    if (cities.empty()) return RECT{ 0, 0, 0, 0 };
    RECT u = cities.front().dirty;
    for (const CityGeometry& g : cities) {
        u.left   = (std::min)(u.left, g.dirty.left);
        u.top    = (std::min)(u.top, g.dirty.top);
        u.right  = (std::max)(u.right, g.dirty.right);
        u.bottom = (std::max)(u.bottom, g.dirty.bottom);
    }
    return u;
}

bool InkReaches(const RECT& dirty, float x, float y, float w, float h, const InkOverhang& o) {
    // One pixel of anti-aliasing beyond the ink on every side.
    const float l = x - o.left - 1.f;
    const float t = y - o.top - 1.f;
    const float r = x + w + o.right + 1.f;
    const float b = y + h + o.bottom + 1.f;
    return l < (float)dirty.right && (float)dirty.left < r
        && t < (float)dirty.bottom && (float)dirty.top < b;
}

bool AvoidsCorners(const RECT& r, SIZE panel, float radius) {
    // Per corner, the part of `r` nearest that corner of the panel. Outside the
    // radius x radius corner square on either axis it is in a straight-edged
    // band, which the pixel-aligned panel edges cover fully. Inside, it has to
    // sit a pixel within the arc, clear of its anti-aliased edge.
    auto clear = [&](float dx, float dy) {   // distances past the arc's centre
        if (dx <= 0.f || dy <= 0.f) return true;
        return std::hypot(dx, dy) <= radius - 1.f;
    };
    const float w = (float)panel.cx, h = (float)panel.cy;
    return clear(radius - (float)r.left, radius - (float)r.top)
        && clear((float)r.right - (w - radius), radius - (float)r.top)
        && clear(radius - (float)r.left, (float)r.bottom - (h - radius))
        && clear((float)r.right - (w - radius), (float)r.bottom - (h - radius));
}
