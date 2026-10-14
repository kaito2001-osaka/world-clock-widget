// Where each city's block sits on the panel, and which pixels can change from
// one frame to the next.
//
// Only a city's clock line (digital) or face (analog) changes between frames;
// its label and date stay put until the config, the DPI or the day changes.
// The renderer repaints just the dirty rect of each city on a steady-state
// frame and presents just their union, so the geometry it clips to has to be
// exact. It is pure arithmetic over the measured sizes, so it is computed here
// where it can be unit tested without a render target.
#pragma once
#include <windows.h>
#include <vector>

struct PanelLayoutInput {
    bool  analog = false;
    bool  horizontal = false;
    bool  showDate = false;

    // Scaled lengths, device pixels.
    float pad = 0.f, lineGap = 0.f, cityGap = 0.f;
    float faceD = 0.f;
    float corner = 0.f;  // panel corner radius

    float maxW = 0.f;    // widest block; every block's width in vertical layout
    float timeH = 0.f;   // worst-case clock line height (digital)
    float dateH = 0.f;   // worst-case date line height

    // Per city, in config order.
    std::vector<float> blockW;   // own block width (used in horizontal layout)
    std::vector<float> blockH;   // own block height
    std::vector<float> labelH;   // label line height

    SIZE panel = { 0, 0 };       // the surface the dirty rects are clamped to
};

struct CityGeometry {
    float left = 0.f, top = 0.f;   // block origin
    float width = 0.f;             // block width as drawn (maxW in vertical)
    float height = 0.f;            // block height

    float labelY = 0.f;            // top of the label line
    float timeY = 0.f;             // top of the clock line (digital)
    float dateY = 0.f;             // top of the date line

    float faceCx = 0.f, faceCy = 0.f, faceR = 0.f;   // analog face

    // Pixels a steady-state frame repaints for this city: the clock line or
    // the face, rounded out to whole pixels and clamped to the panel.
    //  - Sideways, and around a face, it grows by up to half the gap between
    //    cities, so stroke and glyph ink just past the box is still covered
    //    without reaching another city's block.
    //  - Above and below a clock line it grows by half the line gap: the
    //    glyphs stay inside their line box, and the label and date lines are
    //    a line gap away.
    RECT dirty = { 0, 0, 0, 0 };

    // The dirty rect misses the panel's rounded corners, so the background
    // there is a plain fill of the panel colour.
    bool dirtyAvoidsCorners = false;
};

// Positions in the same order and with the same float expressions the draw
// code used before this was factored out, so a full redraw is unchanged to
// the byte.
std::vector<CityGeometry> ComputePanelLayout(const PanelLayoutInput& in);

// Bounding rect of every city's dirty rect: what a steady-state frame
// presents. Empty (all zero) when there are no cities.
RECT DirtyUnion(const std::vector<CityGeometry>& cities);

// How far ink reaches past a text layout's box on each side, as DirectWrite's
// GetOverhangMetrics reports it (positive = outside the box).
struct InkOverhang {
    float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;
};

// Whether text laid out in the box at (x, y), w x h, can put any pixel inside
// `dirty`: its ink box, grown by a pixel for anti-aliasing, overlaps it. A
// steady-state frame redraws a label or date inside a city's clip only when
// this is true; otherwise the clip could not touch it anyway.
bool InkReaches(const RECT& dirty, float x, float y, float w, float h, const InkOverhang& o);

// Whether every pixel of `r` is fully covered by the rounded background of a
// panel this size: it stays at least a pixel inside each corner's arc (the
// arc's anti-aliased edge is the only partial coverage, as the panel's
// straight edges are pixel-aligned).
bool AvoidsCorners(const RECT& r, SIZE panel, float radius);
