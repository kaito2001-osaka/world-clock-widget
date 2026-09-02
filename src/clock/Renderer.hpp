// Direct2D / DirectWrite renderer for the gadget panel.
//
// Rendering model: we render into a 32-bit premultiplied-alpha DIB via an
// ID2D1DCRenderTarget, then present with UpdateLayeredWindow. This gives true
// per-pixel alpha (smooth rounded corners, clean anti-aliased text) plus an
// overall window opacity applied through the layered blend's constant alpha.
//
// Frame cost: everything that depends only on the config, the DPI and the font
// is computed when one of those changes, not per frame. A steady-state frame
// resolves each city's local time, refreshes only the text whose string
// actually changed, draws, and presents.
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
#include "TimeEngine.hpp"

class Renderer {
public:
    bool Init(HWND hwnd);
    void Shutdown();

    void SetConfig(const Config& cfg);  // recomputes layout
    void OnDpiChanged(UINT dpi);        // dpi = pixels per 96

    // Pixel size the window should be to fit the current config at this DPI.
    SIZE DesiredClientSize() const { return desired_; }

    // Render the current frame and present it via UpdateLayeredWindow.
    // Resizes the layered surface (and window) to DesiredClientSize().
    void Render(std::chrono::system_clock::time_point now);

private:
    Microsoft::WRL::ComPtr<ID2D1Factory>      d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory>    dwFactory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
    Microsoft::WRL::ComPtr<IWICBitmap>        wicBitmap_;     // PBGRA, per-size
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
        std::wstring dateText;               // what dateLayout currently holds
    };
    std::vector<CityVisual>      visuals_;
    std::vector<D2D1_SIZE_F>     sizes_;   // block size per city (config-level)
    std::vector<LocalTimeFields> lts_;     // per frame, reused between frames
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
    void RecomputeLayout();         // block sizes, maxW_, desired_
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

    void DrawDigital(ID2D1RenderTarget* rt);
    // Analog faces switch between an AM (light) and PM (dark) palette per city
    // based on that city's local hour; the accent brush draws the name and the
    // second hand.
    void DrawAnalog(ID2D1RenderTarget* rt);
};
