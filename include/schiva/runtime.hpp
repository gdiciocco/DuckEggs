#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "schiva/platform.hpp"

namespace schiva {

inline constexpr double kMotionHz = 120.0;
inline constexpr double kIdleHz = 60.0;
inline constexpr double kPausedHz = 15.0;
inline constexpr double kControlWaitSeconds = 0.012;
inline constexpr std::size_t kTimingSamples = 240;
inline constexpr double kStatusIntervalSeconds = 1.0;
inline constexpr double kStartupTimeoutSeconds = 5.0;
inline constexpr std::string_view kVersion = "0.4.0-alpha.5";

double target_period(bool motion_active, bool paused);
double next_deadline(double previous_deadline, double work_finished, double period);

using BackendFactory = std::function<std::unique_ptr<Backend>()>;
using RuntimeSignalsFactory =
    std::function<std::unique_ptr<RuntimeSignals>()>;

struct RuntimeOptions {
    std::filesystem::path data_dir;
    bool observe_only{};
    bool no_visual{};
    bool no_gui{};
    bool start_paused{true};
    bool resume{};
    std::optional<double> duration;
    // Optional owner-thread factories for deterministic integration tests and
    // benchmarks. Empty factories use the platform backend and signals.
    BackendFactory backend_factory;
    RuntimeSignalsFactory signals_factory;
};

class StatusFile {
public:
    explicit StatusFile(std::filesystem::path data_dir);

    Json read() const;
    void write(const Json& status, bool force = false);
    const std::filesystem::path& path() const;
    const std::optional<std::string>& error() const;

private:
    std::filesystem::path data_dir_;
    std::filesystem::path path_;
    double last_write_{-1.0e300};
    std::string last_state_;
    std::optional<std::string> error_;
};

class AdaptiveApplication {
public:
    explicit AdaptiveApplication(RuntimeOptions options);
    ~AdaptiveApplication();

    AdaptiveApplication(const AdaptiveApplication&) = delete;
    AdaptiveApplication& operator=(const AdaptiveApplication&) = delete;

    int run();
    void request_pause();
    void request_quit();
    void request_insights(std::optional<std::string> profile_key = std::nullopt);
    void request_manual_picker();
    void request_manual_hover(Point point);
    void request_manual_selection(Point point);
    void request_manual_reset();
    void request_manual_picker_closed();

    Json snapshot() const;
    Json insights_snapshot() const;
    Json manual_snapshot() const;
    bool finished() const;
    int exit_code() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

int run_application(const RuntimeOptions& options);
Json probe_native();
void print_json(const Json& value);
std::string clean_error(std::string_view value);

}  // namespace schiva
