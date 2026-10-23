#include "schiva/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct FakeState {
    mutable std::mutex mutex;
    schiva::Rect pip{600, 200, 1000, 425};
    const schiva::Point cursor{510, 250};
    const schiva::Rect work{0, 0, 1920, 1080};
    std::vector<double> move_times;
    std::vector<schiva::Point> move_positions;
    double max_step{};
    int frames{};
    bool closed{};
    bool owner_thread_factory{};
};

class FakeSignals final : public schiva::RuntimeSignals {
public:
    bool already_running() const override {
        return false;
    }

    bool stop_requested() const override {
        return stop_.load();
    }

    void sign_stop() override {
        stop_.store(true);
    }

    void close() override {}

private:
    std::atomic<bool> stop_{false};
};

class FakeBackend final : public schiva::Backend {
public:
    explicit FakeBackend(std::shared_ptr<FakeState> state)
        : state_(std::move(state)) {}

    void set_paused(bool paused) override {
        paused_ = paused;
    }

    schiva::Observation observe(
        double,
        bool include_visual) override {
        (void)include_visual;
        std::lock_guard lock(state_->mutex);
        ++state_->frames;
        schiva::WindowInfo pip{
            "hwnd:1001",
            "chrome.exe",
            "Chrome_WidgetWin_1",
            state_->pip};
        schiva::Observation observation;
        observation.cursor = state_->cursor;
        observation.pip = pip;
        observation.work_area = state_->work;
        observation.windows = {pip};
        observation.input_active = false;
        observation.input_age = 10.0;
        return observation;
    }

    bool begin_motion(const std::string& pip_id) override {
        motion_active_ = pip_id == "hwnd:1001";
        return motion_active_;
    }

    void end_motion() override {
        motion_active_ = false;
    }

    bool move(
        const std::string& pip_id,
        int left,
        int top) override {
        if (!motion_active_ || pip_id != "hwnd:1001") {
            return false;
        }
        std::lock_guard lock(state_->mutex);
        const double elapsed = std::chrono::duration<double>(
            Clock::now().time_since_epoch())
                                   .count();
        const double step = std::hypot(
            static_cast<double>(left - state_->pip.left),
            static_cast<double>(top - state_->pip.top));
        state_->max_step = std::max(state_->max_step, step);
        state_->move_times.push_back(elapsed);
        state_->move_positions.push_back({left, top});
        state_->pip = state_->pip.moved_to(left, top);
        return true;
    }

    schiva::Json capabilities() const override {
        return {
            {"platform", "fake"},
            {"supported", true},
            {"backend", "deterministic benchmark"},
            {"candidate_count", 1},
            {"last_error", nullptr}};
    }

    schiva::Json probe() override {
        return capabilities();
    }

    bool python_instance_active() const override {
        return false;
    }

    void start_hotkeys(
        std::function<void()>,
        std::function<void()>) override {}

    void stop_hotkeys() override {}

    void close() override {
        std::lock_guard lock(state_->mutex);
        state_->closed = true;
    }

private:
    std::shared_ptr<FakeState> state_;
    bool paused_{};
    bool motion_active_{};
};

struct FakeSnapshot {
    schiva::Rect pip;
    std::vector<double> move_times;
    std::vector<schiva::Point> move_positions;
    double max_step{};
    int frames{};
    bool closed{};
    bool owner_thread_factory{};
};

FakeSnapshot snapshot(const FakeState& state) {
    std::lock_guard lock(state.mutex);
    return {
        state.pip,
        state.move_times,
        state.move_positions,
        state.max_step,
        state.frames,
        state.closed,
        state.owner_thread_factory};
}

std::filesystem::path temporary_data_dir() {
    const auto stamp = Clock::now().time_since_epoch().count();
    const auto thread = std::hash<std::thread::id>{}(
        std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
           ("duckeggs-runtime-benchmark-" + std::to_string(stamp) +
            "-" + std::to_string(thread));
}

}  // namespace

int main() {
    auto state = std::make_shared<FakeState>();
    const auto data_dir = temporary_data_dir();
    const auto coordinator_thread = std::this_thread::get_id();
    const double started =
        std::chrono::duration<double>(
            Clock::now().time_since_epoch())
            .count();

    schiva::RuntimeOptions options;
    options.data_dir = data_dir;
    options.no_visual = true;
    options.no_gui = true;
    options.start_paused = false;
    options.resume = true;
    options.duration = 0.85;
    options.backend_factory = [state, coordinator_thread] {
        if (std::this_thread::get_id() == coordinator_thread) {
            throw schiva::PlatformError(
                "the benchmark backend was created off its owner thread");
        }
        {
            std::lock_guard lock(state->mutex);
            state->owner_thread_factory = true;
        }
        return std::make_unique<FakeBackend>(state);
    };
    options.signals_factory = [] {
        return std::make_unique<FakeSignals>();
    };

    schiva::AdaptiveApplication application(std::move(options));
    const int exit_code = application.run();
    const FakeSnapshot result = snapshot(*state);
    const double finished =
        std::chrono::duration<double>(
            Clock::now().time_since_epoch())
            .count();

    std::vector<double> gaps;
    for (std::size_t index = 1;
         index < result.move_times.size();
         ++index) {
        gaps.push_back(
            (result.move_times[index] -
             result.move_times[index - 1]) *
            1000.0);
    }
    double gap_total = 0.0;
    double gap_max = 0.0;
    for (const double gap : gaps) {
        gap_total += gap;
        gap_max = std::max(gap_max, gap);
    }
    const std::optional<double> gap_mean =
        gaps.empty()
            ? std::nullopt
            : std::optional<double>(
                  gap_total / static_cast<double>(gaps.size()));

    schiva::Json output = {
        {"firstmove_ms",
         result.move_times.empty()
             ? schiva::Json(nullptr)
             : schiva::Json(
                   (result.move_times.front() - started) * 1000.0)},
        {"moving_gap_ms",
         {{"mean", gap_mean.has_value()
                       ? schiva::Json(*gap_mean)
                       : schiva::Json(nullptr)},
          {"max", gaps.empty() ? schiva::Json(nullptr)
                               : schiva::Json(gap_max)},
          {"samples", gaps.size()}}},
        {"frames", result.frames},
        {"moves", result.move_positions.size()},
        {"maxstep", result.max_step},
        {"finalposition",
         schiva::Json::array({result.pip.left, result.pip.top})},
        {"elapsed_ms", (finished - started) * 1000.0},
        {"database_created",
         std::filesystem::exists(data_dir / "learning.sqlite3")},
        {"fake_backend_closed", result.closed},
        {"owner_thread_factory", result.owner_thread_factory},
        {"exit_code", exit_code},
        {"timing", application.snapshot().value("timing", schiva::Json())}};

    std::error_code cleanup_error;
    std::filesystem::remove_all(data_dir, cleanup_error);

    std::cout << output.dump(2) << '\n';
    return exit_code == 0 && !result.move_positions.empty() ? 0 : 1;
}
