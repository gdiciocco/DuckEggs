#ifndef SCHIVA_RUNTIME_BRAND_MARK_HPP
#define SCHIVA_RUNTIME_BRAND_MARK_HPP

#ifdef _WIN32

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>

namespace schiva::ui::brand {

constexpr int kAppIconResourceId = 101;
constexpr int kEmbeddedMarkResourceId = 102;
constexpr int kEmbeddedSmallMarkResourceId = 103;

namespace detail {

struct MarkCache {
    Gdiplus::Bitmap* bitmap = nullptr;
    IStream* stream = nullptr;
    bool load_attempted = false;
};

inline MarkCache& mark_cache() {
    static MarkCache cache;
    return cache;
}

inline void release_mark_cache() {
    MarkCache& cache = mark_cache();
    delete cache.bitmap;
    cache.bitmap = nullptr;
    if (cache.stream != nullptr) {
        cache.stream->Release();
        cache.stream = nullptr;
    }
    cache.load_attempted = false;
}

inline bool decode_mark_resource(int resource_id) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(
        module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (resource == nullptr) {
        return false;
    }
    const DWORD resource_size = SizeofResource(module, resource);
    HGLOBAL resource_data = LoadResource(module, resource);
    const void* resource_bytes =
        resource_data == nullptr ? nullptr : LockResource(resource_data);
    if (resource_size == 0 || resource_bytes == nullptr) {
        return false;
    }

    HGLOBAL stream_memory =
        GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(resource_size));
    if (stream_memory == nullptr) {
        return false;
    }
    void* stream_bytes = GlobalLock(stream_memory);
    if (stream_bytes == nullptr) {
        GlobalFree(stream_memory);
        return false;
    }
    std::memcpy(stream_bytes, resource_bytes, resource_size);
    GlobalUnlock(stream_memory);

    IStream* stream = nullptr;
    const HRESULT stream_status =
        CreateStreamOnHGlobal(stream_memory, TRUE, &stream);
    if (FAILED(stream_status) || stream == nullptr) {
        if (stream != nullptr) {
            stream->Release();
        } else {
            GlobalFree(stream_memory);
        }
        return false;
    }
    LARGE_INTEGER beginning{};
    if (FAILED(stream->Seek(beginning, STREAM_SEEK_SET, nullptr))) {
        stream->Release();
        return false;
    }

    Gdiplus::Bitmap* bitmap = nullptr;
    try {
        bitmap = new Gdiplus::Bitmap(stream, FALSE);
    } catch (const std::bad_alloc&) {
        stream->Release();
        return false;
    }
    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        stream->Release();
        return false;
    }
    MarkCache& cache = mark_cache();
    cache.bitmap = bitmap;
    cache.stream = stream;
    return true;
}

inline bool ensure_mark_cache() {
    MarkCache& cache = mark_cache();
    if (cache.bitmap != nullptr) {
        return true;
    }
    if (cache.load_attempted) {
        return false;
    }
    cache.load_attempted = true;
    return decode_mark_resource(kEmbeddedMarkResourceId) ||
           decode_mark_resource(kEmbeddedSmallMarkResourceId);
}

}  // namespace detail

inline void release_mark_cache() {
    detail::release_mark_cache();
}

inline void draw_mark(
    Gdiplus::Graphics& graphics,
    const Gdiplus::RectF& bounds,
    Gdiplus::REAL stroke_width) {
    using namespace Gdiplus;

    const REAL size = std::min(bounds.Width, bounds.Height);
    if (size <= 0.0F || stroke_width <= 0.0F ||
        !detail::ensure_mark_cache()) {
        return;
    }

    Bitmap* image = detail::mark_cache().bitmap;
    const REAL source_width = static_cast<REAL>(image->GetWidth());
    const REAL source_height = static_cast<REAL>(image->GetHeight());
    if (source_width <= 0.0F || source_height <= 0.0F) {
        return;
    }
    const REAL scale =
        std::min(bounds.Width / source_width, bounds.Height / source_height);
    const REAL width = source_width * scale;
    const REAL height = source_height * scale;
    const RectF destination{
        bounds.X + (bounds.Width - width) / 2.0F,
        bounds.Y + (bounds.Height - height) / 2.0F,
        width,
        height};

    const GraphicsState previous = graphics.Save();
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.DrawImage(
        image,
        destination,
        0.0F,
        0.0F,
        source_width,
        source_height,
        UnitPixel);
    graphics.Restore(previous);
}

}  // namespace schiva::ui::brand

#endif

#endif
