// An IWICBitmap over memory someone else owns.
//
// The renderer draws through a Direct2D WIC bitmap render target because, unlike
// a GDI DC target, it keeps the alpha channel. A bitmap from IWICImagingFactory
// owns its own pixels, though, so every frame had to be copied from it into the
// DIB that UpdateLayeredWindow presents. Wrapping the DIB's bits instead lets
// Direct2D write the frame straight into the DIB: one buffer and no copy.
//
// Format is fixed to 32bpp premultiplied BGRA (GUID_WICPixelFormat32bppPBGRA),
// which is what both Direct2D and UpdateLayeredWindow want.
#pragma once
#include <windows.h>
#include <wincodec.h>

// Creates a bitmap of `width` x `height` pixels whose rows start every `stride`
// bytes from `bits`. The memory is not copied or owned: the caller keeps it
// alive, and unchanged in place, until the bitmap and everything holding a
// reference to it (a render target, an outstanding lock) has been released.
// Fails with E_INVALIDARG on a null pointer, an empty size, or a stride shorter
// than a row.
HRESULT CreateDibWicBitmap(void* bits, UINT width, UINT height, UINT stride,
                           IWICBitmap** out);
