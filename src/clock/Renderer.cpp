#include "Renderer.hpp"
#include "TextFormat.hpp"
#include "TimeEngine.hpp"

#include <d2d1helper.h>
#include <d3d11.h>
#include <array>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwrite.lib")

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

// Whether a layout drawn at (x, y) can touch a pixel of `dirty`. Assumes it
// can when the ink extent is unknown, which only costs a redundant draw.
bool LayoutInkReaches(IDWriteTextLayout* layout, const RECT& dirty, float x, float y) {
    if (!layout) return false;   // nothing is drawn
    DWRITE_OVERHANG_METRICS o{};
    if (FAILED(layout->GetOverhangMetrics(&o))) return true;
    return InkReaches(dirty, x, y, layout->GetMaxWidth(), layout->GetMaxHeight(),
                      InkOverhang{ o.left, o.top, o.right, o.bottom });
}

// Unit vectors to the twelve hour marks, clockwise from 12 o'clock. The same
// on every face and every frame, so computed once.
struct TickDir { float s, c; };
const std::array<TickDir, 12>& TickDirections() {
    static const std::array<TickDir, 12> dirs = [] {
        std::array<TickDir, 12> d{};
        for (int t = 0; t < 12; ++t) {
            float a = float(t) * 3.14159265f / 6.f;
            d[t] = { std::sin(a), std::cos(a) };
        }
        return d;
    }();
    return dirs;
}

} // namespace

float Renderer::Sc(float logical) const {
    return logical * (dpi_ / 96.0f) * (float)cfg_.scale();
}

bool Renderer::Init(HWND hwnd, const Config& cfg) {
    hwnd_ = hwnd;
    cfg_ = cfg;
    dpi_ = GetDpiForWindow(hwnd);
    if (dpi_ == 0) dpi_ = 96;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2dFactory_.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwFactory_.GetAddressOf()))))
        return false;
    if (!CreateDevice()) return false;

    if (!CreateTextFormats()) return false;
    RecomputeLayout();
    RebuildVisuals();
    return true;
}

void Renderer::Shutdown() {
    visuals_.clear();
    sizes_.clear();
    lts_.clear();
    ReleaseDevice();
    fmtLabel_.Reset(); fmtTime_.Reset(); fmtDate_.Reset();
    dwFactory_.Reset(); d2dFactory_.Reset();
}

void Renderer::SetConfig(const Config& cfg) {
    cfg_ = cfg;
    CreateTextFormats();
    RecomputeLayout();
    RebuildVisuals();
    fullRedraw_ = true;
}

void Renderer::OnDpiChanged(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    CreateTextFormats();
    RecomputeLayout();
    RebuildVisuals();
    fullRedraw_ = true;
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
    // one probe covers every text style we draw). Compared on the raw advance:
    // Measure() rounds up to whole pixels, which hides the fraction of a pixel
    // that makes '4' the widest.
    {
        float advances[10] = {};
        for (int d = 0; d < 10; ++d) {
            const wchar_t ch = wchar_t(L'0' + d);
            ComPtr<IDWriteTextLayout> layout;
            DWRITE_TEXT_METRICS m{};
            if (SUCCEEDED(dwFactory_->CreateTextLayout(&ch, 1, fmtTime_.Get(), 4000.f, 4000.f,
                                                       &layout)))
                layout->GetMetrics(&m);
            advances[d] = m.widthIncludingTrailingWhitespace;
        }
        widestDigit_ = WidestDigit(advances);
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
    const float lineGap = m_.lineGap;
    const D2D1_SIZE_F ml = visuals_[index].labelSize;

    if (cfg_.displayMode == DisplayMode::Analog) {
        const float faceD = m_.faceD;
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
    m_.pad        = Sc(14.f);
    m_.lineGap    = Sc(2.f);
    m_.cityGap    = Sc(12.f);
    m_.corner     = Sc(10.f);
    m_.faceD      = Sc(80.f);
    m_.tickStroke = Sc(1.5f);
    m_.hub        = Sc(2.5f);
    m_.hourHand   = Sc(3.0f);
    m_.minuteHand = Sc(2.0f);
    m_.secondHand = Sc(1.0f);
    const float pad = m_.pad;
    const float cityGap = m_.cityGap;

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
    clocks_.clear();
    clocks_.reserve(n);
    for (const CityEntry& c : cfg_.cities) clocks_.emplace_back(c.tz);
    maxW_ = 0.f;
    geom_.clear();
    dirtyUnion_ = RECT{ 0, 0, 0, 0 };

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

    PanelLayoutInput in;
    in.analog     = cfg_.displayMode == DisplayMode::Analog;
    in.horizontal = horiz;
    in.showDate   = cfg_.showDate;
    in.pad        = pad;
    in.lineGap    = m_.lineGap;
    in.cityGap    = cityGap;
    in.faceD      = m_.faceD;
    in.corner     = m_.corner;
    in.maxW       = maxW_;
    in.timeH      = worstTime_.height;
    in.dateH      = worstDate_.height;
    in.panel      = desired_;
    for (size_t i = 0; i < n; ++i) {
        in.blockW.push_back(sizes_[i].width);
        in.blockH.push_back(sizes_[i].height);
        in.labelH.push_back(visuals_[i].labelSize.height);
    }
    geom_ = ComputePanelLayout(in);
    dirtyUnion_ = DirtyUnion(geom_);
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
        v.labelInDirty = LayoutInkReaches(v.labelLayout.Get(), geom_[i].dirty,
                                          geom_[i].left, geom_[i].labelY);

        // Force the date layout to be rebuilt against the new width/alignment.
        v.dateLayout.Reset();
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

// The WIC bitmap render target that drew the panel before runs on a WARP
// device of Direct2D's own, and WARP rasterizes on a pool of worker threads.
// Those stayed resident: ten or so, for a panel drawn once a second. On a
// device of our own they can be turned off. Every frame is then rasterized on
// this thread: it takes longer from start to end, but less CPU in all, and the
// threads and their memory are gone (WorldClockBench's cycles column; #22).
bool Renderer::CreateDevice() {
    ComPtr<ID3D11Device> d3d;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT |   // required by Direct2D
                                 D3D11_CREATE_DEVICE_SINGLETHREADED |
                                 D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
                                 nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr)))
        return false;

    ComPtr<IDXGIDevice> dxgi;
    ComPtr<ID2D1Device> device;
    if (FAILED(d3d.As(&dxgi)) ||
        FAILED(d2dFactory_->CreateDevice(dxgi.Get(), &device)) ||
        FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rt_)) ||
        FAILED(rt_.As(&gdi_))) {
        ReleaseDevice();
        return false;
    }
    rt_->SetDpi(96.f, 96.f);   // DPI scaling applied manually via Sc()
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // ClearType needs opaque bg

    if (!CreateDeviceResources()) {
        ReleaseDevice();
        return false;
    }
    return true;
}

void Renderer::ReleaseDevice() {
    ReleaseDeviceResources();
    target_.Reset();
    gdi_.Reset();
    rt_.Reset();
    surface_ = { 0, 0 };
}

bool Renderer::EnsureSurface(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    // Render releases the device when it is lost; start over with a new one.
    if (!rt_ && !CreateDevice()) return false;
    if (target_ && surface_.cx == w && surface_.cy == h) return true;

    rt_->SetTarget(nullptr);
    target_.Reset();
    surface_ = { 0, 0 };

    // GDI compatible, so that UpdateLayeredWindow can read the frame through
    // the bitmap's DC; a DC render target would drop the alpha instead.
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f, 96.f); // DPI scaling applied manually via Sc()
    if (FAILED(rt_->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h), nullptr, 0, props,
                                 &target_)))
        return false;
    rt_->SetTarget(target_.Get());

    surface_ = { w, h };
    fullRedraw_ = true;   // a new bitmap holds no previous frame to build on
    return true;
}

// Resolve each city's local time, and rebuild only the text that changed.
// Nothing here allocates unless the day changed or an offset period ended.
void Renderer::UpdateFrame(system_clock::time_point now) {
    const bool analog = cfg_.displayMode == DisplayMode::Analog;
    const DWRITE_TEXT_ALIGNMENT align =
        analog ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING;

    for (size_t i = 0; i < cfg_.cities.size(); ++i) {
        lts_[i] = clocks_[i].At(now);
        if (!cfg_.showDate) continue;

        CityVisual& v = visuals_[i];
        if (v.dateLayout && SameDate(lts_[i], v.dateFields)) continue;   // same day, reuse

        // The date line lies outside the city's dirty rect, so a new one
        // needs the whole panel repainted. Once a day per city at most.
        fullRedraw_ = true;
        v.dateFields = lts_[i];
        const std::wstring text = FormatDatePattern(lts_[i], dateFormatW_);
        v.dateLayout.Reset();
        dwFactory_->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                     fmtDate_.Get(), BlockWidth(i), worstDate_.height,
                                     &v.dateLayout);
        if (v.dateLayout) v.dateLayout->SetTextAlignment(align);
        v.dateInDirty = LayoutInkReaches(v.dateLayout.Get(), geom_[i].dirty,
                                         geom_[i].left, geom_[i].dateY);
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

    // Read after UpdateFrame and EnsureSurface, either of which can ask for it.
    const bool full = fullRedraw_;
    fullRedraw_ = false;

    BENCH_STAMP(2);
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());

    if (full) {
        DrawBackground(rt_.Get());
        for (size_t i = 0; i < geom_.size(); ++i) DrawCity(rt_.Get(), i, false);
    } else {
        // Everything outside the dirty rects is still in the bitmap from the
        // previous frame. Inside each one, repaint exactly what a full redraw
        // would put there: the background, the clock line or face, and the
        // label or date only where their ink reaches in. The clip is aliased
        // and pixel-aligned, so the pixels it lets through are computed
        // exactly as in a full redraw.
        for (size_t i = 0; i < geom_.size(); ++i) {
            const CityGeometry& g = geom_[i];
            const RECT& d = g.dirty;
            if (d.right <= d.left || d.bottom <= d.top) continue;
            rt_->PushAxisAlignedClip(D2D1::RectF(float(d.left), float(d.top),
                                                 float(d.right), float(d.bottom)),
                                     D2D1_ANTIALIAS_MODE_ALIASED);
            // Away from the rounded corners the background is one flat
            // colour, and a clear is far cheaper than the rounded fill.
            if (g.dirtyAvoidsCorners) rt_->Clear(brBg_->GetColor());
            else                      DrawBackground(rt_.Get());
            DrawCity(rt_.Get(), i, true);
            rt_->PopAxisAlignedClip();
        }
    }

    // Rasterize now, so that a frame that failed to draw is never presented.
    HRESULT hr = rt_->Flush();
    // The bitmap as a GDI DC, which is what UpdateLayeredWindow reads. It is
    // only valid inside BeginDraw / EndDraw, so the frame is presented here.
    HDC frame = nullptr;
    if (SUCCEEDED(hr)) hr = gdi_->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &frame);
    BENCH_STAMP(3);
    // Nothing is copied out of the bitmap. Stamp 4 used to close a
    // full-surface CopyPixels; it stays so the bench table keeps its columns.
    BENCH_STAMP(4);
    if (SUCCEEDED(hr)) {
        // If the window missed this update, the next frame has to resend it all.
        if (!Present(frame, SIZE{ w, h }, full)) fullRedraw_ = true;
        // GDI only read from the DC, so nothing in it has to go back into the
        // bitmap: the area it changed is empty.
        RECT unchanged = { 0, 0, 0, 0 };
        gdi_->ReleaseDC(&unchanged);
    }

    const HRESULT endHr = rt_->EndDraw();
    if (FAILED(hr) || FAILED(endHr)) {
        // Whatever reached the bitmap is not a frame to build the next one on.
        fullRedraw_ = true;
        // The device is gone; the next frame creates a new one.
        if (hr == D2DERR_RECREATE_TARGET || endHr == D2DERR_RECREATE_TARGET) ReleaseDevice();
        return;
    }
    BENCH_STAMP(5);
}

// Present via UpdateLayeredWindowIndirect: keep current position, set size,
// apply overall opacity through the constant source alpha, and on a
// steady-state frame copy only the area that was repainted.
// Passing null for the screen DC and the position also keeps the window in
// place and skips these three calls, but WorldClockBench measured it as a
// net loss: the present got faster and the next analog draw slower by more.
bool Renderer::Present(HDC frame, SIZE size, bool full) {
    HDC screen = GetDC(nullptr);
    RECT wr{}; GetWindowRect(hwnd_, &wr);
    POINT dst = { wr.left, wr.top };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = (BYTE)(cfg_.opacity * 255 / 100);
    bf.AlphaFormat = AC_SRC_ALPHA;
    UPDATELAYEREDWINDOWINFO ulw{};
    ulw.cbSize   = sizeof(ulw);
    ulw.hdcDst   = screen;
    ulw.pptDst   = &dst;
    ulw.psize    = &size;
    ulw.hdcSrc   = frame;
    ulw.pptSrc   = &src;
    ulw.pblend   = &bf;
    ulw.dwFlags  = ULW_ALPHA;
    // A full frame may also be a new size, and a dirty rect is only accepted
    // when the size is unchanged; it also has to cover everything anyway.
    ulw.prcDirty = full ? nullptr : &dirtyUnion_;
    const bool ok = UpdateLayeredWindowIndirect(hwnd_, &ulw) != FALSE;
    ReleaseDC(nullptr, screen);
    return ok;
}

#ifdef WORLDCLOCK_BENCH
// Reads through GDI, as UpdateLayeredWindow does, rather than from the DC
// bitmap's bits: WARP pads a texture's rows (to 8 bytes; a 111 px row takes
// 448), and the DC bitmap does not report that pitch.
const void* Renderer::BenchPixels() {
    if (!rt_ || !target_) return nullptr;
    const int w = surface_.cx, h = surface_.cy;
    benchPixels_.resize((size_t)w * 4 * (size_t)h);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;       // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    bool ok = false;
    rt_->BeginDraw();
    HDC frame = nullptr;
    if (SUCCEEDED(gdi_->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &frame))) {
        void* bits = nullptr;
        HDC mem = CreateCompatibleDC(frame);
        HBITMAP dib = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)
                          : nullptr;
        if (dib) {
            HGDIOBJ old = SelectObject(mem, dib);
            // A 32-bit SRCCOPY between DIBs keeps the alpha byte as it is.
            ok = BitBlt(mem, 0, 0, w, h, frame, 0, 0, SRCCOPY) && GdiFlush();
            if (ok) std::memcpy(benchPixels_.data(), bits, benchPixels_.size());
            SelectObject(mem, old);
            DeleteObject(dib);
        }
        if (mem) DeleteDC(mem);
        RECT unchanged = { 0, 0, 0, 0 };
        gdi_->ReleaseDC(&unchanged);
    }
    if (FAILED(rt_->EndDraw())) ok = false;
    return ok ? benchPixels_.data() : nullptr;
}
#endif

void Renderer::DrawBackground(ID2D1RenderTarget* rt) {
    rt->Clear(D2D1::ColorF(0, 0.f)); // fully transparent
    D2D1_RECT_F panel = D2D1::RectF(0.f, 0.f, float(desired_.cx), float(desired_.cy));
    float radius = m_.corner;
    rt->FillRoundedRectangle(D2D1::RoundedRect(panel, radius, radius), brBg_.Get());
}

void Renderer::DrawCity(ID2D1RenderTarget* rt, size_t index, bool dirtyOnly) {
    if (cfg_.displayMode == DisplayMode::Analog)
        DrawCityAnalog(rt, index, dirtyOnly);
    else
        DrawCityDigital(rt, index, dirtyOnly);
}

void Renderer::DrawCityDigital(ID2D1RenderTarget* rt, size_t index, bool dirtyOnly) {
    const CityVisual& v = visuals_[index];
    const CityGeometry& g = geom_[index];

    if (v.labelLayout && (!dirtyOnly || v.labelInDirty))
        rt->DrawTextLayout(D2D1::Point2F(g.left, g.labelY), v.labelLayout.Get(), brLabel_.Get());

    // The clock line changes every frame by construction, so there is
    // nothing to cache for it. A stack buffer, because "12:59:59" is
    // already too long for std::wstring to hold without allocating.
    wchar_t timeStr[kFormatTimeCapacity];
    const size_t timeLen = FormatTimeTo(lts_[index], cfg_, timeStr, kFormatTimeCapacity);
    rt->DrawTextW(timeStr, (UINT32)timeLen, fmtTime_.Get(),
                  D2D1::RectF(g.left, g.timeY, g.left + g.width, g.timeY + worstTime_.height),
                  brTime_.Get());

    if (cfg_.showDate && v.dateLayout && (!dirtyOnly || v.dateInDirty))
        rt->DrawTextLayout(D2D1::Point2F(g.left, g.dateY), v.dateLayout.Get(), brDate_.Get());
}

void Renderer::DrawCityAnalog(ID2D1RenderTarget* rt, size_t index, bool dirtyOnly) {
    const CityVisual& v = visuals_[index];
    const CityGeometry& g = geom_[index];
    const LocalTimeFields& lt = lts_[index];
    const float r = g.faceR;
    const float cx = g.faceCx;
    const float ccy = g.faceCy;

    // AM = light face with dark hands, PM = dark face with light hands,
    // picked per city from its own local hour (0-11 = AM). The palette only
    // colours the face, so a flip stays inside the city's dirty rect.
    bool am = lt.valid && lt.hour < 12;
    ID2D1SolidColorBrush* face  = am ? brAmFace_.Get() : brPmFace_.Get();
    ID2D1SolidColorBrush* hands = am ? brAmHand_.Get() : brPmHand_.Get();
    ID2D1SolidColorBrush* tick  = am ? brAmTick_.Get() : brPmTick_.Get();

    D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, ccy), r, r);
    rt->FillEllipse(e, face);
    rt->DrawEllipse(e, tick, m_.tickStroke);

    for (const TickDir& d : TickDirections()) {
        rt->DrawLine(D2D1::Point2F(cx + d.s * (r * 0.82f), ccy - d.c * (r * 0.82f)),
                     D2D1::Point2F(cx + d.s * (r * 0.95f), ccy - d.c * (r * 0.95f)),
                     tick, m_.tickStroke);
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
        hand(hr / 12.f,  r * 0.5f,  m_.hourHand, hands);
        hand(min / 60.f, r * 0.75f, m_.minuteHand, hands);
        if (cfg_.showSeconds) hand(sec / 60.f, r * 0.85f, m_.secondHand, brLabel_.Get());
    }
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, ccy), m_.hub, m_.hub), hands);

    // Date (below the face), then city label (below the date). Both are
    // centred by their own layouts.
    if (cfg_.showDate && v.dateLayout && (!dirtyOnly || v.dateInDirty))
        rt->DrawTextLayout(D2D1::Point2F(g.left, g.dateY), v.dateLayout.Get(), brDate_.Get());
    if (v.labelLayout && (!dirtyOnly || v.labelInDirty))
        rt->DrawTextLayout(D2D1::Point2F(g.left, g.labelY), v.labelLayout.Get(), brLabel_.Get());
}
