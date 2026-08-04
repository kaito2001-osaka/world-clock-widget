// Direct2D / DirectWrite renderer for the gadget panel.
//
// Rendering model: we render into a 32-bit premultiplied-alpha DIB via an
// ID2D1DCRenderTarget, then present with UpdateLayeredWindow. This gives true
// per-pixel alpha (smooth rounded corners, clean anti-aliased text) plus an
// overall window opacity applied through the layered blend's constant alpha.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include "Config.hpp"

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

    bool EnsureSurface(int w, int h);
    void CreateTextFormats();
    void RecomputeLayout(std::chrono::system_clock::time_point now);
    float Sc(float logical) const;  // logical px -> device px

    // Content size (DIP) of one city block in the current mode (digital text
    // stack, or analog face + date + label). Used by both layout and drawing.
    D2D1_SIZE_F MeasureBlock(const CityEntry& c, const struct LocalTimeFields& lt);

    void DrawDigital(ID2D1RenderTarget* rt, std::chrono::system_clock::time_point now,
                     ID2D1SolidColorBrush* label, ID2D1SolidColorBrush* time,
                     ID2D1SolidColorBrush* date);
    // Analog faces switch between an AM (light) and PM (dark) palette per city
    // based on that city's local hour; `accent` is used for the name + second hand.
    void DrawAnalog(ID2D1RenderTarget* rt, std::chrono::system_clock::time_point now,
                    ID2D1SolidColorBrush* accent);
};
