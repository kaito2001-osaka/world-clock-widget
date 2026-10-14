// PanelLayout: block positions and the dirty rects a steady-state frame
// repaints. A dirty rect that misses a pixel the clock line or face can touch
// leaves a stale pixel on screen; one that reaches another city's block wipes
// that city's ink without redrawing it. Both are checked here for every
// display option that changes the geometry.
#include "test_framework.hpp"
#include "PanelLayout.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct Options {
    bool analog = false;
    bool horizontal = false;
    bool showDate = true;
    int  cities = 4;
    float s = 1.f;      // DPI / 96 times the size class scale
};

// Plausible measured sizes, built the way Renderer::MeasureBlock builds them.
// Widths vary per city so vertical (shared maxW) and horizontal (own width)
// layouts differ.
PanelLayoutInput MakeInput(const Options& o) {
    PanelLayoutInput in;
    in.analog = o.analog;
    in.horizontal = o.horizontal;
    in.showDate = o.showDate;
    in.pad = 14.f * o.s;
    in.lineGap = 2.f * o.s;
    in.cityGap = 12.f * o.s;
    in.faceD = 80.f * o.s;
    in.corner = 10.f * o.s;
    in.timeH = 35.11f * o.s;
    in.dateH = o.showDate ? 14.63f * o.s : 0.f;
    const float dateW = o.showDate ? 61.f * o.s : 0.f;
    const float timeW = 71.f * o.s;

    float along = 0.f, cross = 0.f;
    for (int i = 0; i < o.cities; ++i) {
        const float labelW = std::ceil((40.f + 9.f * float(i % 5)) * o.s);
        const float labelH = 17.29f * o.s;
        float w, h;
        if (o.analog) {
            w = in.faceD; h = in.faceD;
            if (o.showDate) { w = (std::max)(w, dateW); h += in.lineGap + in.dateH; }
            w = (std::max)(w, labelW); h += in.lineGap + labelH;
        } else {
            w = (std::max)(labelW, timeW);
            h = labelH + in.lineGap + in.timeH;
            if (o.showDate) { w = (std::max)(w, dateW); h += in.lineGap + in.dateH; }
        }
        in.blockW.push_back(w);
        in.blockH.push_back(h);
        in.labelH.push_back(labelH);
        in.maxW = (std::max)(in.maxW, w);
        const float a = o.horizontal ? w : h;
        const float b = o.horizontal ? h : w;
        along += a;
        if (i + 1 < o.cities) along += in.cityGap;
        cross = (std::max)(cross, b);
    }
    const float pw = o.horizontal ? along : cross;
    const float ph = o.horizontal ? cross : along;
    in.panel = { (LONG)std::ceil(pw + 2 * in.pad), (LONG)std::ceil(ph + 2 * in.pad) };
    return in;
}

// The position walk DrawDigital / DrawAnalog did before it was factored out,
// verbatim, so the factored version can be held to the same floats.
struct RefPos { float left, top, width, labelY, timeY, dateY, cx, cy; };
std::vector<RefPos> ReferenceWalk(const PanelLayoutInput& in) {
    std::vector<RefPos> out;
    const float r = in.faceD / 2.f;
    float x = in.pad, y = in.pad;
    for (size_t i = 0; i < in.blockH.size(); ++i) {
        RefPos p{};
        float blockW = in.horizontal ? in.blockW[i] : in.maxW;
        float left = in.horizontal ? x : in.pad;
        p.left = left; p.top = y; p.width = blockW;
        if (in.analog) {
            p.cx = left + blockW / 2.f;
            p.cy = y + r;
            float yy = y + in.faceD;
            if (in.showDate) { yy += in.lineGap; p.dateY = yy; yy += in.dateH; }
            yy += in.lineGap;
            p.labelY = yy;
        } else {
            float yy = y;
            p.labelY = yy;
            yy += in.labelH[i] + in.lineGap;
            p.timeY = yy;
            yy += in.timeH;
            if (in.showDate) { yy += in.lineGap; p.dateY = yy; }
        }
        out.push_back(p);
        if (in.horizontal) x += blockW + in.cityGap;
        else               y += in.blockH[i] + in.cityGap;
    }
    return out;
}

bool Intersects(const RECT& a, const RECT& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// Every property the renderer relies on, for one set of options.
void CheckLayout(const Options& o) {
    const PanelLayoutInput in = MakeInput(o);
    const std::vector<CityGeometry> g = ComputePanelLayout(in);
    const std::vector<RefPos> ref = ReferenceWalk(in);
    CHECK_EQ(g.size(), (size_t)o.cities);

    const float halfGap = in.cityGap / 2.f;
    const float tickStroke = 1.5f * o.s;
    for (size_t i = 0; i < g.size(); ++i) {
        // Same floats as the old walk, so a full redraw is unchanged.
        CHECK(g[i].left == ref[i].left);
        CHECK(g[i].top == ref[i].top);
        CHECK(g[i].width == ref[i].width);
        CHECK(g[i].labelY == ref[i].labelY);
        if (o.showDate) CHECK(g[i].dateY == ref[i].dateY);
        if (o.analog) {
            CHECK(g[i].faceCx == ref[i].cx);
            CHECK(g[i].faceCy == ref[i].cy);
            CHECK(g[i].faceR == in.faceD / 2.f);
        } else {
            CHECK(g[i].timeY == ref[i].timeY);
        }

        const RECT& d = g[i].dirty;
        // Inside the panel, and not empty.
        CHECK(d.left >= 0 && d.top >= 0);
        CHECK(d.right <= in.panel.cx && d.bottom <= in.panel.cy);
        CHECK(d.left < d.right && d.top < d.bottom);

        // Covers everything that changes: the clock line, or the face with
        // the outer half of its rim stroke plus a pixel of anti-aliasing.
        float l, t, r, b;
        if (o.analog) {
            const float grow = g[i].faceR + tickStroke / 2.f + 1.f;
            l = g[i].faceCx - grow; r = g[i].faceCx + grow;
            t = g[i].faceCy - grow; b = g[i].faceCy + grow;
        } else {
            l = g[i].left; r = g[i].left + g[i].width;
            t = g[i].timeY; b = g[i].timeY + in.timeH;
        }
        CHECK((float)d.left <= l && (float)d.right >= r);
        CHECK((float)d.top <= t && (float)d.bottom >= b);

        // Above and below a clock line, no further than half the line gap
        // (plus the pixel rounding out adds): the label and date lines start
        // a line gap away.
        if (!o.analog) {
            CHECK((float)d.top >= g[i].timeY - in.lineGap / 2.f - 1.f);
            CHECK((float)d.bottom <= g[i].timeY + in.timeH + in.lineGap / 2.f + 1.f);
        }

        CHECK_EQ(g[i].dirtyAvoidsCorners, AvoidsCorners(d, in.panel, in.corner));

        // Stays strictly within its own block plus half the gap on each side,
        // which no other block reaches.
        CHECK((float)d.left > g[i].left - halfGap);
        CHECK((float)d.right < g[i].left + g[i].width + halfGap);
        CHECK((float)d.top > g[i].top - halfGap);
        CHECK((float)d.bottom < g[i].top + g[i].height + halfGap);

        for (size_t j = 0; j < g.size(); ++j) {
            if (j == i) continue;
            CHECK(!Intersects(d, g[j].dirty));
            const RECT other = { (LONG)std::floor(g[j].left), (LONG)std::floor(g[j].top),
                                 (LONG)std::ceil(g[j].left + g[j].width),
                                 (LONG)std::ceil(g[j].top + g[j].height) };
            CHECK(!Intersects(d, other));
        }
    }

    // The present covers every dirty rect and nothing beyond their bounds.
    const RECT u = DirtyUnion(g);
    RECT expect = g.front().dirty;
    for (const CityGeometry& c : g) {
        expect.left = (std::min)(expect.left, c.dirty.left);
        expect.top = (std::min)(expect.top, c.dirty.top);
        expect.right = (std::max)(expect.right, c.dirty.right);
        expect.bottom = (std::max)(expect.bottom, c.dirty.bottom);
    }
    CHECK_EQ(u.left, expect.left);
    CHECK_EQ(u.top, expect.top);
    CHECK_EQ(u.right, expect.right);
    CHECK_EQ(u.bottom, expect.bottom);
}

} // namespace

TEST(panel_layout_every_option) {
    const float sizeScales[] = { 0.8f, 1.0f, 1.3f };
    const float dpiScales[] = { 1.0f, 1.5f, 2.0f };   // 96, 144, 192 dpi
    for (bool analog : { false, true })
        for (bool horizontal : { false, true })
            for (bool showDate : { false, true })
                for (int cities : { 1, 4, 8 })
                    for (float z : sizeScales)
                        for (float d : dpiScales)
                            CheckLayout({ analog, horizontal, showDate, cities, z * d });
}

TEST(panel_layout_no_cities) {
    PanelLayoutInput in = MakeInput({ false, false, true, 0, 1.f });
    CHECK(ComputePanelLayout(in).empty());
    const RECT u = DirtyUnion({});
    CHECK_EQ(u.left, 0L);
    CHECK_EQ(u.top, 0L);
    CHECK_EQ(u.right, 0L);
    CHECK_EQ(u.bottom, 0L);
}

TEST(panel_layout_dirty_clamped_to_panel) {
    // No padding: the first block starts at the panel edge, so growing its
    // dirty rect would leave the surface.
    PanelLayoutInput in = MakeInput({ true, false, true, 2, 1.f });
    in.pad = 0.f;
    in.panel = { (LONG)std::ceil(in.maxW), (LONG)std::ceil(in.blockH[0] + in.cityGap + in.blockH[1]) };
    const std::vector<CityGeometry> g = ComputePanelLayout(in);
    CHECK_EQ(g[0].dirty.top, 0L);
    CHECK(g[0].dirty.left >= 0);
    CHECK(g[1].dirty.right <= in.panel.cx);
    CHECK(g[1].dirty.bottom <= in.panel.cy);
}

TEST(panel_layout_vertical_blocks_share_width) {
    // Vertical layout lines up left edges at the widest block's width, which
    // is what the label and date layouts are created against.
    const PanelLayoutInput in = MakeInput({ false, false, true, 4, 1.f });
    const std::vector<CityGeometry> g = ComputePanelLayout(in);
    for (const CityGeometry& c : g) {
        CHECK(c.left == in.pad);
        CHECK(c.width == in.maxW);
    }
    // Horizontal: each block keeps its own width and they share a top.
    const PanelLayoutInput h = MakeInput({ false, true, true, 4, 1.f });
    const std::vector<CityGeometry> gh = ComputePanelLayout(h);
    for (size_t i = 0; i < gh.size(); ++i) {
        CHECK(gh[i].top == h.pad);
        CHECK(gh[i].width == h.blockW[i]);
    }
}

TEST(panel_layout_default_panels_avoid_corners) {
    // With the real padding (14) larger than the corner radius (10) the dirty
    // rects of an ordinary panel never touch a corner, so steady-state frames
    // take the cheap background path.
    for (bool analog : { false, true })
        for (bool horizontal : { false, true })
            for (float s : { 0.8f, 1.0f, 1.5f, 2.6f }) {
                const PanelLayoutInput in = MakeInput({ analog, horizontal, true, 4, s });
                for (const CityGeometry& g : ComputePanelLayout(in))
                    CHECK(g.dirtyAvoidsCorners);
            }
}

// ---- AvoidsCorners --------------------------------------------------------

TEST(avoids_corners_stays_a_pixel_inside_each_arc) {
    const SIZE panel = { 100, 60 };
    // Radius 10: the arcs are centred 10 px in from each corner, and a rect
    // corner has to be within 9 px of that centre.
    CHECK(AvoidsCorners(RECT{ 4, 4, 96, 56 }, panel, 10.f));        // 6,6 -> 8.49
    CHECK(AvoidsCorners(RECT{ 5, 5, 95, 55 }, panel, 10.f));
    // One corner at a time at 7,7 -> 9.90; the other three at most 7,5 -> 8.60.
    CHECK(!AvoidsCorners(RECT{ 3, 3, 95, 55 }, panel, 10.f));       // top-left
    CHECK(!AvoidsCorners(RECT{ 5, 3, 97, 55 }, panel, 10.f));       // top-right
    CHECK(!AvoidsCorners(RECT{ 3, 5, 95, 57 }, panel, 10.f));       // bottom-left
    CHECK(!AvoidsCorners(RECT{ 5, 5, 97, 57 }, panel, 10.f));       // bottom-right
    CHECK(!AvoidsCorners(RECT{ 0, 0, 5, 5 }, panel, 10.f));
    // 9 px from the centre along an axis is the straight edge's band; 9,1 is
    // already a hair too far round the arc.
    CHECK(AvoidsCorners(RECT{ 1, 10, 50, 30 }, panel, 10.f));
    CHECK(!AvoidsCorners(RECT{ 1, 9, 50, 30 }, panel, 10.f));       // 9,1 -> 9.06
    // Running along an edge between the corners is fine: the panel edges are
    // pixel-aligned and fully covered.
    CHECK(AvoidsCorners(RECT{ 0, 20, 100, 40 }, panel, 10.f));
    CHECK(AvoidsCorners(RECT{ 30, 0, 70, 60 }, panel, 10.f));
    CHECK(!AvoidsCorners(RECT{ 0, 5, 100, 55 }, panel, 10.f));      // 10,5 -> 11.2
    // A larger radius pushes the arc in.
    CHECK(AvoidsCorners(RECT{ 4, 4, 96, 56 }, panel, 10.4f));       // 6.4,6.4 -> 9.05 <= 9.4
    CHECK(!AvoidsCorners(RECT{ 3, 3, 97, 57 }, panel, 10.4f));      // 7.4,7.4 -> 10.5
}

// ---- InkReaches -----------------------------------------------------------

TEST(ink_reaches_counts_overhang_and_a_pixel_of_aa) {
    const RECT dirty = { 10, 50, 110, 80 };
    const InkOverhang none{};
    // A line box ending 2 px above the rect: one pixel of AA still misses.
    CHECK(!InkReaches(dirty, 10.f, 30.f, 100.f, 18.f, none));
    // Ending 1 px above: the AA pixel touches the rect edge but not inside.
    CHECK(!InkReaches(dirty, 10.f, 30.f, 100.f, 19.f, none));
    // Ink that hangs 0.5 px past the bottom of the box reaches in.
    CHECK(InkReaches(dirty, 10.f, 30.f, 100.f, 19.f, InkOverhang{ 0.f, 0.f, 0.f, 0.5f }));
    // Ink that stays well inside its box (negative overhang, as for a line
    // with no descenders) does not, even though the box itself overlaps.
    CHECK(!InkReaches(dirty, 10.f, 40.f, 100.f, 15.f, InkOverhang{ 0.f, 0.f, 0.f, -6.f }));
    CHECK(InkReaches(dirty, 10.f, 40.f, 100.f, 15.f, InkOverhang{ 0.f, 0.f, 0.f, -4.f }));
    // Below the rect, and to either side.
    CHECK(!InkReaches(dirty, 10.f, 81.f, 100.f, 10.f, none));
    CHECK(InkReaches(dirty, 10.f, 80.5f, 100.f, 10.f, none));
    CHECK(!InkReaches(dirty, 111.f, 50.f, 20.f, 10.f, none));
    CHECK(!InkReaches(dirty, -20.f, 50.f, 29.f, 10.f, none));
    CHECK(InkReaches(dirty, -20.f, 50.f, 29.5f, 10.f, none));
}
