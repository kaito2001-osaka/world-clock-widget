#include "DibWicBitmap.hpp"

#include <wrl/client.h>
#include <wrl/implements.h>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")   // GUID_WICPixelFormat32bppPBGRA

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ChainInterfaces;

namespace {

constexpr UINT kBytesPerPixel = 4;

// Resolves `rc` (null = the whole bitmap) against a w x h bitmap. An empty,
// negative or out-of-bounds rect is rejected rather than clipped: a caller that
// asked for pixels that do not exist has a bug, and handing back a smaller
// rect than it asked for would let it write past what it thinks it locked.
bool ResolveRect(const WICRect* rc, UINT w, UINT h, WICRect& out) {
    if (!rc) {
        out = { 0, 0, (INT)w, (INT)h };
        return true;
    }
    if (rc->X < 0 || rc->Y < 0 || rc->Width <= 0 || rc->Height <= 0) return false;
    // 64-bit sums so a huge X + Width cannot wrap back into range.
    if ((long long)rc->X + rc->Width > (long long)w) return false;
    if ((long long)rc->Y + rc->Height > (long long)h) return false;
    out = *rc;
    return true;
}

// What a lock hands out: a rect of the bitmap's memory. The IWICBitmapLock
// queries, shared by both kinds of lock below.
struct LockView {
    BYTE* data = nullptr;
    UINT  stride = 0, w = 0, h = 0;

    HRESULT GetSize(UINT* pw, UINT* ph) const {
        if (!pw || !ph) return E_INVALIDARG;
        *pw = w; *ph = h;
        return S_OK;
    }
    HRESULT GetStride(UINT* ps) const {
        if (!ps) return E_INVALIDARG;
        *ps = stride;
        return S_OK;
    }
    // The span runs from the first pixel of the rect to the last, not to the end
    // of the last full row: bytes past the rect's right edge on its last row may
    // belong to nobody (or, with a sub-rect, to pixels the caller did not lock).
    HRESULT GetDataPointer(UINT* size, WICInProcPointer* pd) const {
        if (!size || !pd) return E_INVALIDARG;
        *size = stride * (h - 1) + w * kBytesPerPixel;
        *pd = data;
        return S_OK;
    }
    static HRESULT GetPixelFormat(WICPixelFormatGUID* fmt) {
        if (!fmt) return E_INVALIDARG;
        *fmt = GUID_WICPixelFormat32bppPBGRA;
        return S_OK;
    }
};

// A lock with its own heap object, for when the embedded one (below) is
// already handed out.
class DibWicBitmapLock
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IWICBitmapLock> {
public:
    // Holds a reference to its bitmap, as WIC's own locks do, so the bitmap
    // cannot go away while a lock still points into it.
    DibWicBitmapLock(IWICBitmap* owner, const LockView& view)
        : owner_(owner), view_(view) {}

    STDMETHODIMP GetSize(UINT* w, UINT* h) override { return view_.GetSize(w, h); }
    STDMETHODIMP GetStride(UINT* stride) override { return view_.GetStride(stride); }
    STDMETHODIMP GetDataPointer(UINT* size, WICInProcPointer* data) override {
        return view_.GetDataPointer(size, data);
    }
    STDMETHODIMP GetPixelFormat(WICPixelFormatGUID* fmt) override {
        return LockView::GetPixelFormat(fmt);
    }

private:
    ComPtr<IWICBitmap> owner_;
    LockView view_;
};

// The lock Direct2D takes at every EndDraw, kept inside the bitmap so that
// taking it does not allocate. Every reference to it is also a reference to
// the bitmap -- so it keeps the bitmap alive just as a heap lock does, and the
// bitmap (with this inside it) cannot be freed while the lock is out. Its own
// count says only whether it is handed out. Not thread-safe, like the
// renderer's single-threaded factory that drives it.
class EmbeddedLock final : public IWICBitmapLock {
public:
    explicit EmbeddedLock(IWICBitmap* owner) : owner_(owner) {}

    bool InUse() const { return refs_ != 0; }
    // Only while not in use: whoever holds it is reading view_.
    void Aim(const LockView& view) { view_ = view; }

    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IWICBitmapLock)) {
            *out = static_cast<IWICBitmapLock*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        owner_->AddRef();
        return ++refs_;
    }
    STDMETHODIMP_(ULONG) Release() override {
        // The owner's release can free the owner, and this with it, so
        // nothing of this object is touched after it.
        IWICBitmap* owner = owner_;
        const ULONG left = --refs_;
        owner->Release();
        return left;
    }

    STDMETHODIMP GetSize(UINT* w, UINT* h) override { return view_.GetSize(w, h); }
    STDMETHODIMP GetStride(UINT* stride) override { return view_.GetStride(stride); }
    STDMETHODIMP GetDataPointer(UINT* size, WICInProcPointer* data) override {
        return view_.GetDataPointer(size, data);
    }
    STDMETHODIMP GetPixelFormat(WICPixelFormatGUID* fmt) override {
        return LockView::GetPixelFormat(fmt);
    }

private:
    IWICBitmap* owner_;   // not a ComPtr: the owner contains this
    ULONG       refs_ = 0;
    LockView    view_;
};

class DibWicBitmap
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                          ChainInterfaces<IWICBitmap, IWICBitmapSource>> {
public:
    DibWicBitmap(BYTE* bits, UINT w, UINT h, UINT stride)
        : bits_(bits), w_(w), h_(h), stride_(stride), lock_(this) {}

    // ---- IWICBitmapSource ----
    STDMETHODIMP GetSize(UINT* w, UINT* h) override {
        if (!w || !h) return E_INVALIDARG;
        *w = w_; *h = h_;
        return S_OK;
    }
    STDMETHODIMP GetPixelFormat(WICPixelFormatGUID* fmt) override {
        if (!fmt) return E_INVALIDARG;
        *fmt = GUID_WICPixelFormat32bppPBGRA;
        return S_OK;
    }
    // The render target is created at 96 DPI (DPI scaling is applied by the
    // renderer itself), so the bitmap says the same.
    STDMETHODIMP GetResolution(double* x, double* y) override {
        if (!x || !y) return E_INVALIDARG;
        *x = 96.0; *y = 96.0;
        return S_OK;
    }
    STDMETHODIMP CopyPalette(IWICPalette*) override {
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    STDMETHODIMP CopyPixels(const WICRect* rc, UINT dstStride, UINT dstSize,
                            BYTE* dst) override {
        WICRect r;
        if (!dst || !ResolveRect(rc, w_, h_, r)) return E_INVALIDARG;
        const UINT rowBytes = (UINT)r.Width * kBytesPerPixel;
        if (dstStride < rowBytes) return E_INVALIDARG;
        // The last row needs only its own bytes, not a full stride.
        const unsigned long long need =
            (unsigned long long)dstStride * (UINT)(r.Height - 1) + rowBytes;
        if (need > dstSize) return E_INVALIDARG;

        const BYTE* src = PixelAt(r.X, r.Y);
        for (INT y = 0; y < r.Height; ++y)
            std::memcpy(dst + (size_t)dstStride * y, src + (size_t)stride_ * y, rowBytes);
        return S_OK;
    }

    // ---- IWICBitmap ----
    // Both read and write locks hand out the memory itself; there is nothing
    // to write back on unlock. The renderer is single-threaded, so concurrent
    // locks are not policed the way WIC's own bitmaps police them.
    STDMETHODIMP Lock(const WICRect* rc, DWORD flags, IWICBitmapLock** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (!(flags & (WICBitmapLockRead | WICBitmapLockWrite))) return E_INVALIDARG;
        WICRect r;
        if (!ResolveRect(rc, w_, h_, r)) return E_INVALIDARG;
        const LockView view{ PixelAt(r.X, r.Y), stride_, (UINT)r.Width, (UINT)r.Height };

        // Every frame locks once, so reusing one lock is what keeps a
        // steady-state frame from allocating.
        if (!lock_.InUse()) {
            lock_.Aim(view);
            lock_.AddRef();
            *out = &lock_;
            return S_OK;
        }
        ComPtr<DibWicBitmapLock> lock = Make<DibWicBitmapLock>(this, view);
        if (!lock) return E_OUTOFMEMORY;
        *out = lock.Detach();
        return S_OK;
    }
    STDMETHODIMP SetPalette(IWICPalette*) override {
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    STDMETHODIMP SetResolution(double, double) override {
        return E_NOTIMPL;
    }

private:
    BYTE* PixelAt(INT x, INT y) const {
        return bits_ + (size_t)stride_ * (UINT)y + (size_t)(UINT)x * kBytesPerPixel;
    }

    BYTE* bits_;
    UINT  w_, h_, stride_;
    EmbeddedLock lock_;
};

} // namespace

HRESULT CreateDibWicBitmap(void* bits, UINT width, UINT height, UINT stride,
                           IWICBitmap** out) {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    if (!bits || width == 0 || height == 0) return E_INVALIDARG;
    // Both counts are INT in a WICRect.
    if (width > 0x7FFFFFFF / kBytesPerPixel || height > 0x7FFFFFFF) return E_INVALIDARG;
    if (stride < width * kBytesPerPixel) return E_INVALIDARG;

    ComPtr<DibWicBitmap> bmp =
        Make<DibWicBitmap>(static_cast<BYTE*>(bits), width, height, stride);
    if (!bmp) return E_OUTOFMEMORY;
    *out = bmp.Detach();
    return S_OK;
}
