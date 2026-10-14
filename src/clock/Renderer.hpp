// Direct2D / DirectWrite renderer for the gadget panel.
//
// Rendering model: we render into a 32-bit premultiplied-alpha DIB through a
// Direct2D WIC bitmap render target whose bitmap is a view of the DIB's memory
// (DibWicBitmap.hpp), then present with UpdateLayeredWindow. This gives true
// per-pixel alpha (smooth rounded corners, clean anti-aliased text) plus an
// overall window opacity applied through the layered blend's constant alpha,
// with a single pixel buffer and no per-frame copy.
//
// Frame cost: everything that depends only on the config, the DPI and the font
// is computed when one of those changes, not per frame -- including where each
// block sits (PanelLayout.hpp). A steady-state frame resolves each city's local
// time, repaints only each city's clock line or face (the DIB still holds the
// rest of the previous frame), and presents only that area. The whole panel is
// redrawn after a config, DPI or surface change, and when a date line changes.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <string>
#include <vector>

#include "Config.hpp"
#include "PanelLayout.hpp"
#include "TimeEngine.hpp"

class Renderer {
public:
    // Takes the config up front so the fonts are measured and the panel laid
    // out once, for the config that will actually be shown.
    bool Init(HWND hwnd, const Config& cfg);
    void Shutdown();

    void SetConfig(const Config& cfg);  // recomputes layout
    void OnDpiChanged(UINT dpi);        // dpi = pixels per 96

    // Pixel size the window should be to fit the current config at this DPI.
    SIZE DesiredClientSize() const { return desired_; }

    // Render the current frame and present it via UpdateLayeredWindow.
    // Resizes the layered surface (and window) to DesiredClientSize().
    void Render(std::chrono::system_clock::time_point now);

#ifdef WORLDCLOCK_BENCH
    // Instrumentation for WorldClockBench only; the product exe is compiled
    // without it, so neither its code nor this class's layout changes.
    // QPC stamps taken through the last Render(): start, after UpdateFrame,
    // before BeginDraw, after EndDraw, a second stamp right after EndDraw
    // (where a full-surface CopyPixels used to end), after the present.
    // A frame that returned early leaves the later stamps at zero.
    struct BenchStamps { LONGLONG t[6]; };
    const BenchStamps& BenchLastFrame() const { return bench_; }
    // The DIB handed to UpdateLayeredWindow, i.e. exactly what is presented.
    const void* BenchPixels() const { return bits_; }
    SIZE BenchSurface() const { return surface_; }
    // Make the next Render() redraw the whole panel, as the first frame does;
    // the reference that incremental frames are compared against.
    void BenchForceFullRedraw() { fullRedraw_ = true; }
#endif

private:
#ifdef WORLDCLOCK_BENCH
    BenchStamps bench_{};
#endif
    Microsoft::WRL::ComPtr<ID2D1Factory>      d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory>    dwFactory_;
    Microsoft::WRL::ComPtr<IWICBitmap>        wicBitmap_;     // view of the DIB, per-size
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt_;            // WIC bitmap target

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtLabel_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtTime_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtDate_;

    // Fixed colours, so they live as long as the render target rather than
    // being created eleven times a frame.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brBg_, brLabel_, brTime_, brDate_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brAmFace_, brAmHand_, brAmTick_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brPmFace_, brPmHand_, brPmTick_;

    // Per-city state that only changes with the config, the DPI, or the drawn
    // string. Labels never change between reloads, dates change once a day,
    // and only the clock line is rebuilt every frame.
    struct CityVisual {
        std::wstring label;                  // UTF-16, converted once
        D2D1_SIZE_F  labelSize = { 0.f, 0.f };
        Microsoft::WRL::ComPtr<IDWriteTextLayout> labelLayout;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> dateLayout;
        // The day dateLayout was formatted from. Comparing fields rather than
        // formatted strings keeps the steady-state frame allocation-free.
        LocalTimeFields dateFields;
        // Whether the label's / date's ink reaches into the city's dirty rect,
        // and so has to be redrawn inside it on a steady-state frame.
        bool labelInDirty = true;
        bool dateInDirty = true;
    };
    std::vector<CityVisual>      visuals_;
    std::vector<D2D1_SIZE_F>     sizes_;   // block size per city (config-level)
    std::vector<ZoneClock>       clocks_;  // per city, caches its UTC offset
    std::vector<LocalTimeFields> lts_;     // per frame, reused between frames

    // Scaled lengths the draw calls use, derived from the DPI and the size
    // class in RecomputeLayout rather than re-derived every frame.
    struct Metrics {
        float pad = 0.f, lineGap = 0.f, cityGap = 0.f, corner = 0.f;
        float faceD = 0.f, tickStroke = 0.f, hub = 0.f;
        float hourHand = 0.f, minuteHand = 0.f, secondHand = 0.f;
    };
    Metrics m_;
    std::vector<CityGeometry> geom_;       // block positions + dirty rects
    RECT         dirtyUnion_ = { 0, 0, 0, 0 };   // what a steady frame presents
    // The next frame repaints and presents the whole panel rather than only
    // the dirty rects: nothing valid is in the DIB yet, or something outside
    // the dirty rects (layout, a date line) has changed.
    bool         fullRedraw_ = true;
    float        maxW_ = 0.f;              // widest block, for vertical layout
    std::wstring dateFormatW_;             // UTF-16 date pattern, converted once

    HWND    hwnd_   = nullptr;
    HDC     memDC_  = nullptr;     // memory DC holding the DIB
    HBITMAP dib_    = nullptr;     // top-down 32bpp BGRA
    HBITMAP oldBmp_ = nullptr;
    void*   bits_   = nullptr;
    SIZE    surface_ = { 0, 0 };   // current DIB size

    UINT    dpi_ = 96;
    Config  cfg_;
    SIZE    desired_ = { 240, 200 };
    // Widest digit glyph in the current font; measuring against it keeps the
    // panel from resizing every second as the digits change.
    wchar_t widestDigit_ = L'0';
    // Largest a time / date line could ever be with this config, DPI and font.
    // Sizing blocks from these rather than from the live string is what stops
    // the panel jumping at the 9 -> 10 hour and day rollovers.
    D2D1_SIZE_F worstTime_ = { 0.f, 0.f };
    D2D1_SIZE_F worstDate_ = { 0.f, 0.f };

    bool EnsureSurface(int w, int h);
    bool CreateDeviceResources();   // brushes, tied to the render target
    void ReleaseDeviceResources();
    bool CreateTextFormats();       // false when a font could not be created
    void RecomputeWorstCaseSizes();
    void RecomputeLayout();         // m_, block sizes, maxW_, desired_, clocks_, geom_
    void RebuildVisuals();          // labels + cached layouts, after a change
    void UpdateFrame(std::chrono::system_clock::time_point now);
    float Sc(float logical) const;  // logical px -> device px

    // Content size (DIP) of one city block in the current mode (digital text
    // stack, or analog face + date + label). Worst-case, so it does not change
    // as the clock ticks.
    D2D1_SIZE_F MeasureBlock(size_t index) const;

    // Width a block occupies in the current layout: its own in horizontal,
    // the shared widest in vertical (so left edges line up).
    float BlockWidth(size_t index) const;

    // The rounded panel background, over a transparent clear. Both respect an
    // axis-aligned clip, so this also restores the background under one city.
    void DrawBackground(ID2D1RenderTarget* rt);
    // One city: label, clock line or face, date. With `dirtyOnly`, the label
    // and date are skipped unless their ink reaches the city's dirty rect --
    // the caller has clipped to that rect, so they could not change a pixel.
    void DrawCity(ID2D1RenderTarget* rt, size_t index, bool dirtyOnly);
    void DrawCityDigital(ID2D1RenderTarget* rt, size_t index, bool dirtyOnly);
    // Analog faces switch between an AM (light) and PM (dark) palette per city
    // based on that city's local hour; the accent brush draws the name and the
    // second hand.
    void DrawCityAnalog(ID2D1RenderTarget* rt, size_t index, bool dirtyOnly);
};
