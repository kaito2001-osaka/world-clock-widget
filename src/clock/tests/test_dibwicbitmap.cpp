// Tests for the IWICBitmap that wraps the presented DIB (DibWicBitmap.hpp).
//
// The last group drives a real Direct2D WIC render target over the wrapper:
// whether D2D accepts a bitmap it did not create, and writes the frame into
// our memory with alpha intact, is the whole point of the class.
#include "test_framework.hpp"
#include "DibWicBitmap.hpp"

#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

#pragma comment(lib, "d2d1.lib")

using Microsoft::WRL::ComPtr;

namespace {

// 4 x 3 pixels with 8 bytes of padding per row, so a write that ignored the
// stride, or ran past a row, would land in the padding.
constexpr UINT kW = 4, kH = 3, kStride = kW * 4 + 8;
constexpr uint8_t kPad = 0xCD;

std::vector<uint8_t> PaddedBuffer() {
    return std::vector<uint8_t>((size_t)kStride * kH, kPad);
}

ComPtr<IWICBitmap> Wrap(std::vector<uint8_t>& buf) {
    ComPtr<IWICBitmap> bmp;
    CreateDibWicBitmap(buf.data(), kW, kH, kStride, &bmp);
    return bmp;
}

bool PaddingIntact(const std::vector<uint8_t>& buf) {
    for (UINT y = 0; y < kH; ++y)
        for (UINT x = kW * 4; x < kStride; ++x)
            if (buf[(size_t)y * kStride + x] != kPad) return false;
    return true;
}

// Every pixel of the bitmap area equals {b, g, r, a}.
bool AllPixels(const std::vector<uint8_t>& buf, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    for (UINT y = 0; y < kH; ++y)
        for (UINT x = 0; x < kW; ++x) {
            const uint8_t* p = &buf[(size_t)y * kStride + x * 4];
            if (p[0] != b || p[1] != g || p[2] != r || p[3] != a) return false;
        }
    return true;
}

// Clears a WIC render target over `bmp` to `color` and ends the draw.
HRESULT ClearThroughD2D(IWICBitmap* bmp, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1Factory> factory;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    if (FAILED(hr)) return hr;
    // Same properties as Renderer::EnsureSurface.
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f, 96.f);
    ComPtr<ID2D1RenderTarget> rt;
    hr = factory->CreateWicBitmapRenderTarget(bmp, props, &rt);
    if (FAILED(hr)) return hr;
    rt->BeginDraw();
    rt->Clear(color);
    return rt->EndDraw();
}

} // namespace

// ---- creation -------------------------------------------------------------

TEST(dibwic_reports_size_format_and_resolution) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    CHECK(bmp != nullptr);

    UINT w = 0, h = 0;
    CHECK(SUCCEEDED(bmp->GetSize(&w, &h)));
    CHECK_EQ(w, kW);
    CHECK_EQ(h, kH);

    WICPixelFormatGUID fmt{};
    CHECK(SUCCEEDED(bmp->GetPixelFormat(&fmt)));
    CHECK(fmt == GUID_WICPixelFormat32bppPBGRA);

    double dx = 0, dy = 0;
    CHECK(SUCCEEDED(bmp->GetResolution(&dx, &dy)));
    CHECK(dx == 96.0 && dy == 96.0);
}

TEST(dibwic_rejects_bad_arguments) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp;
    CHECK_EQ(CreateDibWicBitmap(nullptr, kW, kH, kStride, &bmp), E_INVALIDARG);
    CHECK_EQ(CreateDibWicBitmap(buf.data(), 0, kH, kStride, &bmp), E_INVALIDARG);
    CHECK_EQ(CreateDibWicBitmap(buf.data(), kW, 0, kStride, &bmp), E_INVALIDARG);
    CHECK_EQ(CreateDibWicBitmap(buf.data(), kW, kH, kW * 4 - 1, &bmp), E_INVALIDARG);
    CHECK_EQ(CreateDibWicBitmap(buf.data(), kW, kH, kStride, nullptr), E_INVALIDARG);
    CHECK(bmp == nullptr);
    // A stride of exactly one row is fine.
    CHECK(SUCCEEDED(CreateDibWicBitmap(buf.data(), kW, kH, kW * 4, &bmp)));
}

// ---- Lock -----------------------------------------------------------------

TEST(dibwic_lock_whole_bitmap_points_at_the_memory) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    ComPtr<IWICBitmapLock> lock;
    CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockWrite, &lock)));

    UINT w = 0, h = 0, stride = 0, size = 0;
    WICInProcPointer data = nullptr;
    CHECK(SUCCEEDED(lock->GetSize(&w, &h)));
    CHECK(SUCCEEDED(lock->GetStride(&stride)));
    CHECK(SUCCEEDED(lock->GetDataPointer(&size, &data)));
    CHECK_EQ(w, kW);
    CHECK_EQ(h, kH);
    CHECK_EQ(stride, kStride);
    CHECK(data == buf.data());
    // Up to the end of the last pixel, not the last row's padding.
    CHECK_EQ(size, kStride * (kH - 1) + kW * 4);

    WICPixelFormatGUID fmt{};
    CHECK(SUCCEEDED(lock->GetPixelFormat(&fmt)));
    CHECK(fmt == GUID_WICPixelFormat32bppPBGRA);
}

TEST(dibwic_lock_sub_rect_offsets_into_the_memory) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    const WICRect rc = { 1, 2, 3, 1 };   // the last three pixels of the last row
    ComPtr<IWICBitmapLock> lock;
    CHECK(SUCCEEDED(bmp->Lock(&rc, WICBitmapLockRead, &lock)));

    UINT w = 0, h = 0, size = 0;
    WICInProcPointer data = nullptr;
    CHECK(SUCCEEDED(lock->GetSize(&w, &h)));
    CHECK(SUCCEEDED(lock->GetDataPointer(&size, &data)));
    CHECK_EQ(w, 3u);
    CHECK_EQ(h, 1u);
    CHECK(data == buf.data() + 2 * kStride + 1 * 4);
    CHECK_EQ(size, 3u * 4);
}

TEST(dibwic_lock_rejects_rects_outside_the_bitmap) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    const WICRect bad[] = {
        { -1, 0, 1, 1 },                 // negative origin
        { 0, -1, 1, 1 },
        { 0, 0, 0, 1 },                  // empty
        { 0, 0, 1, 0 },
        { 0, 0, -1, 1 },                 // negative size
        { 0, 0, kW + 1, 1 },             // one past the right edge
        { 0, 0, 1, kH + 1 },             // one past the bottom
        { (INT)kW, 0, 1, 1 },            // starts at the right edge
        { 1, 0, 0x7FFFFFFF, 1 },         // X + Width overflows INT
    };
    for (const WICRect& rc : bad) {
        ComPtr<IWICBitmapLock> lock;
        CHECK_EQ(bmp->Lock(&rc, WICBitmapLockRead, &lock), E_INVALIDARG);
        CHECK(lock == nullptr);
    }
    // Exactly the whole bitmap, spelled out, is fine.
    const WICRect all = { 0, 0, (INT)kW, (INT)kH };
    ComPtr<IWICBitmapLock> lock;
    CHECK(SUCCEEDED(bmp->Lock(&all, WICBitmapLockRead, &lock)));
}

TEST(dibwic_lock_requires_read_or_write) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    ComPtr<IWICBitmapLock> lock;
    CHECK_EQ(bmp->Lock(nullptr, 0, &lock), E_INVALIDARG);
    CHECK_EQ(bmp->Lock(nullptr, WICBitmapLockRead, nullptr), E_INVALIDARG);
}

TEST(dibwic_lock_keeps_the_bitmap_alive) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmapLock> lock;
    {
        ComPtr<IWICBitmap> bmp = Wrap(buf);
        CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockWrite, &lock)));
    }
    // The bitmap's last outside reference is gone; the lock still works.
    UINT stride = 0;
    CHECK(SUCCEEDED(lock->GetStride(&stride)));
    CHECK_EQ(stride, kStride);
}

// ---- CopyPixels -----------------------------------------------------------

TEST(dibwic_copy_pixels_copies_a_sub_rect_row_by_row) {
    auto buf = PaddedBuffer();
    for (UINT y = 0; y < kH; ++y)
        for (UINT x = 0; x < kW * 4; ++x)
            buf[(size_t)y * kStride + x] = (uint8_t)(y * 16 + x);
    ComPtr<IWICBitmap> bmp = Wrap(buf);

    const WICRect rc = { 1, 1, 2, 2 };
    uint8_t out[2 * 8] = {};
    CHECK(SUCCEEDED(bmp->CopyPixels(&rc, 8, sizeof out, out)));
    for (UINT y = 0; y < 2; ++y)
        for (UINT x = 0; x < 8; ++x)
            CHECK_EQ(out[y * 8 + x], (uint8_t)((y + 1) * 16 + 4 + x));
}

TEST(dibwic_copy_pixels_rejects_short_buffers_and_strides) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    std::vector<uint8_t> out((size_t)kW * 4 * kH);
    const UINT row = kW * 4;
    CHECK_EQ(bmp->CopyPixels(nullptr, row - 1, (UINT)out.size(), out.data()), E_INVALIDARG);
    CHECK_EQ(bmp->CopyPixels(nullptr, row, (UINT)out.size() - 1, out.data()), E_INVALIDARG);
    CHECK_EQ(bmp->CopyPixels(nullptr, row, (UINT)out.size(), nullptr), E_INVALIDARG);
    const WICRect outside = { 0, 0, (INT)kW + 1, 1 };
    CHECK_EQ(bmp->CopyPixels(&outside, row + 4, (UINT)out.size(), out.data()), E_INVALIDARG);
    // The last row needs only its own bytes, not a whole stride.
    CHECK(SUCCEEDED(bmp->CopyPixels(nullptr, row, (UINT)out.size(), out.data())));
}

TEST(dibwic_has_no_palette) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    CHECK_EQ(bmp->CopyPalette(nullptr), WINCODEC_ERR_PALETTEUNAVAILABLE);
    CHECK_EQ(bmp->SetPalette(nullptr), WINCODEC_ERR_PALETTEUNAVAILABLE);
}

TEST(dibwic_answers_to_its_interfaces) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    ComPtr<IWICBitmapSource> src;
    ComPtr<IUnknown> unk;
    CHECK(SUCCEEDED(bmp.As(&src)));
    CHECK(SUCCEEDED(bmp.As(&unk)));
}

// ---- through Direct2D -----------------------------------------------------

TEST(dibwic_d2d_writes_an_opaque_frame_into_the_memory) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    // 0x336699: r=0x33, g=0x66, b=0x99. Exact in 8 bits, so no rounding.
    CHECK(SUCCEEDED(ClearThroughD2D(bmp.Get(), D2D1::ColorF(0x336699, 1.f))));
    CHECK(AllPixels(buf, 0x99, 0x66, 0x33, 0xFF));
    CHECK(PaddingIntact(buf));
}

TEST(dibwic_d2d_keeps_alpha_premultiplied) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    // White at alpha 0x80 premultiplies to 0x80 in every channel.
    CHECK(SUCCEEDED(ClearThroughD2D(bmp.Get(), D2D1::ColorF(1.f, 1.f, 1.f, 128.f / 255.f))));
    CHECK(AllPixels(buf, 0x80, 0x80, 0x80, 0x80));
    CHECK(PaddingIntact(buf));
}

TEST(dibwic_d2d_writes_full_transparency) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    CHECK(SUCCEEDED(ClearThroughD2D(bmp.Get(), D2D1::ColorF(0, 0.f))));
    CHECK(AllPixels(buf, 0, 0, 0, 0));
    CHECK(PaddingIntact(buf));
}

// ---- lock reuse -----------------------------------------------------------
// Direct2D locks the bitmap at every EndDraw. The bitmap keeps one lock inside
// itself and hands that out again, so a steady-state frame does not allocate;
// only a lock taken while another is out gets an object of its own.

TEST(dibwic_lock_is_reused_once_released) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    IWICBitmapLock* firstAddr = nullptr;
    {
        ComPtr<IWICBitmapLock> lock;
        CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockWrite, &lock)));
        firstAddr = lock.Get();
    }
    ComPtr<IWICBitmapLock> again;
    CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockWrite, &again)));
    CHECK(again.Get() == firstAddr);
}

TEST(dibwic_reused_lock_reports_the_new_rect) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    {
        const WICRect rc = { 1, 2, 3, 1 };
        ComPtr<IWICBitmapLock> lock;
        CHECK(SUCCEEDED(bmp->Lock(&rc, WICBitmapLockRead, &lock)));
    }
    ComPtr<IWICBitmapLock> lock;
    CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockWrite, &lock)));
    UINT w = 0, h = 0, size = 0;
    WICInProcPointer data = nullptr;
    CHECK(SUCCEEDED(lock->GetSize(&w, &h)));
    CHECK(SUCCEEDED(lock->GetDataPointer(&size, &data)));
    CHECK_EQ(w, kW);
    CHECK_EQ(h, kH);
    CHECK(data == buf.data());
    CHECK_EQ(size, kStride * (kH - 1) + kW * 4);
}

TEST(dibwic_concurrent_locks_are_separate) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    const WICRect top = { 0, 0, (INT)kW, 1 };
    const WICRect bottom = { 0, 2, (INT)kW, 1 };
    ComPtr<IWICBitmapLock> a, b;
    CHECK(SUCCEEDED(bmp->Lock(&top, WICBitmapLockRead, &a)));
    CHECK(SUCCEEDED(bmp->Lock(&bottom, WICBitmapLockRead, &b)));
    CHECK(a.Get() != b.Get());

    // The second lock must not have re-aimed the first.
    UINT size = 0;
    WICInProcPointer da = nullptr, db = nullptr;
    CHECK(SUCCEEDED(a->GetDataPointer(&size, &da)));
    CHECK(SUCCEEDED(b->GetDataPointer(&size, &db)));
    CHECK(da == buf.data());
    CHECK(db == buf.data() + 2 * kStride);

    // With the first returned, the next lock reuses it.
    IWICBitmapLock* firstAddr = a.Get();
    a.Reset();
    ComPtr<IWICBitmapLock> c;
    CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockRead, &c)));
    CHECK(c.Get() == firstAddr);
}

TEST(dibwic_second_lock_also_keeps_the_bitmap_alive) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmapLock> first, second;
    {
        ComPtr<IWICBitmap> bmp = Wrap(buf);
        CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockWrite, &first)));
        CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockWrite, &second)));
    }
    // Either order of release must leave the other working.
    first.Reset();
    UINT stride = 0;
    CHECK(SUCCEEDED(second->GetStride(&stride)));
    CHECK_EQ(stride, kStride);
}

TEST(dibwic_lock_answers_to_its_interfaces) {
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    ComPtr<IWICBitmapLock> lock;
    CHECK(SUCCEEDED(bmp->Lock(nullptr, WICBitmapLockRead, &lock)));
    ComPtr<IUnknown> unk;
    ComPtr<IWICBitmapLock> same;
    ComPtr<IWICBitmap> notALock;
    CHECK(SUCCEEDED(lock.As(&unk)));
    CHECK(SUCCEEDED(lock.As(&same)));
    CHECK(same.Get() == lock.Get());
    CHECK_EQ(lock.As(&notALock), E_NOINTERFACE);
}

TEST(dibwic_d2d_draws_repeatedly_through_the_reused_lock) {
    // Two frames through one render target, the way the renderer runs.
    auto buf = PaddedBuffer();
    ComPtr<IWICBitmap> bmp = Wrap(buf);
    ComPtr<ID2D1Factory> factory;
    CHECK(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())));
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f, 96.f);
    ComPtr<ID2D1RenderTarget> rt;
    CHECK(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt)));
    if (!rt) return;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0x336699, 1.f));
    CHECK(SUCCEEDED(rt->EndDraw()));
    CHECK(AllPixels(buf, 0x99, 0x66, 0x33, 0xFF));

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0.f));
    CHECK(SUCCEEDED(rt->EndDraw()));
    CHECK(AllPixels(buf, 0, 0, 0, 0));
    CHECK(PaddingIntact(buf));
}
