#include "schiva/platform.hpp"

#include "chromium_pip_titles.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace schiva {
namespace {

wchar_t fold_character(wchar_t value) {
    if (value >= L'A' && value <= L'Z') {
        return static_cast<wchar_t>(value + (L'a' - L'A'));
    }
    if (value >= L'\u00C0' && value <= L'\u00FF') {
        constexpr std::array<wchar_t, 64> folded = {
            L'a', L'a', L'a', L'a', L'a', L'a', L'a', L'c',
            L'e', L'e', L'e', L'e', L'i', L'i', L'i', L'i',
            L'd', L'n', L'o', L'o', L'o', L'o', L'o', L'x',
            L'o', L'u', L'u', L'u', L'u', L'y', L't', L's',
            L'a', L'a', L'a', L'a', L'a', L'a', L'a', L'c',
            L'e', L'e', L'e', L'e', L'i', L'i', L'i', L'i',
            L'd', L'n', L'o', L'o', L'o', L'o', L'o', L'/',
            L'o', L'u', L'u', L'u', L'u', L'y', L't', L'y',
        };
        return folded[static_cast<std::size_t>(value - L'\u00C0')];
    }
    return static_cast<wchar_t>(std::towlower(value));
}

bool title_word(wchar_t value) {
    return (value >= L'a' && value <= L'z') ||
           (value >= L'0' && value <= L'9') ||
           value >= 0x80;
}

bool ends_with_case_insensitive(
    std::wstring_view value,
    std::wstring_view suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }
    const auto offset = value.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        if (fold_character(value[offset + index]) !=
            fold_character(suffix[index])) {
            return false;
        }
    }
    return true;
}

bool segment_hits_forbidden(double start, double end, double low, double high) {
    if (std::abs(end - start) <= 1.0e-12) {
        return low < start && start < high;
    }
    double entry = (low - start) / (end - start);
    double exit = (high - start) / (end - start);
    if (entry > exit) {
        std::swap(entry, exit);
    }
    return std::max(0.0, entry) < std::min(1.0, exit);
}

double clearance_to_rect(const Rect& rect, Point point) {
    const double nearest_x = std::clamp(
        static_cast<double>(point.x), static_cast<double>(rect.left),
        static_cast<double>(rect.right));
    const double nearest_y = std::clamp(
        static_cast<double>(point.y), static_cast<double>(rect.top),
        static_cast<double>(rect.bottom));
    return std::hypot(point.x - nearest_x, point.y - nearest_y);
}

bool segment_has_nondecreasing_pointer_clearance(
    Point start, Point end, MotionSize size, Point cursor) {
    const double start_x = static_cast<double>(start.x);
    const double start_y = static_cast<double>(start.y);
    const double cursor_x = static_cast<double>(cursor.x);
    const double cursor_y = static_cast<double>(cursor.y);
    const double dx =
        static_cast<double>(end.x) - static_cast<double>(start.x);
    const double dy =
        static_cast<double>(end.y) - static_cast<double>(start.y);

    // The squared distance derivative is twice the sum of gap times
    // signed gap velocity. Negative-velocity gaps only shrink in magnitude,
    // so a nonnegative initial sum bounds the derivative for the whole
    // segment.
    double gap_x = 0.0;
    double proof = 0.0;
    if (cursor_x < start_x) {
        gap_x = start_x - cursor_x;
        proof += gap_x * dx;
    } else if (cursor_x > start_x + size.width) {
        gap_x = cursor_x - (start_x + size.width);
        proof -= gap_x * dx;
    }

    double gap_y = 0.0;
    if (cursor_y < start_y) {
        gap_y = start_y - cursor_y;
        proof += gap_y * dy;
    } else if (cursor_y > start_y + size.height) {
        gap_y = cursor_y - (start_y + size.height);
        proof -= gap_y * dy;
    }
    return (gap_x > 0.0 || gap_y > 0.0) && proof >= 0.0;
}

constexpr int kVisualControlMarginPixels = 20;

}  // namespace

std::string current_platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "other";
#endif
}

std::filesystem::path default_data_dir() {
#ifdef _WIN32
    const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
    if (base != nullptr && *base != L'\0') {
        return std::filesystem::path(base) / L"DuckEggs";
    }
    wchar_t temporary[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH, temporary);
    return std::filesystem::path(std::wstring(temporary, length)) /
           L"DuckEggs";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" /
               "Application Support" / "DuckEggs";
    }
    return std::filesystem::temp_directory_path() / "DuckEggs";
#elif defined(__linux__)
    const char* data_home = std::getenv("XDG_DATA_HOME");
    if (data_home != nullptr && *data_home != '\0') {
        return std::filesystem::path(data_home) / "duckeggs";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" /
               "duckeggs";
    }
    return std::filesystem::temp_directory_path() / "duckeggs";
#else
    return std::filesystem::temp_directory_path() / "duckeggs";
#endif
}

std::string path_to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring value = path.wstring();
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        throw PlatformError("a path could not be converted to UTF-8");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    (void)WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
#else
    const std::string value = path.string();
    return value;
#endif
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
#ifdef _WIN32
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0);
    if (size <= 0) {
        throw PlatformError("a UTF-8 path is invalid");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    (void)MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        result.data(),
        size);
    return std::filesystem::path(result);
#else
    return std::filesystem::path(std::string(utf8));
#endif
}

std::wstring normalize_title(std::wstring_view title) {
    std::wstring result;
    result.reserve(title.size());
    bool separator = true;
    for (const wchar_t raw : title) {
        if (raw >= 0x0300 && raw <= 0x036F) {
            continue;
        }
        wchar_t value = fold_character(raw);
        if (value == L'\u2018' || value == L'\u2019' ||
            value == L'\'' || value == L'_') {
            value = L' ';
        }
        if (title_word(value)) {
            result.push_back(value);
            separator = false;
        } else if (!separator) {
            result.push_back(L' ');
            separator = true;
        }
    }
    while (!result.empty() && result.back() == L' ') {
        result.pop_back();
    }
    return result;
}

namespace {

const std::vector<std::wstring>& chromium_pip_title_aliases() {
    static const std::vector<std::wstring> aliases = [] {
        std::vector<std::wstring> result;
        result.reserve(detail::kChromiumPipOfficialTitles.size() + 1);
        for (const std::wstring_view title :
             detail::kChromiumPipOfficialTitles) {
            result.push_back(normalize_title(title));
        }
        // Keep recognition of the title shipped by older DuckEggs builds.
        result.push_back(normalize_title(L"Immagine nell\u2019immagine"));
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }();
    return aliases;
}

}  // namespace

bool title_matches(std::wstring_view title) {
    const std::wstring normalized = normalize_title(title);
    const auto& aliases = chromium_pip_title_aliases();
    return std::binary_search(
        aliases.begin(), aliases.end(), normalized);
}

bool process_matches(std::wstring_view process_basename) {
    if (process_basename.empty()) {
        return false;
    }
    const auto slash = process_basename.find_last_of(L"/\\");
    if (slash != std::wstring_view::npos) {
        process_basename.remove_prefix(slash + 1);
    }
    return ends_with_case_insensitive(process_basename, L"chrome.exe") &&
           process_basename.size() == std::wstring_view(L"chrome.exe").size();
}

bool is_target_window(
    std::wstring_view class_name,
    std::wstring_view title,
    std::wstring_view process_basename,
    bool visible,
    bool top_level) {
    return visible && top_level &&
           class_name == L"Chrome_WidgetWin_1" &&
           process_matches(process_basename) &&
           title_matches(title);
}

std::string window_id(std::uintptr_t value) {
    if (value == 0) {
        throw PlatformError("invalid window handle");
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string result = "hwnd:";
    char reversed[sizeof(value) * 2]{};
    std::size_t count = 0;
    do {
        reversed[count++] = digits[value & 0x0fU];
        value >>= 4U;
    } while (value != 0);
    while (count > 0) {
        result.push_back(reversed[--count]);
    }
    return result;
}

std::uintptr_t parse_window_id(std::string_view value) {
    constexpr std::string_view prefix = "hwnd:";
    if (value.size() <= prefix.size() ||
        value.substr(0, prefix.size()) != prefix) {
        throw PlatformError("invalid PiP window ID");
    }
    std::uintptr_t result = 0;
    for (const char character : value.substr(prefix.size())) {
        unsigned digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<unsigned>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<unsigned>(character - 'a') + 10U;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<unsigned>(character - 'A') + 10U;
        } else {
            throw PlatformError("invalid PiP window ID");
        }
        if (result >
            (std::numeric_limits<std::uintptr_t>::max() - digit) / 16U) {
            throw PlatformError("invalid PiP window ID");
        }
        result = result * 16U + digit;
    }
    if (result == 0) {
        throw PlatformError("invalid PiP window ID");
    }
    return result;
}

bool segment_is_safe(
    Point start,
    Point end,
    MotionSize size,
    Point cursor,
    int safety_margin,
    bool allow_clearance_recovery) {
    const double left = static_cast<double>(cursor.x) - safety_margin - size.width;
    const double right = static_cast<double>(cursor.x) + safety_margin;
    const double top = static_cast<double>(cursor.y) - safety_margin - size.height;
    const double bottom = static_cast<double>(cursor.y) + safety_margin;
    const bool x_entry = segment_hits_forbidden(
        static_cast<double>(start.x), static_cast<double>(end.x), left, right);
    const bool y_entry = segment_hits_forbidden(
        static_cast<double>(start.y), static_cast<double>(end.y), top, bottom);
    if (!(x_entry && y_entry)) {
        return true;
    }
    if (!allow_clearance_recovery) {
        return false;
    }
    const Rect start_rect{
        start.x, start.y, start.x + size.width, start.y + size.height};
    const double start_clearance =
        clearance_to_rect(start_rect, cursor);
    const std::int64_t dx = std::max(
        {static_cast<std::int64_t>(start_rect.left) - cursor.x,
         std::int64_t{0},
         static_cast<std::int64_t>(cursor.x) - start_rect.right});
    const std::int64_t dy = std::max(
        {static_cast<std::int64_t>(start_rect.top) - cursor.y,
         std::int64_t{0},
         static_cast<std::int64_t>(cursor.y) - start_rect.bottom});
    return start_clearance > 0.0 &&
           dx < safety_margin &&
           dy < safety_margin &&
           segment_has_nondecreasing_pointer_clearance(
               start, end, size, cursor);
}

bool point_near_rect(Point point, const Rect& rect, int margin) {
    const int dx = std::max({rect.left - point.x, 0, point.x - rect.right});
    const int dy = std::max({rect.top - point.y, 0, point.y - rect.bottom});
    const auto squared =
        static_cast<std::int64_t>(dx) * dx +
        static_cast<std::int64_t>(dy) * dy;
    return squared <=
           static_cast<std::int64_t>(margin) * margin;
}

bool visual_capture_eligible(
    bool include_visual,
    bool paused,
    bool motion_active,
    bool mouse_buttons_down,
    const std::optional<Rect>& pip_rect,
    Point cursor) {
    if (!include_visual || paused || motion_active || mouse_buttons_down) {
        return false;
    }
    if (!pip_rect.has_value() ||
        pip_rect->left >= pip_rect->right ||
        pip_rect->top >= pip_rect->bottom) {
        return true;
    }

    const std::int64_t margin = kVisualControlMarginPixels;
    const std::int64_t x = cursor.x;
    const std::int64_t y = cursor.y;
    const bool over_controls =
        x >= static_cast<std::int64_t>(pip_rect->left) - margin &&
        x <= static_cast<std::int64_t>(pip_rect->right) + margin &&
        y >= static_cast<std::int64_t>(pip_rect->top) - margin &&
        y <= static_cast<std::int64_t>(pip_rect->bottom) + margin;
    return !over_controls;
}

std::optional<Rect> clipped_intersection(const Rect& rect, const Rect& bounds) {
    Rect result{
        std::max(rect.left, bounds.left),
        std::max(rect.top, bounds.top),
        std::min(rect.right, bounds.right),
        std::min(rect.bottom, bounds.bottom)};
    if (result.width() <= 0 || result.height() <= 0) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::size_t> manual_hit_test(
    const std::vector<ManualWindowHit>& front_to_back,
    Point point) {
    for (std::size_t index = 0; index < front_to_back.size(); ++index) {
        const ManualWindowHit& window = front_to_back[index];
        if (!window.visible || !window.top_level || window.minimized ||
            window.cloaked || window.picker_overlay ||
            window.input_transparent) {
            continue;
        }
        // A visible top-level window whose geometry could not be read still
        // blocks resolution. Falling through could select a window hidden
        // behind it.
        if (!window.rect_valid) {
            return index;
        }
        if (point.x >= window.rect.left &&
            point.x < window.rect.right &&
            point.y >= window.rect.top &&
            point.y < window.rect.bottom) {
            return index;
        }
    }
    return std::nullopt;
}

ManualWindowEligibility manual_window_eligibility(
    Rect window,
    Rect monitor,
    Rect work,
    bool maximized) {
    constexpr double kFullScreenCoverage = 0.97;
    if (window.width() <= 0 || window.height() <= 0) {
        return {false, "This window cannot be moved."};
    }
    if (maximized) {
        return {false, "Full-screen windows cannot be moved."};
    }
    const auto covers = [](const Rect& candidate, const Rect& bounds) {
        const auto overlap = clipped_intersection(candidate, bounds);
        if (!overlap.has_value() || bounds.width() <= 0 ||
            bounds.height() <= 0) {
            return false;
        }
        const double area =
            static_cast<double>(overlap->width()) * overlap->height();
        const double covered =
            area /
            (static_cast<double>(bounds.width()) * bounds.height());
        return covered >= kFullScreenCoverage;
    };
    if (covers(window, monitor) || covers(window, work)) {
        return {false, "Full-screen windows cannot be moved."};
    }
    return {true, {}};
}

Json Backend::probe() {
    Json result = capabilities();
    result["capture_performed"] = false;
    return result;
}

ManualHoverSnapshot Backend::manual_hover(Point point) {
    ManualHoverSnapshot snapshot;
    snapshot.point = point;
    snapshot.error =
        "This platform does not support manual window selection.";
    return snapshot;
}

ManualTargetStatus Backend::manual_target_status() const {
    return {};
}

bool Backend::select_manual_at(Point) {
    return false;
}

bool Backend::reset_manual_target() {
    return false;
}

void Backend::start_hotkeys(
    std::function<void()>,
    std::function<void()>) {}

void Backend::stop_hotkeys() {}

}  // namespace schiva
