#include "schiva/platform.hpp"

#include "manual_window_identity.hpp"

#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace schiva {
namespace {

constexpr wchar_t kChromeClass[] = L"Chrome_WidgetWin_1";
constexpr wchar_t kPythonMutexName[] =
    L"Local\\SchivaAdaptive.Instance.v1";
constexpr wchar_t kCppMutexName[] =
    L"Local\\DuckEggs.Instance.v1";
constexpr wchar_t kCppStopEventName[] =
    L"Local\\DuckEggs.Stop.v1";
constexpr wchar_t kManualTargetPropertyPrefix[] =
    L"DuckEggs.ManualTarget.";
constexpr DWORD kDwmWindowCloaked = 14;
constexpr int kCaptureWidth = 64;
constexpr int kCaptureHeight = 36;
constexpr double kIdentityScanSeconds = 0.75;
constexpr double kMotionScanSeconds = 0.20;
constexpr double kVisualNearPipPixels = 160.0;
constexpr int kInnerMargin = 20;
constexpr int kSafetyMargin = 28;
constexpr DWORD kMaskProcessQuery = 0x1000;
constexpr UINT kMoveFlags =
    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER;

double monotonic_now() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::wstring window_title(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0 || length > 4096) {
        return {};
    }
    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(
        window, buffer.data(), static_cast<int>(buffer.size()));
    if (copied <= 0) {
        return {};
    }
    buffer.resize(static_cast<std::size_t>(
        std::min(copied, static_cast<int>(buffer.size()) - 1)));
    return buffer;
}

std::wstring window_class(HWND window) {
    std::array<wchar_t, 256> buffer{};
    const int copied =
        GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
    if (copied <= 0) {
        return {};
    }
    return std::wstring(
        buffer.data(), static_cast<std::size_t>(std::min(copied, 255)));
}

std::optional<Rect> window_rect(HWND window) {
    RECT value{};
    if (!GetWindowRect(window, &value)) {
        return std::nullopt;
    }
    Rect result{value.left, value.top, value.right, value.bottom};
    if (result.width() <= 0 || result.height() <= 0) {
        return std::nullopt;
    }
    return result;
}

bool rect_contains_point(const Rect& rect, Point point) {
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

std::uint64_t process_creation_time(HANDLE process) {
    FILETIME creation{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (!GetProcessTimes(
            process, &creation, &exit_time, &kernel_time, &user_time)) {
        return 0;
    }
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

std::wstring process_basename(HANDLE process) {
    std::wstring buffer(32768, L'\0');
    DWORD capacity = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(
            process, 0, buffer.data(), &capacity)) {
        return {};
    }
    buffer.resize(capacity);
    const auto slash = buffer.find_last_of(L"/\\");
    if (slash != std::wstring::npos) {
        buffer.erase(0, slash + 1);
    }
    return buffer;
}

bool same_point(Point first, Point second) {
    return first == second;
}

bool valid_creation_time(std::uint64_t value) {
    return value != 0;
}

bool python_mutex_present() {
    SetLastError(ERROR_SUCCESS);
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, kPythonMutexName);
    if (mutex == nullptr) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            return false;
        }
        throw PlatformError(
            "could not verify the Python instance guard (Win32 " +
            std::to_string(error) + ")");
    }
    CloseHandle(mutex);
    return true;
}

class DurationMetrics {
public:
    void add(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0.0) {
            return;
        }
        std::lock_guard lock(mutex_);
        samples_.push_back(seconds * 1000.0);
        while (samples_.size() > 64) {
            samples_.pop_front();
        }
    }

    Json snapshot() const {
        std::lock_guard lock(mutex_);
        Json result{
            {"latest", nullptr},
            {"avg", nullptr},
            {"max", nullptr},
            {"samples", 0}};
        if (samples_.empty()) {
            return result;
        }
        double total = 0.0;
        double maximum = 0.0;
        for (const double sample : samples_) {
            total += sample;
            maximum = std::max(maximum, sample);
        }
        result["latest"] = samples_.back();
        result["avg"] = total / static_cast<double>(samples_.size());
        result["max"] = maximum;
        result["samples"] = samples_.size();
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::deque<double> samples_;
};

struct ProcessIdentity {
    std::uint64_t creation_time{};
    std::wstring basename;
};

struct WindowRecord {
    HWND handle{};
    DWORD process_id{};
    std::wstring class_name;
    std::wstring title;
    std::wstring process_basename;
    Rect rect;
    bool is_target{};
    std::uint64_t creation_time{};
};

struct ManualBinding {
    WindowRecord record;
    std::string selection_id;
    HANDLE marker{};
};

struct Candidate {
    HWND handle{};
    Rect rect;
    std::wstring app_id;
    std::wstring role;
};

bool shell_or_desktop_class(std::wstring_view class_name) {
    static constexpr std::array<std::wstring_view, 7> classes{
        L"Progman",
        L"WorkerW",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"NotifyIconOverflowWindow",
        L"XamlExplorerHostIslandWindow",
        L"TopLevelWindowForOverflowXamlIsland"};
    return std::find(classes.begin(), classes.end(), class_name) !=
           classes.end();
}

bool window_is_cloaked(HWND window) {
    using DwmGetWindowAttributeFunction =
        HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
    static const auto query = []() -> DwmGetWindowAttributeFunction {
        HMODULE library = GetModuleHandleW(L"dwmapi.dll");
        if (library == nullptr) {
            library = LoadLibraryW(L"dwmapi.dll");
        }
        return library == nullptr
                   ? nullptr
                   : reinterpret_cast<DwmGetWindowAttributeFunction>(
                         GetProcAddress(library, "DwmGetWindowAttribute"));
    }();
    if (query == nullptr) {
        // Cloak state is part of manual window eligibility. Fail closed when
        // the Windows 10+ query is unavailable.
        return true;
    }
    DWORD cloaked = 0;
    return FAILED(query(
               window,
               kDwmWindowCloaked,
               &cloaked,
               static_cast<DWORD>(sizeof(cloaked)))) ||
           cloaked != 0;
}

bool window_is_input_transparent(HWND window) {
    const LONG_PTR extended_style =
        GetWindowLongPtrW(window, GWL_EXSTYLE);
    return (extended_style & WS_EX_LAYERED) != 0 &&
           (extended_style & WS_EX_TRANSPARENT) != 0;
}

bool same_motion_identity(
    const WindowRecord& lease,
    const WindowRecord& current) {
    return lease.handle == current.handle &&
           lease.process_id == current.process_id &&
           lease.class_name == current.class_name &&
           _wcsicmp(
               lease.process_basename.c_str(),
               current.process_basename.c_str()) == 0 &&
           lease.creation_time == current.creation_time;
}

std::string utf8(const std::wstring& value) {
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
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

std::string manual_hover_title(const std::wstring& title) {
    if (title.empty()) {
        return "(Untitled window)";
    }
    constexpr std::size_t kTitleLimit = 180;
    if (title.size() <= kTitleLimit) {
        return utf8(title);
    }
    std::size_t length = kTitleLimit - 3;
    if (length > 0 && title[length - 1] >= 0xD800 &&
        title[length - 1] <= 0xDBFF) {
        --length;
    }
    std::wstring shortened = title.substr(0, length) + L"...";
    return utf8(shortened);
}

std::string identity_string(const WindowRecord* record) {
    if (record == nullptr) {
        return "-";
    }
    return std::to_string(
               reinterpret_cast<std::uintptr_t>(record->handle)) +
           ":" + std::to_string(record->process_id) + ":" +
           std::to_string(record->creation_time) + ":" +
           utf8(record->class_name) + ":" +
           utf8(record->process_basename) + ":" +
           std::to_string(record->rect.left) + "," +
           std::to_string(record->rect.top) + "," +
           std::to_string(record->rect.right) + "," +
           std::to_string(record->rect.bottom);
}

std::string initialize_dpi_awareness() {
    using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const auto function = reinterpret_cast<SetContext>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (function != nullptr &&
            function(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return "per-monitor-v2";
        }
    }
    HRESULT result = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    if (result == S_OK || result == E_ACCESSDENIED) {
        return "per-monitor";
    }
    if (SetProcessDPIAware()) {
        return "system";
    }
    return "unavailable";
}

VisualFrame capture_desktop(
    const Rect& bounds,
    const std::vector<Rect>& masks) {
    if (bounds.width() <= 0 || bounds.height() <= 0) {
        throw PlatformError("desktop capture bounds are invalid");
    }
    HDC screen = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ original = nullptr;
    bool selected = false;
    std::optional<VisualFrame> output;
    try {
        screen = GetDC(nullptr);
        if (screen == nullptr) {
            throw PlatformError("screen capture access was denied");
        }
        memory = CreateCompatibleDC(screen);
        if (memory == nullptr) {
            throw PlatformError("could not create a capture device context");
        }
        bitmap = CreateCompatibleBitmap(screen, kCaptureWidth, kCaptureHeight);
        if (bitmap == nullptr) {
            throw PlatformError("could not create a capture bitmap");
        }
        original = SelectObject(memory, bitmap);
        if (original == nullptr || original == HGDI_ERROR) {
            throw PlatformError("could not select the capture bitmap");
        }
        selected = true;
        SetStretchBltMode(memory, HALFTONE);
        if (!StretchBlt(
                memory,
                0,
                0,
                kCaptureWidth,
                kCaptureHeight,
                screen,
                bounds.left,
                bounds.top,
                bounds.width(),
                bounds.height(),
                SRCCOPY)) {
            throw PlatformError(
                "desktop capture was blocked or failed; protected content "
                "may deny capture");
        }
        const HGDIOBJ restored = SelectObject(memory, original);
        if (restored == nullptr || restored == HGDI_ERROR) {
            throw PlatformError("could not release the capture bitmap");
        }
        selected = false;

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = kCaptureWidth;
        info.bmiHeader.biHeight = -kCaptureHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        std::vector<unsigned char> bgra(
            static_cast<std::size_t>(kCaptureWidth * kCaptureHeight * 4));
        const int rows = GetDIBits(
            memory,
            bitmap,
            0,
            kCaptureHeight,
            bgra.data(),
            &info,
            DIB_RGB_COLORS);
        if (rows != kCaptureHeight) {
            throw PlatformError("desktop capture returned incomplete pixels");
        }
        std::vector<std::uint8_t> gray(
            static_cast<std::size_t>(kCaptureWidth * kCaptureHeight));
        unsigned char maximum = 0;
        for (std::size_t index = 0; index < gray.size(); ++index) {
            const std::size_t offset = index * 4;
            const unsigned blue = bgra[offset];
            const unsigned green = bgra[offset + 1];
            const unsigned red = bgra[offset + 2];
            const auto value = static_cast<std::uint8_t>(
                (red * 299U + green * 587U + blue * 114U + 500U) /
                1000U);
            gray[index] = value;
            maximum = std::max(maximum, value);
        }
        if (maximum <= 3U) {
            throw PlatformError(
                "desktop capture returned a black frame; protected content "
                "or an unavailable desktop may be the cause");
        }

        std::vector<Rect> excluded;
        excluded.reserve(masks.size());
        for (const Rect& mask : masks) {
            if (const auto clipped = clipped_intersection(mask, bounds)) {
                excluded.push_back(*clipped);
            }
        }
        VisualFrame result;
        result.width = kCaptureWidth;
        result.height = kCaptureHeight;
        result.pixels = std::move(gray);
        result.bounds = bounds;
        result.excluded = std::move(excluded);
        output = std::move(result);
    } catch (...) {
        if (selected && memory != nullptr && original != nullptr) {
            SelectObject(memory, original);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            DeleteDC(memory);
        }
        if (screen != nullptr) {
            ReleaseDC(nullptr, screen);
        }
        throw;
    }
    if (selected && memory != nullptr && original != nullptr) {
        SelectObject(memory, original);
    }
    if (bitmap != nullptr) {
        DeleteObject(bitmap);
    }
    if (memory != nullptr) {
        DeleteDC(memory);
    }
    if (screen != nullptr) {
        ReleaseDC(nullptr, screen);
    }
    if (!output.has_value()) {
        throw PlatformError("desktop capture failed safely");
    }
    return std::move(*output);
}

class WindowsHotkeys {
public:
    WindowsHotkeys(
        std::function<void()> pause_callback,
        std::function<void()> quit_callback)
        : pause_callback_(std::move(pause_callback)),
          quit_callback_(std::move(quit_callback)) {}

    ~WindowsHotkeys() {
        stop();
    }

    void start() {
        if (thread_.joinable()) {
            return;
        }
        std::unique_lock lock(mutex_);
        ready_ = false;
        thread_ = std::thread([this] { run(); });
        condition_.wait_for(lock, std::chrono::seconds(2), [this] {
            return ready_;
        });
    }

    void stop() {
        if (!thread_.joinable()) {
            return;
        }
        if (thread_id_ != 0) {
            PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
        }
        thread_.join();
        thread_id_ = 0;
    }

private:
    void run() {
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        thread_id_ = GetCurrentThreadId();
        constexpr std::array<int, 2> identifiers{1, 2};
        constexpr std::array<UINT, 2> keys{'P', 'Q'};
        std::vector<int> registered;
        for (std::size_t index = 0; index < identifiers.size(); ++index) {
            if (RegisterHotKey(
                    nullptr,
                    identifiers[index],
                    MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                    keys[index])) {
                registered.push_back(identifiers[index]);
            }
        }
        {
            std::lock_guard lock(mutex_);
            ready_ = true;
        }
        condition_.notify_all();
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.message == WM_HOTKEY) {
                if (message.wParam == 1 && pause_callback_) {
                    pause_callback_();
                } else if (message.wParam == 2 && quit_callback_) {
                    quit_callback_();
                }
            } else {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        for (const int identifier : registered) {
            UnregisterHotKey(nullptr, identifier);
        }
    }

    std::function<void()> pause_callback_;
    std::function<void()> quit_callback_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable condition_;
    DWORD thread_id_{};
    bool ready_{};
};

class WindowsBackend final : public Backend {
public:
    WindowsBackend()
        : dpi_awareness_(initialize_dpi_awareness()),
          capture_worker_(
              [](const Rect& bounds, const std::vector<Rect>& masks) {
                  return capture_desktop(bounds, masks);
              }) {
        manual_property_name_ =
            kManualTargetPropertyPrefix +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(
                reinterpret_cast<std::uintptr_t>(this)) + L"." +
            std::to_wstring(static_cast<unsigned long long>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
        capture_worker_.resume();
    }

    ~WindowsBackend() override {
        close();
    }

    void set_paused(bool paused) override {
        if (paused == paused_) {
            return;
        }
        paused_ = paused;
        if (paused_) {
            capture_worker_.pause();
            invalidate_capture();
        } else {
            capture_worker_.resume();
        }
        end_motion();
        motion_ambiguous_ = false;
        last_scan_at_ = -1.0e300;
        last_observe_now_ = -1.0e300;
        last_scan_records_.clear();
        last_records_.clear();
        process_cache_.clear();
        last_real_foreground_ = nullptr;
    }

    ManualHoverSnapshot manual_hover(Point point) override {
        ManualHoverSnapshot snapshot;
        snapshot.supported = true;
        snapshot.point = point;
        if (closed_) {
            snapshot.error = "the Windows backend is closed";
            return snapshot;
        }
        try {
            const auto target = manual_target_at(point);
            if (target.has_value()) {
                snapshot.found = true;
                snapshot.title = manual_hover_title(target->record.title);
                snapshot.rect = target->record.rect;
                snapshot.eligible = target->eligibility.eligible;
                snapshot.reason = target->eligibility.reason;
            }
        } catch (const std::exception& exception) {
            snapshot.error = exception.what();
        }
        return snapshot;
    }

    ManualTargetStatus manual_target_status() const override {
        ManualTargetStatus status;
        status.supported = true;
        status.mode = manual_mode_ ? ManualTargetMode::Manual
                                   : ManualTargetMode::Automatic;
        status.selected =
            manual_mode_ && manual_binding_.has_value();
        if (manual_mode_ && manual_binding_.has_value()) {
            status.selected_id = manual_binding_->selection_id;
        }
        status.error = manual_error_;
        return status;
    }

    bool select_manual_at(Point point) override {
        if (closed_) {
            manual_error_ = "the Windows backend is closed";
            return false;
        }
        std::optional<ManualPointTarget> target;
        try {
            target = manual_target_at(point);
        } catch (const std::exception& exception) {
            manual_error_ =
                "The window under the pointer could not be validated: " +
                std::string(exception.what());
            last_error_ = manual_error_;
            return false;
        }
        if (!target.has_value()) {
            manual_error_ = "No movable window is under the pointer.";
            last_error_ = manual_error_;
            return false;
        }
        if (!target->eligibility.eligible) {
            manual_error_ = target->eligibility.reason;
            last_error_ = manual_error_;
            return false;
        }
        const WindowRecord current = target->record;
        if (manual_binding_.has_value() &&
            manual_binding_->record.handle == current.handle &&
            manual_marker_matches(current.handle) &&
            same_motion_identity(manual_binding_->record, current)) {
            manual_binding_->record = current;
            manual_binding_->selection_id =
                "manual-" + std::to_string(++manual_selection_generation_);
            manual_mode_ = true;
            manual_error_.reset();
            last_error_.reset();
            return true;
        }

        // A same-handle binding with a broken marker must be removed before
        // placing the replacement property on that window.
        if (manual_binding_.has_value() &&
            manual_binding_->record.handle == current.handle) {
            remove_manual_marker();
        }
        HANDLE marker = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (marker == nullptr) {
            manual_error_ =
                "The selected window could not be pinned safely.";
            return false;
        }
        if (GetPropW(
                current.handle,
                manual_property_name_.c_str()) != nullptr ||
            !SetPropW(
                current.handle,
                manual_property_name_.c_str(),
                marker)) {
            CloseHandle(marker);
            manual_error_ =
                "The selected window could not be pinned safely.";
            return false;
        }
        if (GetPropW(
                current.handle,
                manual_property_name_.c_str()) != marker) {
            // The property is no longer ours, so removal would be unsafe.
            CloseHandle(marker);
            manual_error_ =
                "The selected window could not be pinned safely.";
            return false;
        }

        remove_manual_marker();
        manual_binding_ = ManualBinding{
            current,
            "manual-" + std::to_string(++manual_selection_generation_),
            marker};
        manual_mode_ = true;
        manual_error_.reset();
        last_error_.reset();
        invalidate_capture();
        return true;
    }

    bool reset_manual_target() override {
        if (closed_) {
            return false;
        }
        remove_manual_marker();
        manual_mode_ = false;
        manual_error_.reset();
        last_error_.reset();
        invalidate_capture();
        return true;
    }

    bool python_instance_active() const override {
        return python_mutex_present();
    }

    Observation observe(double now, bool include_visual) override {
        if (closed_) {
            throw PlatformError("the Windows backend is closed");
        }
        const auto records = records_for_observe(now);
        std::optional<Candidate> candidate;
        if (manual_mode_) {
            if (const auto current = read_manual_identity()) {
                candidate = Candidate{
                    current->handle,
                    current->rect,
                    L"manual-window",
                    L"window"};
            }
        } else {
            auto candidates = candidates_from(records);
            if (candidates.size() > 1U) {
                if (motion_cancelled_this_observe_ || motion_ambiguous_) {
                    candidates.clear();
                } else {
                    throw PlatformError(
                        "found multiple Chrome PiP windows; close extras so "
                        "a unique target can be selected");
                }
            }
            if (candidates.size() == 1U) {
                candidate = candidates.front();
            }
            if (motion_ambiguous_ && candidate.has_value()) {
                motion_ambiguous_ = false;
            }
        }

        const Point cursor = read_cursor();
        const bool buttons = buttons_down();
        HWND foreground_handle = GetForegroundWindow();
        const WindowRecord* foreground_record =
            find_record(records, foreground_handle);
        const DWORD own_pid = GetCurrentProcessId();
        const bool foreground_is_helper =
            foreground_record != nullptr &&
            foreground_record->process_id == own_pid;
        const bool foreground_is_pip =
            foreground_record != nullptr &&
            (foreground_record->is_target ||
             (manual_mode_ && manual_binding_.has_value() &&
              foreground_record->handle ==
                  manual_binding_->record.handle));
        if (foreground_record != nullptr && !foreground_is_helper &&
            !foreground_is_pip) {
            last_real_foreground_ = foreground_record->handle;
        }
        foreground_record = find_record(records, last_real_foreground_);
        if (foreground_record == nullptr &&
            (foreground_is_helper || foreground_is_pip)) {
            foreground_record = nullptr;
        }

        Point monitor_point = cursor;
        if (candidate.has_value()) {
            monitor_point = Point{
                candidate->rect.left + candidate->rect.width() / 2,
                candidate->rect.top + candidate->rect.height() / 2};
        } else if (foreground_record != nullptr) {
            monitor_point = Point{
                foreground_record->rect.left +
                    foreground_record->rect.width() / 2,
                foreground_record->rect.top +
                    foreground_record->rect.height() / 2};
        }
        const Rect work = monitor_work_area(monitor_point);

        std::vector<Rect> interaction_exclusions;
        std::vector<WindowInfo> windows;
        windows.reserve(records.size());
        for (const auto& record : records) {
            windows.push_back(window_info(record));
            if (record.process_id == own_pid) {
                interaction_exclusions.push_back(record.rect);
            }
        }

        const double age = input_age();
        const bool cursor_changed =
            last_cursor_.has_value() && !same_point(*last_cursor_, cursor);
        const bool button_changed =
            last_cursor_.has_value() && buttons != last_buttons_;
        const bool input_active =
            age <= 0.75 || cursor_changed || button_changed;
        last_cursor_ = cursor;
        last_buttons_ = buttons;
        last_input_age_ = age;

        std::optional<VisualFrame> visual;
        const bool visual_near_pip =
            candidate.has_value() &&
            point_near_rect(
                cursor, candidate->rect,
                static_cast<int>(kVisualNearPipPixels));
        if (include_visual && !paused_ && !motion_active() && !buttons &&
            !visual_near_pip) {
            capture_worker_.set_suppressed(false);
            const auto [generation, masks] = prepare_capture(
                work, records, candidate, foreground_record);
            if (auto frame = capture_worker_.poll(generation)) {
                capture_error_.reset();
                visual = std::move(frame);
            } else if (const auto error =
                           capture_worker_.capture_error(generation)) {
                capture_error_ = error;
            }
            (void)capture_worker_.request(generation, work, masks);
        } else {
            capture_worker_.set_suppressed(true);
        }

        Observation observation;
        observation.cursor = cursor;
        observation.buttons_down = buttons;
        if (candidate.has_value()) {
            observation.pip = WindowInfo{
                window_id(reinterpret_cast<std::uintptr_t>(
                    candidate->handle)),
                utf8(candidate->app_id),
                utf8(candidate->role),
                candidate->rect};
        }
        if (foreground_record != nullptr) {
            observation.foreground = window_info(*foreground_record);
        }
        observation.work_area = work;
        observation.windows = std::move(windows);
        observation.visual_frame = std::move(visual);
        observation.caret = std::nullopt;
        observation.input_active = input_active;
        observation.input_age = age;
        observation.interaction_exclusions =
            std::move(interaction_exclusions);
        return observation;
    }

    bool begin_motion(const std::string& pip_id) override {
        if (closed_ || paused_) {
            last_error_ = "movement cannot start while paused or closed";
            return false;
        }
        if (python_instance_active()) {
            last_error_ =
                "resume blocked: the Python Schiva instance is still running";
            return false;
        }
        std::uintptr_t requested_value = 0;
        try {
            requested_value = parse_window_id(pip_id);
        } catch (const PlatformError&) {
            last_error_ = "invalid PiP window ID";
            return false;
        }
        capture_worker_.set_suppressed(true);
        invalidate_capture();
        if (manual_mode_) {
            const auto current = read_manual_identity();
            if (!current.has_value() ||
                reinterpret_cast<std::uintptr_t>(current->handle) !=
                    requested_value) {
                if (current.has_value()) {
                    fail_motion(
                        "the manual PiP window changed before movement");
                }
                return false;
            }
            motion_lease_ = *current;
            motion_cancelled_this_observe_ = false;
            motion_ambiguous_ = false;
            last_error_.reset();
            return true;
        }
        std::vector<WindowRecord> records;
        if (!last_scan_records_.empty() &&
            std::abs(last_scan_at_ - last_observe_now_) < 1.0e-9) {
            records = last_scan_records_;
        } else {
            records = snapshot_records();
        }
        const auto candidates = candidates_from(records);
        if (candidates.size() != 1U) {
            fail_motion(
                candidates.empty()
                    ? "the target PiP window is no longer available"
                    : "the target PiP window is no longer uniquely available",
                candidates.size() > 1U);
            return false;
        }
        if (reinterpret_cast<std::uintptr_t>(candidates.front().handle) !=
            requested_value) {
            fail_motion("the PiP window changed before movement");
            return false;
        }
        const WindowRecord* target =
            find_record(records, candidates.front().handle);
        if (target == nullptr || !valid_creation_time(target->creation_time)) {
            fail_motion(
                "the PiP process identity could not be validated safely");
            return false;
        }
        motion_lease_ = *target;
        motion_cancelled_this_observe_ = false;
        motion_ambiguous_ = false;
        last_error_.reset();
        return true;
    }

    void end_motion() override {
        motion_lease_.reset();
        motion_cancelled_this_observe_ = false;
    }

    bool move(const std::string& pip_id, int left, int top) override {
        if (closed_ || paused_) {
            last_error_ = "movement is unavailable while paused or closed";
            return false;
        }
        if (python_instance_active()) {
            fail_motion(
                "movement stopped because the Python Schiva instance is active");
            return false;
        }
        std::uintptr_t requested = 0;
        try {
            requested = parse_window_id(pip_id);
        } catch (const PlatformError&) {
            last_error_ = "invalid PiP window ID";
            return false;
        }
        if (motion_lease_.has_value() &&
            reinterpret_cast<std::uintptr_t>(motion_lease_->handle) !=
                requested) {
            fail_motion("the PiP window changed before the move");
            return false;
        }
        if (!motion_lease_.has_value() && !begin_motion(pip_id)) {
            return false;
        }
        const Rect expected_rect = motion_lease_->rect;
        const auto validated = read_motion_identity();
        if (!validated.has_value() || !motion_lease_.has_value()) {
            return false;
        }
        const Rect& current = validated->rect;
        if (!motion_rect_matches(expected_rect, current)) {
            fail_motion("manual PiP movement cancelled the motion lease");
            return false;
        }
        const Point cursor = read_cursor();
        if (buttons_down()) {
            fail_motion("mouse-button hold cancelled movement");
            return false;
        }
        if (point_near_rect(cursor, current, kInnerMargin)) {
            fail_motion(
                "the pointer entered the PiP controls before the move");
            return false;
        }
        if (!segment_is_safe(
                Point{current.left, current.top},
                Point{left, top},
                MotionSize{current.width(), current.height()},
                cursor,
                kSafetyMargin)) {
            fail_motion("the pointer entered the PiP motion corridor");
            return false;
        }
        Point destination_monitor{
            left + current.width() / 2,
            top + current.height() / 2};
        Rect work;
        try {
            work = monitor_work_area(destination_monitor);
        } catch (...) {
            fail_motion("the destination work area could not be validated");
            return false;
        }
        if (left < work.left || top < work.top ||
            left + current.width() > work.right ||
            top + current.height() > work.bottom) {
            fail_motion("the requested move leaves the work area");
            return false;
        }
        if (manual_mode_ && !manual_marker_matches(validated->handle)) {
            fail_motion(
                "the manual window lifetime marker changed before the move");
            return false;
        }
        if (!SetWindowPos(
                validated->handle,
                nullptr,
                left,
                top,
                0,
                0,
                kMoveFlags)) {
            fail_motion(
                "SetWindowPos failed (Win32 " +
                std::to_string(GetLastError()) + ")");
            return false;
        }
        WindowRecord moved = *validated;
        moved.rect = Rect{
            left, top, left + current.width(), top + current.height()};
        motion_lease_ = moved;
        replace_record(last_scan_records_, moved);
        replace_record(last_records_, moved);
        last_cursor_ = cursor;
        last_buttons_ = false;
        last_error_.reset();
        return true;
    }

    Json capabilities() const override {
        Json capture_metrics = capture_worker_.metrics();
        const ManualTargetStatus manual = manual_target_status();
        const int candidates =
            manual.mode == ManualTargetMode::Manual
                ? manual.selected ? 1 : 0
                : count_targets(last_records_);
        return {
            {"platform", "windows"},
            {"supported", true},
            {"backend", "Win32 + GDI"},
            {"dpi_awareness", dpi_awareness_},
            {"window_identity",
             "chrome.exe / Chrome_WidgetWin_1 / exact localized PiP title"},
            {"manual_selection_supported", true},
            {"candidate_count", candidates},
            {"input_activity",
             "GetLastInputInfo + cursor/button transitions"},
            {"caret", "unavailable (no UI Automation assumed)"},
            {"visual_available",
             !capture_error_.has_value() &&
                 capture_metrics["capture_duration_ms"]["samples"]
                         .get<int>() > 0},
            {"capture_error", capture_error_ ? Json(*capture_error_)
                                              : Json(nullptr)},
            {"last_error", last_error_ ? Json(*last_error_) : Json(nullptr)},
            {"visual_size", Json::array({kCaptureWidth, kCaptureHeight})},
            {"visual_max_hz", 2},
            {"capture_duration_ms",
             capture_metrics["capture_duration_ms"]},
            {"full_scan_duration_ms", full_scan_metrics_.snapshot()},
            {"motion_active", motion_active()},
            {"paused", paused_},
            {"python_instance_active", python_instance_active()},
            {"notes",
             "Occluded content, protected video and the area beneath the PiP "
             "cannot be observed."}};
    }

    Json probe() override {
        const auto records = snapshot_records();
        last_records_ = records;
        Json result = capabilities();
        const auto candidates = candidates_from(records);
        result["candidate_count"] = candidates.size();
        result["ambiguous_pip"] = candidates.size() > 1U;
        result["visible_top_level_windows"] = records.size();
        result["capture_performed"] = false;
        return result;
    }

    void start_hotkeys(
        std::function<void()> pause_callback,
        std::function<void()> quit_callback) override {
        if (hotkeys_ == nullptr) {
            hotkeys_ = std::make_unique<WindowsHotkeys>(
                std::move(pause_callback), std::move(quit_callback));
            hotkeys_->start();
        }
    }

    void stop_hotkeys() override {
        if (hotkeys_ != nullptr) {
            hotkeys_->stop();
            hotkeys_.reset();
        }
    }

    void close() override {
        if (closed_) {
            return;
        }
        stop_hotkeys();
        capture_worker_.close();
        invalidate_capture();
        end_motion();
        remove_manual_marker();
        manual_mode_ = false;
        manual_error_.reset();
        last_records_.clear();
        last_scan_records_.clear();
        process_cache_.clear();
        closed_ = true;
    }

private:
    struct EnumerationContext {
        WindowsBackend* backend{};
        std::vector<WindowRecord>* records{};
        std::unordered_set<DWORD>* live_pids{};
        std::exception_ptr failure;
        DWORD own_pid{};
    };

    static BOOL CALLBACK enum_window(HWND window, LPARAM parameter) {
        auto* context =
            reinterpret_cast<EnumerationContext*>(parameter);
        try {
            if (!IsWindowVisible(window) ||
                GetAncestor(window, GA_ROOT) != window) {
                return TRUE;
            }
            const auto rect = window_rect(window);
            if (!rect.has_value()) {
                return TRUE;
            }
            DWORD process_id = 0;
            GetWindowThreadProcessId(window, &process_id);
            if (process_id == 0) {
                return TRUE;
            }
            context->live_pids->insert(process_id);
            const auto identity =
                context->backend->lookup_process(process_id);
            const std::wstring basename =
                identity.has_value() ? identity->basename : L"";
            const std::wstring class_name = window_class(window);
            const std::wstring title = window_title(window);
            bool target = is_target_window(
                class_name, title, basename, true, true);
            if (process_id == context->own_pid) {
                target = false;
            }
            context->records->push_back(WindowRecord{
                window,
                process_id,
                class_name,
                title,
                basename,
                *rect,
                target,
                identity.has_value() ? identity->creation_time : 0});
            return TRUE;
        } catch (...) {
            context->failure = std::current_exception();
            return FALSE;
        }
    }

    struct ManualPointTarget {
        WindowRecord record;
        ManualWindowEligibility eligibility;
    };

    struct ManualMonitorGeometry {
        Rect monitor;
        Rect work;
    };

    struct ManualHitEntry {
        HWND handle{};
        ManualWindowHit hit;
    };

    struct ManualEnumerationContext {
        std::vector<ManualHitEntry>* entries{};
        std::exception_ptr failure;
        DWORD own_pid{};
    };

    static BOOL CALLBACK enum_manual_window(
        HWND window,
        LPARAM parameter) {
        auto* context =
            reinterpret_cast<ManualEnumerationContext*>(parameter);
        try {
            const std::wstring class_name = window_class(window);
            const auto rect = window_rect(window);
            DWORD process_id = 0;
            GetWindowThreadProcessId(window, &process_id);
            context->entries->push_back(ManualHitEntry{
                window,
                ManualWindowHit{
                    rect.value_or(Rect{}),
                    IsWindowVisible(window) != FALSE,
                    GetAncestor(window, GA_ROOT) == window,
                    IsIconic(window) != FALSE,
                    window_is_cloaked(window),
                    process_id != 0 && process_id == context->own_pid,
                    !class_name.empty() &&
                        shell_or_desktop_class(class_name),
                    rect.has_value(),
                    process_id != 0 &&
                        process_id == context->own_pid &&
                        class_name == detail::kManualPickerClass,
                    window_is_input_transparent(window)}});
            return TRUE;
        } catch (...) {
            context->failure = std::current_exception();
            return FALSE;
        }
    }

    std::vector<ManualHitEntry> snapshot_manual_hits() {
        std::vector<ManualHitEntry> entries;
        ManualEnumerationContext context{
            &entries, nullptr, GetCurrentProcessId()};
        if (!EnumWindows(
                &WindowsBackend::enum_manual_window,
                reinterpret_cast<LPARAM>(&context))) {
            if (context.failure) {
                std::rethrow_exception(context.failure);
            }
            throw PlatformError(
                "could not enumerate windows for manual selection (Win32 " +
                std::to_string(GetLastError()) + ")");
        }
        return entries;
    }

    std::optional<ManualPointTarget> manual_target_at(Point point) {
        struct CurrentWindow {
            DWORD process_id{};
            std::wstring class_name;
            Rect rect;
            bool own_process{};
            bool shell_or_desktop{};
            bool picker_overlay{};
            bool input_transparent{};
        };
        const DWORD own_pid = GetCurrentProcessId();
        const auto read_current =
            [own_pid](HWND handle, Point current_point)
            -> std::optional<CurrentWindow> {
            if (!IsWindow(handle) || !IsWindowVisible(handle) ||
                IsIconic(handle) ||
                GetAncestor(handle, GA_ROOT) != handle ||
                window_is_cloaked(handle)) {
                return std::nullopt;
            }
            const auto rect = window_rect(handle);
            if (!rect.has_value() ||
                !rect_contains_point(*rect, current_point)) {
                return std::nullopt;
            }
            DWORD process_id = 0;
            GetWindowThreadProcessId(handle, &process_id);
            if (process_id == 0) {
                return std::nullopt;
            }
            const std::wstring class_name = window_class(handle);
            if (class_name.empty()) {
                return std::nullopt;
            }
            const bool own_process = process_id == own_pid;
            return CurrentWindow{
                process_id,
                class_name,
                *rect,
                own_process,
                shell_or_desktop_class(class_name),
                own_process &&
                    class_name == detail::kManualPickerClass,
                window_is_input_transparent(handle)};
        };
        const auto blocked_target =
            [](HWND handle, std::string reason) {
            DWORD process_id = 0;
            GetWindowThreadProcessId(handle, &process_id);
            const auto rect = window_rect(handle).value_or(Rect{});
            return ManualPointTarget{
                WindowRecord{
                    handle,
                    process_id,
                    window_class(handle),
                    window_title(handle),
                    L"",
                    rect,
                    false,
                    0},
                {false, std::move(reason)}};
        };

        for (int attempt = 0; attempt < 2; ++attempt) {
            const std::vector<ManualHitEntry> entries =
                snapshot_manual_hits();
            if (entries.empty()) {
                return std::nullopt;
            }
            std::vector<ManualWindowHit> hits;
            hits.reserve(entries.size());
            for (const ManualHitEntry& entry : entries) {
                hits.push_back(entry.hit);
            }
            const auto index = manual_hit_test(hits, point);
            if (!index.has_value()) {
                return std::nullopt;
            }

            const HWND handle = entries[*index].handle;
            const auto current = read_current(handle, point);
            if (!current.has_value()) {
                if (attempt == 0) {
                    continue;
                }
                return blocked_target(
                    handle, "This window could not be validated safely.");
            }
            if (current->picker_overlay || current->input_transparent) {
                // The snapshot can race as the picker or a click-through
                // overlay moves. Re-resolve once against the current stack.
                if (attempt == 0) {
                    continue;
                }
                return std::nullopt;
            }
            if (current->own_process) {
                return blocked_target(
                    handle,
                    "DuckEggs' own interface cannot be selected.");
            }
            if (current->shell_or_desktop) {
                return blocked_target(
                    handle,
                    "The Windows desktop or taskbar cannot be selected.");
            }

            const auto process = lookup_process(current->process_id);
            const std::wstring title = window_title(handle);
            WindowRecord record{
                handle,
                current->process_id,
                current->class_name,
                title,
                process.has_value() ? process->basename : L"",
                current->rect,
                false,
                process.has_value() ? process->creation_time : 0};
            const auto geometry = monitor_geometry(current->rect);
            ManualWindowEligibility eligibility =
                manual_window_eligibility(
                    current->rect,
                    geometry.monitor,
                    geometry.work,
                    IsZoomed(handle) != FALSE);
            if (!process.has_value() ||
                !valid_creation_time(process->creation_time) ||
                process->basename.empty()) {
                eligibility = {
                    false, "This window cannot be selected safely."};
            }
            // Identity and monitor queries can yield, so require the same
            // live state and geometry once more before exposing the record
            // for binding.
            const auto final_state = read_current(handle, point);
            const bool changed =
                !final_state.has_value() ||
                final_state->process_id != current->process_id ||
                final_state->class_name != current->class_name ||
                final_state->rect != current->rect ||
                final_state->input_transparent !=
                    current->input_transparent;
            if (changed) {
                if (attempt == 0) {
                    continue;
                }
                return blocked_target(
                    handle, "This window could not be validated safely.");
            }
            return ManualPointTarget{record, eligibility};
        }
        return std::nullopt;
    }

    std::optional<ProcessIdentity> lookup_process(DWORD process_id) {
        if (process_id == 0) {
            return std::nullopt;
        }
        HANDLE process =
            OpenProcess(kMaskProcessQuery, FALSE, process_id);
        if (process == nullptr) {
            process_cache_.erase(process_id);
            return std::nullopt;
        }
        struct HandleGuard {
            HANDLE value;
            ~HandleGuard() {
                if (value != nullptr) {
                    CloseHandle(value);
                }
            }
        } guard{process};
        const std::uint64_t creation = process_creation_time(process);
        const auto cached = process_cache_.find(process_id);
        if (cached != process_cache_.end() && creation != 0 &&
            cached->second.creation_time == creation) {
            return cached->second;
        }
        const std::wstring basename = process_basename(process);
        if (creation != 0) {
            ProcessIdentity identity{creation, basename};
            process_cache_[process_id] = identity;
            return identity;
        }
        process_cache_.erase(process_id);
        return ProcessIdentity{0, basename};
    }

    std::vector<WindowRecord> snapshot_records() {
        const double started = monotonic_now();
        std::vector<WindowRecord> records;
        std::unordered_set<DWORD> live_pids;
        EnumerationContext context{
            this, &records, &live_pids, nullptr, GetCurrentProcessId()};
        if (!EnumWindows(&WindowsBackend::enum_window,
                         reinterpret_cast<LPARAM>(&context))) {
            if (context.failure) {
                std::rethrow_exception(context.failure);
            }
            throw PlatformError(
                "could not enumerate windows (Win32 " +
                std::to_string(GetLastError()) + ")");
        }
        for (auto iterator = process_cache_.begin();
             iterator != process_cache_.end();) {
            if (!live_pids.contains(iterator->first)) {
                iterator = process_cache_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        full_scan_metrics_.add(monotonic_now() - started);
        return records;
    }

    std::optional<WindowRecord> refresh_record(
        const WindowRecord& record,
        HWND foreground) const {
        if (!IsWindow(record.handle) ||
            !IsWindowVisible(record.handle) ||
            GetAncestor(record.handle, GA_ROOT) != record.handle) {
            return std::nullopt;
        }
        const auto rect = window_rect(record.handle);
        if (!rect.has_value()) {
            return std::nullopt;
        }
        DWORD process_id = 0;
        GetWindowThreadProcessId(record.handle, &process_id);
        if (process_id == 0) {
            return std::nullopt;
        }
        WindowRecord result = record;
        result.process_id = process_id;
        result.rect = *rect;
        const bool refresh_identity =
            process_id != record.process_id || record.is_target ||
            record.class_name == kChromeClass ||
            record.handle == foreground;
        if (!refresh_identity) {
            return result;
        }
        result.class_name = window_class(record.handle);
        const std::wstring title = window_title(record.handle);
        result.title = title;
        if (process_id != record.process_id || title_matches(title) ||
            record.is_target) {
            // lookup_process mutates the cache, so it is invoked through the
            // non-const owner path below.
            auto* mutable_this = const_cast<WindowsBackend*>(this);
            const auto identity = mutable_this->lookup_process(process_id);
            result.process_basename =
                identity.has_value() ? identity->basename : L"";
            result.creation_time =
                identity.has_value() ? identity->creation_time : 0;
        }
        result.is_target = is_target_window(
            result.class_name,
            title,
            result.process_basename,
            true,
            true);
        if (process_id == GetCurrentProcessId()) {
            result.is_target = false;
        }
        return result;
    }

    std::vector<WindowRecord> records_for_observe(double now) {
        last_observe_now_ = now;
        motion_cancelled_this_observe_ = false;
        const bool moving = motion_active();
        const double interval =
            moving ? kMotionScanSeconds : kIdentityScanSeconds;
        const bool must_scan =
            now < last_scan_at_ || now - last_scan_at_ >= interval;
        if (must_scan) {
            std::vector<WindowRecord> records = snapshot_records();
            last_scan_at_ = now;
            if (moving && motion_lease_.has_value()) {
                const HWND leased_handle = motion_lease_->handle;
                bool current_valid = false;
                bool remove_lease_record = false;
                const WindowRecord* current = nullptr;
                if (manual_mode_) {
                    const auto validated = read_motion_identity();
                    current_valid = validated.has_value();
                    remove_lease_record = !current_valid;
                    if (validated.has_value()) {
                        motion_lease_ = *validated;
                    }
                } else {
                    const auto candidates = candidates_from(records);
                    current = candidates.size() == 1U
                                  ? find_record(
                                        records,
                                        candidates.front().handle)
                                  : nullptr;
                    current_valid =
                        current != nullptr &&
                        same_motion_identity(*motion_lease_, *current);
                    remove_lease_record = current == nullptr;
                    if (!current_valid) {
                        fail_motion(
                            "movement stopped because the PiP target was no "
                            "longer uniquely valid",
                            candidates.size() > 1U);
                    }
                }
                if (!current_valid) {
                    if (remove_lease_record) {
                        records.erase(
                            std::remove_if(
                                records.begin(),
                                records.end(),
                                [leased_handle](const WindowRecord& item) {
                                    return item.handle == leased_handle;
                                }),
                            records.end());
                    }
                } else {
                    if (!manual_mode_) {
                        motion_lease_ = *current;
                    }
                }
            }
            last_scan_records_ = records;
            last_records_ = records;
            return records;
        }

        const HWND foreground = GetForegroundWindow();
        std::vector<WindowRecord> records;
        records.reserve(last_scan_records_.size());
        if (moving && motion_lease_.has_value()) {
            std::unordered_set<HWND> critical;
            if (foreground != nullptr) {
                critical.insert(foreground);
            }
            if (last_real_foreground_ != nullptr) {
                critical.insert(last_real_foreground_);
            }
            critical.erase(motion_lease_->handle);
            const DWORD own_pid = GetCurrentProcessId();
            for (const auto& record : last_scan_records_) {
                if (record.process_id == own_pid) {
                    critical.insert(record.handle);
                }
            }
            for (const auto& record : last_scan_records_) {
                if (record.handle == motion_lease_->handle) {
                    continue;
                }
                if (critical.contains(record.handle)) {
                    if (auto refreshed =
                            refresh_record(record, foreground)) {
                        records.push_back(std::move(*refreshed));
                    }
                } else {
                    records.push_back(record);
                }
            }
            if (const auto validated = read_motion_identity()) {
                motion_lease_ = *validated;
                replace_record(records, *validated);
            }
        } else {
            for (const auto& record : last_scan_records_) {
                if (auto refreshed =
                        refresh_record(record, foreground)) {
                    records.push_back(std::move(*refreshed));
                }
            }
        }
        last_scan_records_ = records;
        last_records_ = records;
        return records;
    }

    static std::vector<Candidate> candidates_from(
        const std::vector<WindowRecord>& records) {
        std::vector<Candidate> result;
        for (const auto& record : records) {
            if (record.is_target) {
                result.push_back(Candidate{
                    record.handle,
                    record.rect,
                    record.process_basename,
                    record.class_name});
            }
        }
        return result;
    }

    static WindowRecord* find_record(
        std::vector<WindowRecord>& records,
        HWND handle) {
        for (auto& record : records) {
            if (record.handle == handle) {
                return &record;
            }
        }
        return nullptr;
    }

    static const WindowRecord* find_record(
        const std::vector<WindowRecord>& records,
        HWND handle) {
        for (const auto& record : records) {
            if (record.handle == handle) {
                return &record;
            }
        }
        return nullptr;
    }

    static int count_targets(const std::vector<WindowRecord>& records) {
        return static_cast<int>(std::count_if(
            records.begin(),
            records.end(),
            [](const WindowRecord& record) { return record.is_target; }));
    }

    static WindowInfo window_info(const WindowRecord& record) {
        return WindowInfo{
            window_id(reinterpret_cast<std::uintptr_t>(record.handle)),
            utf8(record.process_basename),
            utf8(record.class_name),
            record.rect};
    }

    static Point read_cursor() {
        POINT point{};
        if (!GetCursorPos(&point)) {
            throw PlatformError("could not read the cursor");
        }
        return Point{point.x, point.y};
    }

    static bool buttons_down() {
        constexpr std::array<int, 5> keys{
            VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        for (const int key : keys) {
            if ((GetAsyncKeyState(key) & 0x8000) != 0) {
                return true;
            }
        }
        return false;
    }

    static double input_age() {
        LASTINPUTINFO info{};
        info.cbSize = sizeof(info);
        if (!GetLastInputInfo(&info)) {
            return 0.0;
        }
        const DWORD current = static_cast<DWORD>(GetTickCount64());
        const DWORD elapsed = current - info.dwTime;
        return static_cast<double>(elapsed) / 1000.0;
    }

    static Rect monitor_work_area(Point point) {
        POINT native_point{point.x, point.y};
        HMONITOR monitor =
            MonitorFromPoint(native_point, MONITOR_DEFAULTTONEAREST);
        if (monitor == nullptr) {
            throw PlatformError("could not find the current monitor");
        }
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            throw PlatformError("could not read monitor geometry");
        }
        return Rect{
            info.rcWork.left,
            info.rcWork.top,
            info.rcWork.right,
            info.rcWork.bottom};
    }

    static ManualMonitorGeometry monitor_geometry(Rect window) {
        RECT native_rect{
            window.left, window.top, window.right, window.bottom};
        const HMONITOR monitor = MonitorFromRect(
            &native_rect, MONITOR_DEFAULTTONEAREST);
        if (monitor == nullptr) {
            throw PlatformError("could not find the window monitor");
        }
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            throw PlatformError("could not read monitor geometry");
        }
        return {
            Rect{
                info.rcMonitor.left,
                info.rcMonitor.top,
                info.rcMonitor.right,
                info.rcMonitor.bottom},
            Rect{
                info.rcWork.left,
                info.rcWork.top,
                info.rcWork.right,
                info.rcWork.bottom}};
    }

    bool motion_active() const {
        return motion_lease_.has_value();
    }

    void fail_motion(std::string message, bool ambiguous = false) {
        last_error_ = std::move(message);
        motion_lease_.reset();
        motion_cancelled_this_observe_ = true;
        motion_ambiguous_ = motion_ambiguous_ || ambiguous;
    }

    bool manual_marker_matches(HWND handle) const {
        return manual_binding_.has_value() &&
               manual_binding_->marker != nullptr &&
               IsWindow(handle) &&
               GetPropW(handle, manual_property_name_.c_str()) ==
                   manual_binding_->marker;
    }

    void remove_manual_marker() {
        if (!manual_binding_.has_value()) {
            return;
        }
        const HWND handle = manual_binding_->record.handle;
        if (IsWindow(handle) && manual_marker_matches(handle)) {
            RemovePropW(handle, manual_property_name_.c_str());
        }
        if (manual_binding_->marker != nullptr) {
            CloseHandle(manual_binding_->marker);
        }
        manual_binding_.reset();
    }

    void fail_manual(std::string message) {
        remove_manual_marker();
        motion_lease_.reset();
        manual_error_ = std::move(message);
        last_error_ = manual_error_;
    }

    std::optional<WindowRecord> read_manual_identity() {
        if (!manual_mode_ || !manual_binding_.has_value()) {
            manual_error_ =
                "The selected manual window is no longer available.";
            return std::nullopt;
        }
        const ManualBinding binding = *manual_binding_;
        const HWND handle = binding.record.handle;
        if (!IsWindow(handle)) {
            fail_manual("The selected manual window was closed.");
            return std::nullopt;
        }
        if (!IsWindowVisible(handle) || IsIconic(handle) ||
            GetAncestor(handle, GA_ROOT) != handle ||
            window_is_cloaked(handle)) {
            fail_manual(
                "The selected manual window is no longer visible and "
                "restorable.");
            return std::nullopt;
        }
        if (!manual_marker_matches(handle)) {
            fail_manual(
                "The selected manual window lifetime marker changed.");
            return std::nullopt;
        }
        DWORD process_id = 0;
        GetWindowThreadProcessId(handle, &process_id);
        if (process_id == 0) {
            fail_manual(
                "The selected manual window identity is unavailable.");
            return std::nullopt;
        }
        const std::wstring class_name = window_class(handle);
        const std::wstring title = window_title(handle);
        const auto process = lookup_process(process_id);
        const Rect rect = window_rect(handle).value_or(Rect{});
        if (!process.has_value() || process->basename.empty() ||
            !valid_creation_time(process->creation_time) ||
            rect.width() <= 0 || rect.height() <= 0) {
            fail_manual(
                "The selected manual window identity could not be "
                "validated.");
            return std::nullopt;
        }
        const auto geometry = monitor_geometry(rect);
        const ManualWindowEligibility eligibility =
            manual_window_eligibility(
                rect,
                geometry.monitor,
                geometry.work,
                IsZoomed(handle) != FALSE);
        if (!eligibility.eligible) {
            fail_manual(eligibility.reason);
            return std::nullopt;
        }
        const detail::ManualWindowIdentity expected{
            reinterpret_cast<std::uintptr_t>(binding.record.handle),
            static_cast<std::uint32_t>(binding.record.process_id),
            binding.record.creation_time,
            binding.record.class_name,
            binding.record.process_basename,
            reinterpret_cast<std::uintptr_t>(binding.marker)};
        const detail::ManualWindowIdentity current{
            reinterpret_cast<std::uintptr_t>(handle),
            static_cast<std::uint32_t>(process_id),
            process->creation_time,
            class_name,
            process->basename,
            reinterpret_cast<std::uintptr_t>(
                GetPropW(handle, manual_property_name_.c_str()))};
        if (!detail::manual_window_identity_matches(expected, current)) {
            fail_manual(
                "The selected manual window identity changed.");
            return std::nullopt;
        }
        WindowRecord result = binding.record;
        result.process_id = process_id;
        result.class_name = class_name;
        result.title = title;
        result.process_basename = process->basename;
        result.creation_time = process->creation_time;
        result.rect = rect;
        result.is_target = false;
        return result;
    }

    std::optional<WindowRecord> read_motion_identity() {
        if (!motion_lease_.has_value()) {
            last_error_ = "no PiP motion validation lease is active";
            return std::nullopt;
        }
        const WindowRecord lease = *motion_lease_;
        if (manual_mode_) {
            const auto current = read_manual_identity();
            if (!current.has_value()) {
                motion_lease_.reset();
                motion_cancelled_this_observe_ = true;
                return std::nullopt;
            }
            if (current->handle != lease.handle ||
                !same_motion_identity(lease, *current)) {
                fail_motion(
                    "the manual PiP window changed during movement");
                return std::nullopt;
            }
            return current;
        }
        if (!IsWindow(lease.handle)) {
            fail_motion("the PiP window disappeared during movement");
            return std::nullopt;
        }
        if (!IsWindowVisible(lease.handle)) {
            fail_motion("the PiP window became hidden during movement");
            return std::nullopt;
        }
        if (GetAncestor(lease.handle, GA_ROOT) != lease.handle) {
            fail_motion("the PiP window is no longer top-level");
            return std::nullopt;
        }
        DWORD process_id = 0;
        GetWindowThreadProcessId(lease.handle, &process_id);
        const std::wstring class_name = window_class(lease.handle);
        const std::wstring title = window_title(lease.handle);
        if (process_id != lease.process_id ||
            class_name != lease.class_name ||
            class_name != kChromeClass ||
            !process_matches(lease.process_basename) ||
            !title_matches(title)) {
            fail_motion(
                "the PiP window identity changed during movement");
            return std::nullopt;
        }
        const auto identity = lookup_process(process_id);
        if (!identity.has_value() ||
            !valid_creation_time(identity->creation_time) ||
            identity->creation_time != lease.creation_time ||
            _wcsicmp(
                identity->basename.c_str(),
                lease.process_basename.c_str()) != 0) {
            fail_motion(
                "the PiP process identity changed during movement");
            return std::nullopt;
        }
        const auto rect = window_rect(lease.handle);
        if (!rect.has_value()) {
            fail_motion("the PiP window geometry became invalid");
            return std::nullopt;
        }
        WindowRecord current = lease;
        current.rect = *rect;
        return current;
    }

    std::pair<int, std::vector<Rect>> prepare_capture(
        const Rect& work,
        const std::vector<WindowRecord>& records,
        const std::optional<Candidate>& candidate,
        const WindowRecord* foreground) {
        std::vector<Rect> masks;
        if (candidate.has_value()) {
            masks.push_back(candidate->rect);
        }
        const DWORD own_pid = GetCurrentProcessId();
        for (const auto& record : records) {
            if (record.process_id == own_pid) {
                masks.push_back(record.rect);
            }
        }
        const WindowRecord* pip_record = candidate.has_value()
            ? find_record(records, candidate->handle)
            : nullptr;
        std::string key =
            identity_string(foreground) + "|" + identity_string(pip_record);
        for (const Rect& mask : masks) {
            key += "|" + std::to_string(mask.left) + "," +
                   std::to_string(mask.top) + "," +
                   std::to_string(mask.right) + "," +
                   std::to_string(mask.bottom);
        }
        key += "|" + std::to_string(work.left) + "," +
               std::to_string(work.top) + "," +
               std::to_string(work.right) + "," +
               std::to_string(work.bottom);
        if (!capture_key_.has_value() || *capture_key_ != key) {
            invalidate_capture();
            capture_key_ = key;
        }
        return {capture_generation_, std::move(masks)};
    }

    void invalidate_capture() {
        ++capture_generation_;
        capture_key_.reset();
        capture_error_.reset();
        capture_worker_.invalidate(capture_generation_);
    }

    static void replace_record(
        std::vector<WindowRecord>& records,
        const WindowRecord& replacement) {
        for (auto& record : records) {
            if (record.handle == replacement.handle) {
                record = replacement;
                return;
            }
        }
        records.push_back(replacement);
    }

    std::string dpi_awareness_;
    bool paused_{};
    bool closed_{};
    HWND last_real_foreground_{};
    std::optional<Point> last_cursor_;
    bool last_buttons_{};
    double last_input_age_{};
    double last_scan_at_{-1.0e300};
    double last_observe_now_{-1.0e300};
    std::vector<WindowRecord> last_scan_records_;
    std::vector<WindowRecord> last_records_;
    std::map<DWORD, ProcessIdentity> process_cache_;
    std::wstring manual_property_name_;
    unsigned long long manual_selection_generation_{};
    std::optional<ManualBinding> manual_binding_;
    bool manual_mode_{};
    std::optional<std::string> manual_error_;
    std::optional<WindowRecord> motion_lease_;
    bool motion_cancelled_this_observe_{};
    bool motion_ambiguous_{};
    std::optional<std::string> last_error_;
    std::optional<std::string> capture_error_;
    int capture_generation_{};
    std::optional<std::string> capture_key_;
    VisualCaptureWorker capture_worker_;
    DurationMetrics full_scan_metrics_;
    std::unique_ptr<WindowsHotkeys> hotkeys_;
};

class WindowsSignals final : public RuntimeSignals {
public:
    WindowsSignals() {
        SetLastError(ERROR_SUCCESS);
        mutex_ = CreateMutexW(nullptr, FALSE, kCppMutexName);
        if (mutex_ == nullptr) {
            throw PlatformError(
                "could not create the C++ instance mutex (Win32 " +
                std::to_string(GetLastError()) + ")");
        }
        already_running_ = GetLastError() == ERROR_ALREADY_EXISTS;
        if (already_running_) {
            return;
        }
        stop_event_ = CreateEventW(nullptr, TRUE, FALSE, kCppStopEventName);
        if (stop_event_ == nullptr) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
            throw PlatformError(
                "could not create the C++ stop event (Win32 " +
                std::to_string(GetLastError()) + ")");
        }
        ResetEvent(stop_event_);
    }

    ~WindowsSignals() override {
        close();
    }

    bool already_running() const override {
        return already_running_;
    }

    bool stop_requested() const override {
        return stop_event_ == nullptr ||
               WaitForSingleObject(stop_event_, 0) == WAIT_OBJECT_0;
    }

    void sign_stop() override {
        if (stop_event_ != nullptr) {
            SetEvent(stop_event_);
        }
    }

    void close() override {
        if (stop_event_ != nullptr) {
            CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }
        if (mutex_ != nullptr) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
        }
    }

private:
    HANDLE mutex_{};
    HANDLE stop_event_{};
    bool already_running_{};
};

bool python_reference_active_windows() {
    return python_mutex_present();
}

}  // namespace

std::unique_ptr<Backend> create_backend() {
    return std::make_unique<WindowsBackend>();
}

bool python_reference_active() {
    return python_reference_active_windows();
}

std::unique_ptr<RuntimeSignals> create_runtime_signals() {
    return std::make_unique<WindowsSignals>();
}

bool request_stop() {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kCppStopEventName);
    if (event == nullptr) {
        return false;
    }
    const bool signaled = SetEvent(event) != FALSE;
    CloseHandle(event);
    return signaled;
}

}  // namespace schiva
