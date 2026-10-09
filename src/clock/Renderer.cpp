#include "Renderer.hpp"
#include "TextFormat.hpp"
#include "TimeEngine.hpp"

#include <d2d1helper.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace std::chrono;

// Per-phase timing for WorldClockBench. Expands to nothing in the product exe.
#ifdef WORLDCLOCK_BENCH
#define BENCH_STAMP(i)                                       \
    do {                                                     \
        LARGE_INTEGER qpc_;                                  \
        QueryPerformanceCounter(&qpc_);                      \
        bench_.t[i] = qpc_.QuadPart;                         \
    } while (0)
#else
#define BENCH_STAMP(i) ((void)0)
#endif

namespace {

std::wstring ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Size of a laid-out string. `width` accounts for glyph overhang (ink that
// spills past the advance width) so a rect built from it never clips the text.
D2D1_SIZE_F LayoutSize(IDWriteTextLayout* layout) {
    if (!layout) return { 0.f, 0.f };
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    float w = m.widthIncludingTrailingWhitespace;

    DWRITE_OVERHANG_METRICS o{};
    if (SUCCEEDED(layout->GetOverhangMetrics(&o)) && o.right > 0.f)
        w += o.right;   // right-side ink overhang (italics, wide glyph bearings)

    return { std::ceil(w), m.height };
}

// Measure a string with no intention of keeping the layout around.
D2D1_SIZE_F Measure(IDWriteFactory* dw, IDWriteTextFormat* fmt, const std::wstring& s) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, 4000.f, 4000.f, &layout)))
        return { 0, 0 };
    return LayoutSize(layout.Get());
}

} // namespace

float Renderer::Sc(float logical) const {
    return logical * (dpi_ / 96.0f) * (float)cfg_.scale();
}

bool Renderer::Init(HWND hwnd) {
    hwnd_ = hwnd;
    dpi_ = GetDpiForWindow(hwnd);
    if (dpi_ == 0) dpi_ = 96;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2dFactory_.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwFactory_.GetAddressOf()))))
        return false;

    // WIC factory. We render into a premultiplied-BGRA WIC bitmap (which keeps
    // its alpha channel, unlike a GDI DC target) and copy it into the DIB that
    // UpdateLayeredWindow presents.
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wicFactory_.GetAddressOf()))))
        return false;

    if (!CreateTextFormats()) return false;
    RecomputeLayout();
    RebuildVisuals();
    return true;
}

void Renderer::Shutdown() {
    visuals_.clear();
    sizes_.clear();
    lts_.clear();
    ReleaseDeviceResources();
    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    bits_ = nullptr;              // the DIB it pointed into is gone
    surface_ = { 0, 0 };
    fmtLabel_.Reset(); fmtTime_.Reset(); fmtDate_.Reset();
    rt_.Reset(); wicBitmap_.Reset(); wicFactory_.Reset();
    dwFactory_.Reset(); d2dFactory_.Reset();
}

void Renderer::SetConfig(const Config& cfg) {
    cfg_ = cfg;
    CreateTextFormats();
    RecomputeLayout();
    RebuildVisuals();
}

void Renderer::OnDpiChanged(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    CreateTextFormats();
    RecomputeLayout();
    RebuildVisuals();
}

bool Renderer::CreateTextFormats() {
    fmtLabel_.Reset(); fmtTime_.Reset(); fmtDate_.Reset();
    auto make = [&](float sizeDip, DWRITE_FONT_WEIGHT w, ComPtr<IDWriteTextFormat>& out) {
        return SUCCEEDED(dwFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"", &out));
    };
    // A null format would reach the draw calls, and in the analog path a text
    // alignment call, as a null dereference. Fail cleanly instead.
    if (!make(Sc(13.f), DWRITE_FONT_WEIGHT_SEMI_BOLD, fmtLabel_)) return false;
    if (!make(Sc(26.f), DWRITE_FONT_WEIGHT_LIGHT,     fmtTime_))  return false;
    if (!make(Sc(11.f), DWRITE_FONT_WEIGHT_NORMAL,    fmtDate_))  return false;

    // Find the widest digit once per format change (same family throughout, so
    // one probe covers every text style we draw).
    widestDigit_ = L'0';
    {
        float best = -1.f;
        for (wchar_t d = L'0'; d <= L'9'; ++d) {
            auto m = Measure(dwFactory_.Get(), fmtTime_.Get(), std::wstring(1, d));
            if (m.width > best) { best = m.width; widestDigit_ = d; }
        }
    }

    RecomputeWorstCaseSizes();
    return true;
}

// The size a time / date line could ever need, measured across every string
// the formatter could produce. Sizing from the live string instead would grow
// and shrink the panel as "9:59" becomes "10:00" or "Jun 9" becomes "Jun 10".
// Depends only on the config and the font, so it is computed here rather than
// per frame -- the date list runs to 84 candidates.
void Renderer::RecomputeWorstCaseSizes() {
    dateFormatW_ = ToW(cfg_.dateFormat);

    auto widest = [&](IDWriteTextFormat* fmt, const std::vector<std::wstring>& candidates) {
        D2D1_SIZE_F best{ 0.f, 0.f };
        for (const std::wstring& s : candidates) {
            auto m = Measure(dwFactory_.Get(), fmt, WidenDigits(s, widestDigit_));
            best.width  = (std::max)(best.width, m.width);
            best.height = (std::max)(best.height, m.height);
        }
        return best;
    };
    worstTime_ = widest(fmtTime_.Get(), MeasurementTimeCandidates(cfg_));
    worstDate_ = cfg_.showDate
               ? widest(fmtDate_.Get(), MeasurementDateCandidates(dateFormatW_))
               : D2D1_SIZE_F{ 0.f, 0.f };
}

// Content size of one city block (label/time/date for digital; face/date/label
// for analog). Heights/widths are in DIPs. Independent of the current time --
// that is what keeps the panel from resizing as the clock ticks over.
D2D1_SIZE_F Renderer::MeasureBlock(size_t index) const {
    const float lineGap = Sc(2.f);
    const D2D1_SIZE_F ml = visuals_[index].labelSize;

    if (cfg_.displayMode == DisplayMode::Analog) {
        const float faceD = Sc(80.f);
        float w = faceD, h = faceD;
        if (cfg_.showDate) {
            w = (std::max)(w, worstDate_.width); h += lineGap + worstDate_.height;
        }
        w = (std::max)(w, ml.width); h += lineGap + ml.height;
        return { w, h };
    }
    // Digital
    float w = (std::max)(ml.width, worstTime_.width);
    float h = ml.height + lineGap + worstTime_.height;
    if (cfg_.showDate) {
        w = (std::max)(w, worstDate_.width); h += lineGap + worstDate_.height;
    }
    return { w, h };
}

float Renderer::BlockWidth(size_t index) const {
    return cfg_.layout == LayoutDir::Horizontal ? sizes_[index].width : maxW_;
}

void Renderer::RecomputeLayout() {
    if (!dwFactory_) return;
    const float pad = Sc(14.f);
    const float cityGap = Sc(12.f);

    // Label metrics feed MeasureBlock, so they have to exist before it runs.
    // Labels only change on a config reload, so this is the only place the
    // UTF-8 to UTF-16 conversion and the measurement happen.
    const size_t n = cfg_.cities.size();
    visuals_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        visuals_[i].label = ToW(cfg_.cities[i].label);
        visuals_[i].labelSize = Measure(dwFactory_.Get(), fmtLabel_.Get(), visuals_[i].label);
    }

    sizes_.assign(n, D2D1_SIZE_F{ 0.f, 0.f });
    lts_.assign(n, LocalTimeFields{});
    maxW_ = 0.f;

    if (n == 0) {
        desired_.cx = (LONG)std::ceil(Sc(120.f) + 2 * pad);
        desired_.cy = (LONG)std::ceil(Sc(40.f) + 2 * pad);
        return;
    }

    const bool horiz = cfg_.layout == LayoutDir::Horizontal;
    float along = 0.f;   // sum of sizes along the stacking axis (+ gaps)
    float cross = 0.f;   // max size on the other axis
    for (size_t i = 0; i < n; ++i) {
        sizes_[i] = MeasureBlock(i);
        maxW_ = (std::max)(maxW_, sizes_[i].width);
        float a = horiz ? sizes_[i].width : sizes_[i].height;   // along
        float b = horiz ? sizes_[i].height : sizes_[i].width;   // across
        along += a;
        if (i + 1 < n) along += cityGap;
        cross = (std::max)(cross, b);
    }
    float w = horiz ? along : cross;
    float h = horiz ? cross : along;
    desired_.cx = (LONG)std::ceil(w + 2 * pad);
    desired_.cy = (LONG)std::ceil(h + 2 * pad);
}

// Build the text layouts that outlive a frame. The label never changes between
// config reloads; the date is rebuilt in UpdateFrame only when its string does.
// Alignment lives on the layout rather than on the shared format, so the analog
// path no longer flips the format to centred and back on every frame.
void Renderer::RebuildVisuals() {
    if (!dwFactory_) return;
    const bool analog = cfg_.displayMode == DisplayMode::Analog;
    const DWRITE_TEXT_ALIGNMENT align =
        analog ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING;

    for (size_t i = 0; i < visuals_.size(); ++i) {
        CityVisual& v = visuals_[i];
        const float blockW = BlockWidth(i);

        v.labelLayout.Reset();
        dwFactory_->CreateTextLayout(v.label.c_str(), (UINT32)v.label.size(),
                                     fmtLabel_.Get(), blockW, v.labelSize.height,
                                     &v.labelLayout);
        if (v.labelLayout) v.labelLayout->SetTextAlignment(align);

        // Force the date layout to be rebuilt against the new width/alignment.
        v.dateLayout.Reset();
        v.dateText.clear();
    }
}

bool Renderer::CreateDeviceResources() {
    if (!rt_) return false;
    auto make = [&](UINT32 rgb, float a, ComPtr<ID2D1SolidColorBrush>& out) {
        out.Reset();
        return SUCCEEDED(rt_->CreateSolidColorBrush(D2D1::ColorF(rgb, a), &out));
    };
    // Panel background: dark, ~0.86 alpha (overall opacity applied at present).
    if (!make(0x14161C, 0.86f, brBg_))    return false;
    if (!make(0x6FB6FF, 1.00f, brLabel_)) return false;
    if (!make(0xF2F4F8, 1.00f, brTime_))  return false;
    if (!make(0x9AA3B2, 1.00f, brDate_))  return false;
    // Two analog palettes: AM = light face with dark hands, PM = the reverse.
    if (!make(0xF5F7FA, 0.95f, brAmFace_)) return false;  // near-white
    if (!make(0x14161C, 1.00f, brAmHand_)) return false;  // dark hands
    if (!make(0x8A93A2, 1.00f, brAmTick_)) return false;  // gray ticks
    if (!make(0x20242E, 0.95f, brPmFace_)) return false;  // dark
    if (!make(0xF2F4F8, 1.00f, brPmHand_)) return false;  // light hands
    if (!make(0x4A5160, 1.00f, brPmTick_)) return false;  // dim ticks
    return true;
}

void Renderer::ReleaseDeviceResources() {
    brBg_.Reset(); brLabel_.Reset(); brTime_.Reset(); brDate_.Reset();
    brAmFace_.Reset(); brAmHand_.Reset(); brAmTick_.Reset();
    brPmFace_.Reset(); brPmHand_.Reset(); brPmTick_.Reset();
}

bool Renderer::EnsureSurface(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (memDC_ && surface_.cx == w && surface_.cy == h) return true;

    if (memDC_) {
        if (oldBmp_) { SelectObject(memDC_, oldBmp_); oldBmp_ = nullptr; }
        DeleteDC(memDC_); memDC_ = nullptr;
    }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    bits_ = nullptr;

    HDC screen = GetDC(nullptr);
    memDC_ = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!memDC_) return false;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;       // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dib_ = CreateDIBSection(memDC_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!dib_) { DeleteDC(memDC_); memDC_ = nullptr; return false; }
    oldBmp_ = (HBITMAP)SelectObject(memDC_, dib_);

    // WIC bitmap (premultiplied BGRA) + matching D2D render target. The brushes
    // belong to the render target, so they go with it.
    ReleaseDeviceResources();
    wicBitmap_.Reset(); rt_.Reset();
    if (FAILED(wicFactory_->CreateBitmap((UINT)w, (UINT)h, GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapCacheOnLoad, wicBitmap_.GetAddressOf())))
        return false;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f, 96.f); // DPI scaling applied manually via Sc()
    if (FAILED(d2dFactory_->CreateWicBitmapRenderTarget(wicBitmap_.Get(), props,
                                                        rt_.ReleaseAndGetAddressOf())))
        return false;
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // ClearType needs opaque bg

    if (!CreateDeviceResources()) return false;

    surface_ = { w, h };
    return true;
}

// Resolve each city's local time, and rebuild only the text that changed.
void Renderer::UpdateFrame(system_clock::time_point now) {
    const bool analog = cfg_.displayMode == DisplayMode::Analog;
    const DWRITE_TEXT_ALIGNMENT align =
        analog ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING;

    for (size_t i = 0; i < cfg_.cities.size(); ++i) {
        lts_[i] = ComputeLocal(cfg_.cities[i].tz, now);
        if (!cfg_.showDate) continue;

        CityVisual& v = visuals_[i];
        std::wstring text = FormatDatePattern(lts_[i], dateFormatW_);
        if (v.dateLayout && text == v.dateText) continue;   // same day, reuse

        v.dateText = std::move(text);
        v.dateLayout.Reset();
        dwFactory_->CreateTextLayout(v.dateText.c_str(), (UINT32)v.dateText.size(),
                                     fmtDate_.Get(), BlockWidth(i), worstDate_.height,
                                     &v.dateLayout);
        if (v.dateLayout) v.dateLayout->SetTextAlignment(align);
    }
}

void Renderer::Render(system_clock::time_point now) {
    // Block sizes are worst-case and depend only on the config, DPI and font,
    // so the layout is already current; recomputing per frame would only
    // re-derive the same numbers.
#ifdef WORLDCLOCK_BENCH
    bench_ = {};
#endif
    BENCH_STAMP(0);
    UpdateFrame(now);
    BENCH_STAMP(1);

    int w = desired_.cx, h = desired_.cy;
    if (!EnsureSurface(w, h) || !rt_) return;

    BENCH_STAMP(2);
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(D2D1::ColorF(0, 0.f)); // fully transparent

    D2D1_RECT_F panel = D2D1::RectF(0.f, 0.f, float(w), float(h));
    float radius = Sc(10.f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(panel, radius, radius), brBg_.Get());

    if (cfg_.displayMode == DisplayMode::Analog)
        DrawAnalog(rt_.Get());
    else
        DrawDigital(rt_.Get());

    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        surface_ = { 0, 0 };
        return;
    }
    BENCH_STAMP(3);

    // Copy the WIC pixels (premultiplied BGRA, alpha preserved) into the DIB
    // backing the layered window. A GDI DC render target would drop the alpha
    // here, which is why we go through WIC.
    {
        WICRect rc = { 0, 0, w, h };
        UINT stride = (UINT)w * 4;
        UINT bufSize = stride * (UINT)h;
        if (FAILED(wicBitmap_->CopyPixels(&rc, stride, bufSize,
                                          static_cast<BYTE*>(bits_))))
            return;
    }
    BENCH_STAMP(4);

    // Present via UpdateLayeredWindow: keep current position, set size, and
    // apply overall opacity through the constant source alpha.
    HDC screen = GetDC(nullptr);
    RECT wr{}; GetWindowRect(hwnd_, &wr);
    POINT dst = { wr.left, wr.top };
    SIZE size = { w, h };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = (BYTE)(cfg_.opacity * 255 / 100);
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd_, screen, &dst, &size, memDC_, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    BENCH_STAMP(5);
}

void Renderer::DrawDigital(ID2D1RenderTarget* rt) {
    const float pad = Sc(14.f);
    const float lineGap = Sc(2.f);
    const float cityGap = Sc(12.f);
    const bool horiz = cfg_.layout == LayoutDir::Horizontal;

    float x = pad, y = pad;
    for (size_t i = 0; i < cfg_.cities.size(); ++i) {
        const CityVisual& v = visuals_[i];
        float blockW = BlockWidth(i);
        float left = horiz ? x : pad;
        float yy = y;

        if (v.labelLayout)
            rt->DrawTextLayout(D2D1::Point2F(left, yy), v.labelLayout.Get(), brLabel_.Get());
        yy += v.labelSize.height + lineGap;

        // The clock line changes every frame by construction, so there is
        // nothing to cache for it.
        std::wstring timeStr = FormatTime(lts_[i], cfg_);
        rt->DrawTextW(timeStr.c_str(), (UINT32)timeStr.size(), fmtTime_.Get(),
                      D2D1::RectF(left, yy, left + blockW, yy + worstTime_.height),
                      brTime_.Get());
        yy += worstTime_.height;

        if (cfg_.showDate && v.dateLayout) {
            yy += lineGap;
            rt->DrawTextLayout(D2D1::Point2F(left, yy), v.dateLayout.Get(), brDate_.Get());
        }

        if (horiz) x += blockW + cityGap;
        else       y += sizes_[i].height + cityGap;
    }
}

void Renderer::DrawAnalog(ID2D1RenderTarget* rt) {
    const float pad = Sc(14.f);
    const float lineGap = Sc(2.f);
    const float cityGap = Sc(12.f);
    const float faceD = Sc(80.f);
    const float r = faceD / 2.f;
    const bool horiz = cfg_.layout == LayoutDir::Horizontal;

    float x = pad, y = pad;
    for (size_t i = 0; i < cfg_.cities.size(); ++i) {
        const CityVisual& v = visuals_[i];
        const LocalTimeFields& lt = lts_[i];
        float blockW = BlockWidth(i);
        float left = horiz ? x : pad;
        float cx = left + blockW / 2.f;
        float ccy = y + r;

        // AM = light face with dark hands, PM = dark face with light hands,
        // picked per city from its own local hour (0-11 = AM).
        bool am = lt.valid && lt.hour < 12;
        ID2D1SolidColorBrush* face  = am ? brAmFace_.Get() : brPmFace_.Get();
        ID2D1SolidColorBrush* hands = am ? brAmHand_.Get() : brPmHand_.Get();
        ID2D1SolidColorBrush* tick  = am ? brAmTick_.Get() : brPmTick_.Get();

        D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, ccy), r, r);
        rt->FillEllipse(e, face);
        rt->DrawEllipse(e, tick, Sc(1.5f));

        for (int t = 0; t < 12; ++t) {
            float a = float(t) * 3.14159265f / 6.f;
            float s = std::sin(a), c = std::cos(a);
            rt->DrawLine(D2D1::Point2F(cx + s * (r * 0.82f), ccy - c * (r * 0.82f)),
                         D2D1::Point2F(cx + s * (r * 0.95f), ccy - c * (r * 0.95f)),
                         tick, Sc(1.5f));
        }

        if (lt.valid) {
            float sec = float(lt.second);
            float min = float(lt.minute) + sec / 60.f;
            float hr  = float(lt.hour % 12) + min / 60.f;
            auto hand = [&](float frac, float len, float width, ID2D1SolidColorBrush* b) {
                float a = frac * 2.f * 3.14159265f;
                rt->DrawLine(D2D1::Point2F(cx, ccy),
                             D2D1::Point2F(cx + std::sin(a) * len, ccy - std::cos(a) * len),
                             b, width);
            };
            hand(hr / 12.f,  r * 0.5f,  Sc(3.0f), hands);
            hand(min / 60.f, r * 0.75f, Sc(2.0f), hands);
            if (cfg_.showSeconds) hand(sec / 60.f, r * 0.85f, Sc(1.0f), brLabel_.Get());
        }
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, ccy), Sc(2.5f), Sc(2.5f)), hands);

        // Date (below the face), then city label (below the date). Both are
        // centred by their own layouts.
        float yy = y + faceD;
        if (cfg_.showDate && v.dateLayout) {
            yy += lineGap;
            rt->DrawTextLayout(D2D1::Point2F(left, yy), v.dateLayout.Get(), brDate_.Get());
            yy += worstDate_.height;
        }
        yy += lineGap;
        if (v.labelLayout)
            rt->DrawTextLayout(D2D1::Point2F(left, yy), v.labelLayout.Get(), brLabel_.Get());

        if (horiz) x += blockW + cityGap;
        else       y += sizes_[i].height + cityGap;
    }
}
