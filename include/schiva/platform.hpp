#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "schiva/core.hpp"

namespace schiva {

class PlatformError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class UnsupportedPlatformError : public PlatformError {
public:
    using PlatformError::PlatformError;
};

std::string current_platform();
std::filesystem::path default_data_dir();
std::string path_to_utf8(const std::filesystem::path& path);
std::filesystem::path path_from_utf8(std::string_view utf8);

// The first argument is the complete window title. Matching is deliberately
// exact after compatibility normalization; partial Chrome titles never match.
std::wstring normalize_title(std::wstring_view title);
bool title_matches(std::wstring_view title);
bool process_matches(std::wstring_view process_basename);
bool is_target_window(
    std::wstring_view class_name,
    std::wstring_view title,
    std::wstring_view process_basename,
    bool visible = true,
    bool top_level = true);

std::string window_id(std::uintptr_t value);
std::uintptr_t parse_window_id(std::string_view value);

struct MotionSize {
    int width{};
    int height{};
};

struct ManualHoverSnapshot {
    bool supported{};
    bool found{};
    Point point{};
    std::string title;
    Rect rect;
    bool eligible{};
    std::string reason;
    std::optional<std::string> error;
};

// One entry per top-level window in native front-to-back z-order. The shared
// hit test skips only non-visible windows, the transient picker overlay, and
// input-transparent layered overlays. Everything else in front of the pointer
// blocks resolution, including DuckEggs' own main window and the shell.
struct ManualWindowHit {
    Rect rect;
    bool visible{};
    bool top_level{};
    bool minimized{};
    bool cloaked{};
    bool own_process{};
    bool shell_or_desktop{};
    bool rect_valid{true};
    bool picker_overlay{};
    bool input_transparent{};
};

std::optional<std::size_t> manual_hit_test(
    const std::vector<ManualWindowHit>& front_to_back,
    Point point);

struct ManualWindowEligibility {
    bool eligible{};
    std::string reason;
};

// Portable geometry policy shared with the native backend. Maximized windows
// and windows covering at least 97% of their monitor or work area are treated
// as full-screen and cannot be selected or retained as a manual target.
ManualWindowEligibility manual_window_eligibility(
    Rect window,
    Rect monitor,
    Rect work,
    bool maximized);

enum class ManualTargetMode {
    Automatic,
    Manual,
};

struct ManualTargetStatus {
    bool supported{};
    ManualTargetMode mode{ManualTargetMode::Automatic};
    bool selected{};
    std::string selected_id;
    std::optional<std::string> error;
};

// Compares a motion lease's expected geometry with a freshly observed
// rectangle before a native move is applied.
inline bool motion_rect_matches(
    const Rect& expected,
    const Rect& observed,
    int tolerance = 2) {
    const int limit = tolerance < 0 ? 0 : tolerance;
    const auto within = [limit](int first, int second) {
        const int difference = first >= second ? first - second
                                               : second - first;
        return difference <= limit;
    };
    return within(expected.left, observed.left) &&
           within(expected.top, observed.top) &&
           within(expected.right, observed.right) &&
           within(expected.bottom, observed.bottom);
}

// Native-side duplicate of the small core safety predicate. It avoids a
// dependency on unagreed core internals while preserving the same corridor
// test used by the controller.
bool segment_is_safe(
    Point start,
    Point end,
    MotionSize size,
    Point cursor,
    int safety_margin = 28);
bool point_near_rect(Point point, const Rect& rect, int margin);
// Visual capture masks handle the PiP itself. Suppress capture only while the
// cursor is over its controls, including the same 20 px inner margin used by
// movement, so nearby content remains observable.
bool visual_capture_eligible(
    bool include_visual,
    bool paused,
    bool motion_active,
    bool mouse_buttons_down,
    const std::optional<Rect>& pip_rect,
    Point cursor);
std::optional<Rect> clipped_intersection(const Rect& rect, const Rect& bounds);

class Backend {
public:
    virtual ~Backend() = default;

    virtual void set_paused(bool paused) = 0;
    virtual Observation observe(double monotonic_seconds, bool include_visual) = 0;
    virtual bool begin_motion(const std::string& pip_id) = 0;
    virtual void end_motion() = 0;
    virtual bool move(const std::string& pip_id, int left, int top) = 0;
    virtual ManualHoverSnapshot manual_hover(Point point);
    virtual ManualTargetStatus manual_target_status() const;
    virtual bool select_manual_at(Point point);
    virtual bool reset_manual_target();
    virtual Json capabilities() const = 0;
    virtual Json probe();
    virtual bool python_instance_active() const = 0;
    virtual void start_hotkeys(
        std::function<void()> pause_callback,
        std::function<void()> quit_callback);
    virtual void stop_hotkeys();
    virtual void close() = 0;
};

std::unique_ptr<Backend> create_backend();
bool python_reference_active();

class RuntimeSignals {
public:
    virtual ~RuntimeSignals() = default;
    virtual bool already_running() const = 0;
    virtual bool stop_requested() const = 0;
    virtual void sign_stop() = 0;
    virtual void close() = 0;
};

std::unique_ptr<RuntimeSignals> create_runtime_signals();
bool request_stop();

struct ImportReport {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::int64_t profiles{};
    std::int64_t decision_records{};
    std::int64_t model_rows{};
    int schema_version{};
};

// Copies a validated schema-1 SQLite backup into a new, empty C++ data
// directory. The source is opened READONLY and is never modified.
ImportReport import_python_backup(
    const std::filesystem::path& source,
    const std::filesystem::path& data_dir);

// Portable, bounded latest-request/latest-result capture worker. Native
// adapters provide the capture function; its lifetime is held by the worker.
class VisualCaptureWorker {
public:
    using CaptureFunction =
        std::function<VisualFrame(const Rect&, const std::vector<Rect>&)>;

    VisualCaptureWorker(
        CaptureFunction capture,
        double interval_seconds = 0.5,
        double request_max_age_seconds = 0.5,
        double result_max_age_seconds = 0.25,
        double close_timeout_seconds = 0.25);
    ~VisualCaptureWorker();

    VisualCaptureWorker(const VisualCaptureWorker&) = delete;
    VisualCaptureWorker& operator=(const VisualCaptureWorker&) = delete;

    void invalidate(int generation);
    void pause();
    void resume();
    void set_suppressed(bool suppressed);
    bool request(int generation, const Rect& bounds, std::vector<Rect> masks);
    std::optional<VisualFrame> poll(int generation);
    std::optional<VisualFrame> wait_for_result(int generation, double timeout_seconds);
    std::optional<std::string> capture_error(int generation) const;
    bool thread_started() const;
    Json metrics() const;
    void close();

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace schiva
