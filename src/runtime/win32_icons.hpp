#pragma once

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <unordered_map>

namespace schiva::ui::detail {

// Own one native icon for each requested pixel size. Avoid LR_SHARED because
// LoadImage may return the first cached resource regardless of later sizes.
class AppIconCache {
public:
    AppIconCache(HINSTANCE instance, int resource_id)
        : instance_(instance), resource_id_(resource_id) {}

    AppIconCache(const AppIconCache&) = delete;
    AppIconCache& operator=(const AppIconCache&) = delete;

    ~AppIconCache() {
        for (const auto& [pixels, icon] : icons_) {
            (void)pixels;
            if (icon != nullptr) {
                DestroyIcon(icon);
            }
        }
    }

    HICON load(int pixels) {
        pixels = std::max(1, pixels);
        if (const auto found = icons_.find(pixels);
            found != icons_.end()) {
            return found->second;
        }
        HICON icon = reinterpret_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(resource_id_),
            IMAGE_ICON,
            pixels,
            pixels,
            LR_DEFAULTCOLOR));
        if (icon != nullptr) {
            try {
                icons_.emplace(pixels, icon);
            } catch (...) {
                DestroyIcon(icon);
                throw;
            }
        }
        return icon;
    }

    void abandon_handles() {
        icons_.clear();
    }

private:
    HINSTANCE instance_{};
    int resource_id_{};
    std::unordered_map<int, HICON> icons_;
};

}  // namespace schiva::ui::detail

#endif
