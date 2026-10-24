#include "../src/runtime/win32_icons.hpp"

#include <array>
#include <iostream>

namespace {

bool icon_size(HICON icon, int expected) {
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) {
        std::cerr << "GetIconInfo failed for " << expected << " px\n";
        return false;
    }
    BITMAP bitmap{};
    const int read = info.hbmColor == nullptr
        ? 0
        : GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
    if (info.hbmColor != nullptr) {
        DeleteObject(info.hbmColor);
    }
    if (info.hbmMask != nullptr) {
        DeleteObject(info.hbmMask);
    }
    if (read == 0 || bitmap.bmWidth != expected ||
        bitmap.bmHeight != expected) {
        std::cerr << "requested " << expected << " px, got "
                  << bitmap.bmWidth << 'x' << bitmap.bmHeight << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    constexpr std::array sizes{16, 24, 32, 48, 64};
    constexpr int app_icon_resource_id = 101;
    schiva::ui::detail::AppIconCache cache(
        GetModuleHandleW(nullptr), app_icon_resource_id);
    std::array<HICON, sizes.size()> icons{};
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        icons[index] = cache.load(sizes[index]);
        if (icons[index] == nullptr || !icon_size(icons[index], sizes[index])) {
            return 1;
        }
        if (cache.load(sizes[index]) != icons[index]) {
            std::cerr << "same-size request did not reuse its cached HICON\n";
            return 1;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (icons[previous] == icons[index]) {
                std::cerr << "different pixel sizes reused one HICON\n";
                return 1;
            }
        }
    }

    schiva::ui::detail::AppIconCache reverse_cache(
        GetModuleHandleW(nullptr), app_icon_resource_id);
    std::array<HICON, sizes.size()> reverse_icons{};
    for (std::size_t step = 0; step < sizes.size(); ++step) {
        const std::size_t index = sizes.size() - step - 1;
        reverse_icons[index] = reverse_cache.load(sizes[index]);
        if (reverse_icons[index] == nullptr ||
            !icon_size(reverse_icons[index], sizes[index])) {
            return 1;
        }
        if (reverse_cache.load(sizes[index]) != reverse_icons[index]) {
            std::cerr << "reverse-order cache request changed its HICON\n";
            return 1;
        }
    }
    return 0;
}
