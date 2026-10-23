#include "schiva/platform.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <limits>

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
        return std::filesystem::path(base) / L"SchivaAdaptiveCpp";
    }
    wchar_t temporary[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH, temporary);
    return std::filesystem::path(std::wstring(temporary, length)) /
           L"SchivaAdaptiveCpp";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" /
               "Application Support" / "SchivaCpp";
    }
    return std::filesystem::temp_directory_path() / "SchivaCpp";
#elif defined(__linux__)
    const char* state_home = std::getenv("XDG_STATE_HOME");
    if (state_home != nullptr && *state_home != '\0') {
        return std::filesystem::path(state_home) / "schiva-cpp";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state" /
               "schiva-cpp";
    }
    return std::filesystem::temp_directory_path() / "schiva-cpp";
#else
    return std::filesystem::temp_directory_path() / "schiva-cpp";
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

bool title_matches(std::wstring_view title) {
    const std::wstring normalized = normalize_title(title);
    return normalized == L"picture in picture" ||
           normalized == L"immagine nell immagine";
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
    int safety_margin) {
    const double left = static_cast<double>(cursor.x) - safety_margin - size.width;
    const double right = static_cast<double>(cursor.x) + safety_margin;
    const double top = static_cast<double>(cursor.y) - safety_margin - size.height;
    const double bottom = static_cast<double>(cursor.y) + safety_margin;
    const bool x_entry = segment_hits_forbidden(
        static_cast<double>(start.x), static_cast<double>(end.x), left, right);
    const bool y_entry = segment_hits_forbidden(
        static_cast<double>(start.y), static_cast<double>(end.y), top, bottom);
    return !(x_entry && y_entry);
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

Json Backend::probe() {
    Json result = capabilities();
    result["capture_performed"] = false;
    return result;
}

void Backend::start_hotkeys(
    std::function<void()>,
    std::function<void()>) {}

void Backend::stop_hotkeys() {}

}  // namespace schiva
