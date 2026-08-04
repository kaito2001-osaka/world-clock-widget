#include "Renderer.hpp"
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

namespace {

const wchar_t* kWeekdayShort[7] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
const wchar_t* kWeekdayLong[7]  = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
                                    L"Thursday", L"Friday", L"Saturday" };
const wchar_t* kWeekdayJp[7]    = { L"日", L"月", L"火", L"水", L"木", L"金", L"土" };
const wchar_t* kMonthShort[13]  = { L"", L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                    L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
const wchar_t* kMonthLong[13]   = { L"", L"January", L"February", L"March", L"April", L"May",
                                    L"June", L"July", L"August", L"September", L"October",
                                    L"November", L"December" };

std::wstring ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring FormatTime(const LocalTimeFields& f, const Config& cfg) {
    if (!f.valid) return L"--:--";
    int h = f.hour;
    const wchar_t* ampm = L"";
    if (cfg.hourFormat == 12) {
        ampm = (h < 12) ? L" AM" : L" PM";
        h = h % 12; if (h == 0) h = 12;
    }
    wchar_t buf[32];
    if (cfg.showSeconds)
        swprintf(buf, 32, L"%d:%02d:%02d%s", h, f.minute, f.second, ampm);
    else
        swprintf(buf, 32, L"%d:%02d%s", h, f.minute, ampm);
    return buf;
}

// Format a date by a token pattern. Tokens (run-length sensitive, .NET-like):
//   yyyy/yy            year (4 / 2 digit)
//   MMMM/MMM/MM/M      month long name / short name / 2-digit / number
//   dddd/ddd/dd/d      weekday long / weekday short / 2-digit day / day number
//   aaaa/aaa..a        Japanese weekday: "日曜日" / "日"
// Any other character is emitted literally (so "年", "月", "/", "-", spaces pass through).
std::wstring FormatDatePattern(const LocalTimeFields& f, const std::wstring& pat) {
    if (!f.valid) return L"";
    std::wstring out;
    size_t i = 0, n = pat.size();
    wchar_t buf[16];
    auto run = [&](wchar_t ch) { size_t j = i; int c = 0; while (j < n && pat[j] == ch) { ++c; ++j; } return c; };
    unsigned mo = f.month <= 12 ? f.month : 0;
    unsigned wd = f.weekday % 7;
    while (i < n) {
        wchar_t ch = pat[i];
        switch (ch) {
            case L'y': { int r = run(L'y');
                if (r >= 4) swprintf(buf, 16, L"%04d", f.year);
                else        swprintf(buf, 16, L"%02d", ((f.year % 100) + 100) % 100);
                out += buf; i += r; break; }
            case L'M': { int r = run(L'M');
                if (r >= 4)      out += kMonthLong[mo];
                else if (r == 3) out += kMonthShort[mo];
                else { swprintf(buf, 16, r == 2 ? L"%02u" : L"%u", f.month); out += buf; }
                i += r; break; }
            case L'd': { int r = run(L'd');
                if (r >= 4)      out += kWeekdayLong[wd];
                else if (r == 3) out += kWeekdayShort[wd];
                else { swprintf(buf, 16, r == 2 ? L"%02u" : L"%u", f.day); out += buf; }
                i += r; break; }
            case L'a': { int r = run(L'a');
                out += kWeekdayJp[wd];
                if (r >= 4) out += L"曜日";
                i += r; break; }
            default: out += ch; ++i; break;
        }
    }
    return out;
}

// Substitute every digit with `wide` so a measurement reflects the worst-case
// width for that pattern. Digits in Segoe UI do not share an advance width, so
// measuring the live string would resize the panel every second.
std::wstring WidenDigits(std::wstring s, wchar_t wide) {
    for (auto& ch : s)
        if (ch >= L'0' && ch <= L'9') ch = wide;
    return s;
}

// Measure a string. `width` accounts for glyph overhang (ink that spills past
// the advance width) so a layout rect built from it never clips the text.
D2D1_SIZE_F Measure(IDWriteFactory* dw, IDWriteTextFormat* fmt, const std::wstring& s) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, 4000.f, 4000.f, &layout)))
        return { 0, 0 };
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    float w = m.widthIncludingTrailingWhitespace;

    DWRITE_OVERHANG_METRICS o{};
    if (SUCCEEDED(layout->GetOverhangMetrics(&o)) && o.right > 0.f)
        w += o.right;   // right-side ink overhang (italics, wide glyph bearings)

    return { std::ceil(w), m.height };
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

    CreateTextFormats();
    RecomputeLayout(system_clock::now());
    return true;
}

void Renderer::Shutdown() {
    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    fmtLabel_.Reset(); fmtTime_.Reset(); fmtDate_.Reset();
    rt_.Reset(); wicBitmap_.Reset(); wicFactory_.Reset();
    dwFactory_.Reset(); d2dFactory_.Reset();
}

void Renderer::SetConfig(const Config& cfg) {
    cfg_ = cfg;
    CreateTextFormats();
    RecomputeLayout(system_clock::now());
}

void Renderer::OnDpiChanged(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    CreateTextFormats();
    RecomputeLayout(system_clock::now());
}

void Renderer::CreateTextFormats() {
    fmtLabel_.Reset(); fmtTime_.Reset(); fmtDate_.Reset();
    auto make = [&](float sizeDip, DWRITE_FONT_WEIGHT w, ComPtr<IDWriteTextFormat>& out) {
        dwFactory_->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"", &out);
    };
    make(Sc(13.f), DWRITE_FONT_WEIGHT_SEMI_BOLD, fmtLabel_);
    make(Sc(26.f), DWRITE_FONT_WEIGHT_LIGHT,     fmtTime_);
    make(Sc(11.f), DWRITE_FONT_WEIGHT_NORMAL,    fmtDate_);

    // Find the widest digit once per format change (same family throughout, so
    // one probe covers every text style we draw).
    widestDigit_ = L'0';
    if (fmtTime_) {
        float best = -1.f;
        for (wchar_t d = L'0'; d <= L'9'; ++d) {
            auto m = Measure(dwFactory_.Get(), fmtTime_.Get(), std::wstring(1, d));
            if (m.width > best) { best = m.width; widestDigit_ = d; }
        }
    }
}

// Content size of one city block (label/time/date for digital; face/date/label
// for analog). Heights/widths are in DIPs.
D2D1_SIZE_F Renderer::MeasureBlock(const CityEntry& c, const LocalTimeFields& lt) {
    const float lineGap = Sc(2.f);
    if (cfg_.displayMode == DisplayMode::Analog) {
        const float faceD = Sc(80.f);
        float w = faceD, h = faceD;
        if (cfg_.showDate) {
            auto md = Measure(dwFactory_.Get(), fmtDate_.Get(),
                              WidenDigits(FormatDatePattern(lt, ToW(cfg_.dateFormat)), widestDigit_));
            w = (std::max)(w, md.width); h += lineGap + md.height;
        }
        auto ml = Measure(dwFactory_.Get(), fmtLabel_.Get(), ToW(c.label));
        w = (std::max)(w, ml.width); h += lineGap + ml.height;
        return { w, h };
    }
    // Digital
    auto mLabel = Measure(dwFactory_.Get(), fmtLabel_.Get(), ToW(c.label));
    auto mTime  = Measure(dwFactory_.Get(), fmtTime_.Get(),
                          WidenDigits(FormatTime(lt, cfg_), widestDigit_));
    float w = (std::max)(mLabel.width, mTime.width);
    float h = mLabel.height + lineGap + mTime.height;
    if (cfg_.showDate) {
        auto mDate = Measure(dwFactory_.Get(), fmtDate_.Get(),
                             WidenDigits(FormatDatePattern(lt, ToW(cfg_.dateFormat)), widestDigit_));
        w = (std::max)(w, mDate.width); h += lineGap + mDate.height;
    }
    return { w, h };
}

void Renderer::RecomputeLayout(system_clock::time_point now) {
    if (!dwFactory_) return;
    const float pad = Sc(14.f);
    const float cityGap = Sc(12.f);

    if (cfg_.cities.empty()) {
        desired_.cx = (LONG)std::ceil(Sc(120.f) + 2 * pad);
        desired_.cy = (LONG)std::ceil(Sc(40.f) + 2 * pad);
        return;
    }

    const bool horiz = cfg_.layout == LayoutDir::Horizontal;
    float along = 0.f;   // sum of sizes along the stacking axis (+ gaps)
    float cross = 0.f;   // max size on the other axis
    for (size_t i = 0; i < cfg_.cities.size(); ++i) {
        auto lt = ComputeLocal(cfg_.cities[i].tz, now);
        auto s = MeasureBlock(cfg_.cities[i], lt);
        float a = horiz ? s.width : s.height;   // along the stacking direction
        float b = horiz ? s.height : s.width;   // across
        along += a;
        if (i + 1 < cfg_.cities.size()) along += cityGap;
        cross = (std::max)(cross, b);
    }
    float w = horiz ? along : cross;
    float h = horiz ? cross : along;
    desired_.cx = (LONG)std::ceil(w + 2 * pad);
    desired_.cy = (LONG)std::ceil(h + 2 * pad);
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

    // WIC bitmap (premultiplied BGRA) + matching D2D render target.
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

    surface_ = { w, h };
    return true;
}

void Renderer::Render(system_clock::time_point now) {
    // Re-measure every frame against the instant we are about to draw. The
    // window is sized from desired_, so a stale value (e.g. computed when the
    // hour was still one digit) would clip the trailing column.
    RecomputeLayout(now);

    int w = desired_.cx, h = desired_.cy;
    if (!EnsureSurface(w, h) || !rt_) return;

    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(D2D1::ColorF(0, 0.f)); // fully transparent

    D2D1_RECT_F panel = D2D1::RectF(0.f, 0.f, float(w), float(h));

    ComPtr<ID2D1SolidColorBrush> bg, label, timeB, dateB;
    // Panel background: dark, ~0.86 alpha (overall opacity applied at present).
    rt_->CreateSolidColorBrush(D2D1::ColorF(0x14161C, 0.86f), &bg);
    rt_->CreateSolidColorBrush(D2D1::ColorF(0x6FB6FF, 1.0f), &label);
    rt_->CreateSolidColorBrush(D2D1::ColorF(0xF2F4F8, 1.0f), &timeB);
    rt_->CreateSolidColorBrush(D2D1::ColorF(0x9AA3B2, 1.0f), &dateB);

    float radius = Sc(10.f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(panel, radius, radius), bg.Get());

    if (cfg_.displayMode == DisplayMode::Analog)
        DrawAnalog(rt_.Get(), now, label.Get());
    else
        DrawDigital(rt_.Get(), now, label.Get(), timeB.Get(), dateB.Get());

    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        surface_ = { 0, 0 };
        return;
    }

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
}

void Renderer::DrawDigital(ID2D1RenderTarget* rt, system_clock::time_point now,
                           ID2D1SolidColorBrush* label, ID2D1SolidColorBrush* timeB,
                           ID2D1SolidColorBrush* dateB) {
    const float pad = Sc(14.f);
    const float lineGap = Sc(2.f);
    const float cityGap = Sc(12.f);
    const bool horiz = cfg_.layout == LayoutDir::Horizontal;

    // Pre-measure so vertical columns can share one width (left edges aligned).
    std::vector<LocalTimeFields> lts;
    std::vector<D2D1_SIZE_F> sizes;
    float maxW = 0.f;
    for (auto& c : cfg_.cities) {
        auto lt = ComputeLocal(c.tz, now);
        auto s = MeasureBlock(c, lt);
        lts.push_back(lt); sizes.push_back(s);
        maxW = (std::max)(maxW, s.width);
    }

    float x = pad, y = pad;
    for (size_t i = 0; i < cfg_.cities.size(); ++i) {
        const auto& lt = lts[i];
        float blockW = horiz ? sizes[i].width : maxW;
        float left = horiz ? x : pad;
        float right = left + blockW;
        float yy = y;

        std::wstring labelStr = ToW(cfg_.cities[i].label);
        auto mLabel = Measure(dwFactory_.Get(), fmtLabel_.Get(), labelStr);
        rt->DrawTextW(labelStr.c_str(), (UINT32)labelStr.size(), fmtLabel_.Get(),
                      D2D1::RectF(left, yy, right, yy + mLabel.height), label);
        yy += mLabel.height + lineGap;

        std::wstring timeStr = FormatTime(lt, cfg_);
        auto mTime = Measure(dwFactory_.Get(), fmtTime_.Get(), timeStr);
        rt->DrawTextW(timeStr.c_str(), (UINT32)timeStr.size(), fmtTime_.Get(),
                      D2D1::RectF(left, yy, right, yy + mTime.height), timeB);
        yy += mTime.height;

        if (cfg_.showDate) {
            std::wstring dateStr = FormatDatePattern(lt, ToW(cfg_.dateFormat));
            auto mDate = Measure(dwFactory_.Get(), fmtDate_.Get(), dateStr);
            yy += lineGap;
            rt->DrawTextW(dateStr.c_str(), (UINT32)dateStr.size(), fmtDate_.Get(),
                          D2D1::RectF(left, yy, right, yy + mDate.height), dateB);
        }

        if (horiz) x += blockW + cityGap;
        else       y += sizes[i].height + cityGap;
    }
}

void Renderer::DrawAnalog(ID2D1RenderTarget* rt, system_clock::time_point now,
                          ID2D1SolidColorBrush* accent) {
    const float pad = Sc(14.f);
    const float lineGap = Sc(2.f);
    const float cityGap = Sc(12.f);
    const float faceD = Sc(80.f);
    const float r = faceD / 2.f;
    const bool horiz = cfg_.layout == LayoutDir::Horizontal;

    // Two palettes: AM = light/white face with dark hands, PM = dark face with
    // light hands. Picked per city from its own local hour (0-11 = AM).
    ComPtr<ID2D1SolidColorBrush> amFace, amHand, amTick, pmFace, pmHand, pmTick;
    rt->CreateSolidColorBrush(D2D1::ColorF(0xF5F7FA, 0.95f), &amFace); // near-white
    rt->CreateSolidColorBrush(D2D1::ColorF(0x14161C, 1.00f), &amHand); // dark hands
    rt->CreateSolidColorBrush(D2D1::ColorF(0x8A93A2, 1.00f), &amTick); // gray ticks
    rt->CreateSolidColorBrush(D2D1::ColorF(0x20242E, 0.95f), &pmFace); // dark
    rt->CreateSolidColorBrush(D2D1::ColorF(0xF2F4F8, 1.00f), &pmHand); // light hands
    rt->CreateSolidColorBrush(D2D1::ColorF(0x4A5160, 1.00f), &pmTick); // dim ticks
    ComPtr<ID2D1SolidColorBrush> dateBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0x9AA3B2, 1.00f), &dateBrush); // dim date text

    std::vector<LocalTimeFields> lts;
    std::vector<D2D1_SIZE_F> sizes;
    float maxW = 0.f;
    for (auto& c : cfg_.cities) {
        auto lt = ComputeLocal(c.tz, now);
        lts.push_back(lt); auto s = MeasureBlock(c, lt);
        sizes.push_back(s); maxW = (std::max)(maxW, s.width);
    }

    // Date / label are centered within each block; restore alignment afterwards.
    fmtLabel_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmtDate_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    float x = pad, y = pad;
    for (size_t i = 0; i < cfg_.cities.size(); ++i) {
        const auto& lt = lts[i];
        float blockW = horiz ? sizes[i].width : maxW;
        float left = horiz ? x : pad;
        float cx = left + blockW / 2.f;
        float ccy = y + r;

        bool am = lt.valid && lt.hour < 12;
        ID2D1SolidColorBrush* face  = am ? amFace.Get() : pmFace.Get();
        ID2D1SolidColorBrush* hands = am ? amHand.Get() : pmHand.Get();
        ID2D1SolidColorBrush* tick  = am ? amTick.Get() : pmTick.Get();

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
            if (cfg_.showSeconds) hand(sec / 60.f, r * 0.85f, Sc(1.0f), accent);
        }
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, ccy), Sc(2.5f), Sc(2.5f)), hands);

        // Date (below the face), then city label (below the date).
        float yy = y + faceD;
        if (cfg_.showDate) {
            std::wstring dateStr = FormatDatePattern(lt, ToW(cfg_.dateFormat));
            auto mDate = Measure(dwFactory_.Get(), fmtDate_.Get(), dateStr);
            yy += lineGap;
            rt->DrawTextW(dateStr.c_str(), (UINT32)dateStr.size(), fmtDate_.Get(),
                          D2D1::RectF(left, yy, left + blockW, yy + mDate.height), dateBrush.Get());
            yy += mDate.height;
        }
        std::wstring labelStr = ToW(cfg_.cities[i].label);
        auto mLabel = Measure(dwFactory_.Get(), fmtLabel_.Get(), labelStr);
        yy += lineGap;
        rt->DrawTextW(labelStr.c_str(), (UINT32)labelStr.size(), fmtLabel_.Get(),
                      D2D1::RectF(left, yy, left + blockW, yy + mLabel.height), accent);

        if (horiz) x += blockW + cityGap;
        else       y += sizes[i].height + cityGap;
    }

    fmtLabel_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmtDate_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}
