#pragma once

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
std::optional<Rect> clipped_intersection(const Rect& rect, const Rect& bounds);

class Backend {
public:
    virtual ~Backend() = default;

    virtual void set_paused(bool paused) = 0;
    virtual Observation observe(double monotonic_seconds, bool include_visual) = 0;
    virtual bool begin_motion(const std::string& pip_id) = 0;
    virtual void end_motion() = 0;
    virtual bool move(const std::string& pip_id, int left, int top) = 0;
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
