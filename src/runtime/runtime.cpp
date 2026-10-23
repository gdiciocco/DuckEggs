#include "schiva/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <regex>
#include <sstream>
#include <thread>
#include <utility>

#include "schiva/heatmap.hpp"
#include "ui.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace schiva {
namespace {

using Clock = std::chrono::steady_clock;

double now_seconds() {
    return std::chrono::duration<double>(
               Clock::now().time_since_epoch())
        .count();
}

std::string iso_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm value{};
#ifdef _WIN32
    gmtime_s(&value, &time);
#else
    gmtime_r(&time, &value);
#endif
    std::ostringstream stream;
    stream << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string safe_error(std::string_view value) {
    static const std::regex safe_pattern(
        R"(^[A-Za-z0-9_. :()\-,';/]{1,300}$)");
    std::string text(value);
    if (text.empty()) {
        return "operation failed";
    }
    if (!std::regex_match(text, safe_pattern)) {
        return "error";
    }
    return text;
}

std::string json_error(const std::exception& exception) {
    return safe_error(exception.what());
}

class TimingWindow {
public:
    void add(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0.0) {
            return;
        }
        samples_.push_back(seconds * 1000.0);
        while (samples_.size() > kTimingSamples) {
            samples_.pop_front();
        }
    }

    Json snapshot() const {
        if (samples_.empty()) {
            return {
                {"last", nullptr},
                {"avg", nullptr},
                {"max", nullptr},
                {"samples", 0}};
        }
        double total = 0.0;
        double maximum = 0.0;
        for (const double value : samples_) {
            total += value;
            maximum = std::max(maximum, value);
        }
        return {
            {"last", samples_.back()},
            {"avg", total / static_cast<double>(samples_.size())},
            {"max", maximum},
            {"samples", samples_.size()}};
    }

private:
    std::deque<double> samples_;
};

class TimingRecorder {
public:
    void begin_tick(double started, double expected_period) {
        if (last_started_.has_value()) {
            const double cadence = started - *last_started_;
            cadence_.add(cadence);
            if (cadence > expected_period * 1.5) {
                ++overruns_;
            }
        }
        last_started_ = started;
    }

    void finish_tick(double elapsed, double expected_period) {
        tick_.add(elapsed);
        if (elapsed > expected_period * 1.5) {
            ++overruns_;
        }
    }

    Json snapshot(bool motion_active, bool paused) {
        const double target = paused ? kPausedHz
            : motion_active ? kMotionHz : kIdleHz;
        return {
            {"target_hz", target},
            {"observe_ms", observe_.snapshot()},
            {"controller_ms", controller_.snapshot()},
            {"move_ms", move_.snapshot()},
            {"tick_ms", tick_.snapshot()},
            {"cadence_ms", cadence_.snapshot()},
            {"overruns", overruns_},
            {"deferred_visual", deferred_visual_},
            {"deferred_insights", deferred_insights_}};
    }

    TimingWindow observe_;
    TimingWindow controller_;
    TimingWindow move_;
    TimingWindow tick_;
    TimingWindow cadence_;
    long long overruns_{};
    long long deferred_visual_{};
    long long deferred_insights_{};

private:
    std::optional<double> last_started_;
};

class AdaptiveLoop {
public:
    AdaptiveLoop(
        Backend& backend,
        Controller& controller,
        bool observe_only,
        bool no_visual)
        : backend_(backend),
          controller_(controller),
          observe_only_(observe_only),
          no_visual_(no_visual) {}

    void set_paused(bool paused) {
        controller_.set_paused(paused);
        backend_.set_paused(paused);
    }

    void request_pause() {
        set_paused(!controller_.paused());
    }

    Json tick(double now) {
        const double tick_started = now_seconds();
        const double expected_period = 1.0 / timing_target_hz_;
        timing_.begin_tick(tick_started, expected_period);
        const bool initially_paused = controller_.paused();
        backend_.set_paused(initially_paused);

        Json core_status = controller_status();
        if (!initially_paused && backend_.python_instance_active()) {
            if (motion_active_from_status(core_status)) {
                controller_.movement_failed();
            }
            backend_.end_motion();
            controller_.set_paused(true);
            backend_.set_paused(true);
            last_error_ =
                "resume blocked: the Python Schiva instance is still running";
            core_status = controller_status();
        }

        const bool paused = controller_.paused();
        const bool motion_before = motion_active_from_status(core_status);
        if (!motion_before) {
            end_backend_motion();
        } else if (!backend_motion_active_) {
            const auto pip_id = observation_pip_id(last_observation_);
            if (pip_id.has_value() && !begin_backend_motion(*pip_id)) {
                controller_.movement_failed();
            }
        }

        const bool input_held =
            last_observation_.has_value() &&
            last_observation_->buttons_down;
        const bool defer_committed_motion =
            motion_before &&
            (motion_moving_ || !last_decision_.has_value());
        const bool defer_visual =
            !no_visual_ && !paused &&
            (input_held || defer_committed_motion);
        if (defer_visual) {
            ++timing_.deferred_visual_;
        }
        const bool include_visual =
            !no_visual_ && !paused && !input_held &&
            !defer_committed_motion;

        const double observe_started = now_seconds();
        Observation observation =
            backend_.observe(now, include_visual);
        timing_.observe_.add(now_seconds() - observe_started);
        last_observation_ = observation;

        const double controller_started = now_seconds();
        Decision decision = controller_.tick(now, observation);
        timing_.controller_.add(now_seconds() - controller_started);
        last_decision_ = decision;
        core_status = controller_status();

        bool can_move = decision.move_to.has_value() &&
                        decision.pip_id.has_value() &&
                        !observe_only_ &&
                        !controller_.paused() &&
                        !observation.buttons_down;
        if (can_move && !backend_motion_active_) {
            if (!begin_backend_motion(*decision.pip_id)) {
                controller_.movement_failed();
                end_backend_motion();
                can_move = false;
            }
        }
        if (can_move) {
            ++moves_attempted_;
            const double move_started = now_seconds();
            const bool moved = backend_.move(
                *decision.pip_id,
                decision.move_to->x,
                decision.move_to->y);
            timing_.move_.add(now_seconds() - move_started);
            if (!moved) {
                last_error_ = backend_error(
                    "the PiP window could not be moved");
                controller_.movement_failed();
                end_backend_motion();
            } else {
                last_error_.reset();
            }
        }

        core_status = controller_status();
        const bool core_has_motion = core_status.contains("motion_active");
        const bool current_paused = controller_.paused();
        legacy_motion_active_ = core_has_motion
            ? core_status["motion_active"].is_boolean() &&
                  core_status["motion_active"].get<bool>()
            : !observe_only_ && !current_paused &&
                  decision.move_to.has_value();
        const bool motion_active =
            motion_active_from_status(core_status);
        if (!motion_active) {
            end_backend_motion();
        }
        motion_active_ = motion_active;
        motion_moving_ = motion_active && decision.move_to.has_value();
        const double final_period =
            target_period(motion_active, current_paused);
        timing_.finish_tick(
            now_seconds() - tick_started, final_period);
        timing_target_hz_ = 1.0 / final_period;

        Json status = make_status(
            observation,
            decision,
            core_status,
            motion_active);
        return status;
    }

    bool motion_active() const {
        return motion_active_;
    }

    bool motion_moving() const {
        return motion_moving_;
    }

    void note_deferred_insights() {
        ++timing_.deferred_insights_;
    }

    void close() {
        end_backend_motion();
    }

private:
    Json controller_status() const {
        try {
            Json status = controller_.status();
            if (status.is_object()) {
                return status;
            }
        } catch (...) {
        }
        return Json::object();
    }

    bool motion_active_from_status(const Json& status) const {
        if (status.contains("motion_active") &&
            status["motion_active"].is_boolean()) {
            return status["motion_active"].get<bool>();
        }
        return legacy_motion_active_;
    }

    static std::optional<std::string> observation_pip_id(
        const std::optional<Observation>& observation) {
        if (!observation.has_value() ||
            !observation->pip.has_value()) {
            return std::nullopt;
        }
        return observation->pip->window_id;
    }

    std::string backend_error(std::string fallback) const {
        try {
            const Json capabilities = backend_.capabilities();
            if (capabilities.contains("last_error") &&
                capabilities["last_error"].is_string()) {
                return capabilities["last_error"].get<std::string>();
            }
        } catch (...) {
        }
        return fallback;
    }

    bool begin_backend_motion(const std::string& pip_id) {
        if (backend_motion_active_) {
            return motion_pip_id_ == pip_id;
        }
        bool established = false;
        try {
            established = backend_.begin_motion(pip_id);
        } catch (...) {
            established = false;
        }
        if (!established) {
            last_error_ = backend_error(
                "the PiP target could not be validated for movement");
            return false;
        }
        backend_motion_active_ = true;
        motion_pip_id_ = pip_id;
        last_error_.reset();
        return true;
    }

    void end_backend_motion() {
        if (!backend_motion_active_) {
            return;
        }
        try {
            backend_.end_motion();
        } catch (...) {
        }
        backend_motion_active_ = false;
        motion_pip_id_.reset();
    }

    Json make_status(
        const Observation& observation,
        const Decision& decision,
        const Json& core_status,
        bool motion_active) {
        Json foreground = nullptr;
        if (observation.foreground.has_value()) {
            foreground = {
                {"app_id", observation.foreground->app_id},
                {"role", observation.foreground->role}};
        }
        const Json capabilities = backend_.capabilities();
        Json native_timing = Json::object();
        for (const char* name :
             {"capture_duration_ms", "full_scan_duration_ms"}) {
            if (capabilities.contains(name)) {
                native_timing[name] = capabilities[name];
            }
        }
        const bool paused = controller_.paused();
        const bool visual_available =
            core_status.contains("visual_available")
                ? core_status["visual_available"].is_boolean() &&
                      core_status["visual_available"].get<bool>()
                : observation.visual_frame.has_value();
        Json status = {
            {"mode", "adaptive"},
            {"paused", paused},
            {"observe_only", observe_only_},
            {"state",
             core_status.contains("state")
                 ? core_status["state"]
                 : Json(decision.state)},
            {"learning_count",
             core_status.contains("learning_count")
                 ? core_status["learning_count"]
                 : Json(decision.learning_count)},
            {"context_key",
             core_status.contains("context_key")
                 ? core_status["context_key"]
                 : decision.context_key.has_value()
                       ? Json(*decision.context_key)
                       : Json(nullptr)},
            {"visual_available", visual_available},
            {"capture_error",
             capabilities.contains("capture_error")
                 ? capabilities["capture_error"]
                 : Json(nullptr)},
            {"candidate_count",
             capabilities.value("candidate_count", 0)},
            {"native_timing", std::move(native_timing)},
            {"motion_active", motion_active},
            {"foreground", std::move(foreground)},
            {"last_error",
             last_error_.has_value() ? Json(*last_error_) : Json(nullptr)},
            {"timing", timing_.snapshot(motion_active, paused)}};
        return status;
    }

    Backend& backend_;
    Controller& controller_;
    bool observe_only_;
    bool no_visual_;
    std::optional<Observation> last_observation_;
    std::optional<Decision> last_decision_;
    std::optional<std::string> last_error_;
    bool legacy_motion_active_{};
    bool backend_motion_active_{};
    std::optional<std::string> motion_pip_id_;
    bool motion_active_{};
    bool motion_moving_{};
    long long moves_attempted_{};
    TimingRecorder timing_;
    double timing_target_hz_{kIdleHz};
};

}  // namespace

double target_period(bool motion_active, bool paused) {
    if (paused) {
        return 1.0 / kPausedHz;
    }
    return 1.0 / (motion_active ? kMotionHz : kIdleHz);
}

double next_deadline(
    double previous_deadline,
    double work_finished,
    double period) {
    if (!std::isfinite(work_finished) || !std::isfinite(previous_deadline) ||
        period <= 0.0) {
        throw std::invalid_argument(
            "deadline inputs must be finite and positive");
    }
    double candidate = previous_deadline + period;
    if (candidate <= work_finished) {
        const double missed =
            std::floor((work_finished - candidate) / period) + 1.0;
        candidate += missed * period;
        while (candidate <= work_finished) {
            candidate += period;
        }
    }
    return candidate;
}

StatusFile::StatusFile(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir)),
      path_(data_dir_ / "status.json") {}

Json StatusFile::read() const {
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        throw PlatformError("status is not available yet; start Schiva once");
    }
    Json result;
    try {
        stream >> result;
    } catch (const std::exception&) {
        throw PlatformError("status file is not valid JSON");
    }
    if (!result.is_object()) {
        throw PlatformError("status file does not contain a JSON object");
    }
    return result;
}

void StatusFile::write(const Json& status, bool force) {
    const std::string state =
        status.contains("state") && status["state"].is_string()
            ? status["state"].get<std::string>()
            : "";
    const double current = now_seconds();
    if (!force && state == last_state_ &&
        current - last_write_ < kStatusIntervalSeconds) {
        return;
    }
    try {
        std::filesystem::create_directories(data_dir_);
        Json payload = status;
        if (!payload.contains("timestamp")) {
            payload["timestamp"] = iso_timestamp();
        }
        static std::atomic<unsigned long long> serial{};
        const auto process_id =
#ifdef _WIN32
            static_cast<unsigned long long>(GetCurrentProcessId());
#else
            static_cast<unsigned long long>(
                reinterpret_cast<std::uintptr_t>(&serial));
#endif
        const auto temporary = path_.parent_path() /
            (path_.filename().string() + ".tmp-" +
             std::to_string(process_id) + "-" +
             std::to_string(serial.fetch_add(1)));
        {
            std::ofstream stream(
                temporary, std::ios::binary | std::ios::trunc);
            if (!stream) {
                throw PlatformError("status write failed");
            }
            stream << payload.dump(2) << '\n';
            stream.flush();
            if (!stream) {
                throw PlatformError("status write failed");
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(
                temporary.c_str(),
                path_.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary);
            throw PlatformError("status write failed");
        }
#else
        std::error_code error;
        std::filesystem::rename(temporary, path_, error);
        if (error) {
            std::filesystem::remove(temporary);
            throw PlatformError("status write failed");
        }
#endif
        last_write_ = current;
        last_state_ = state;
        error_.reset();
    } catch (const std::exception&) {
        error_ = "status write failed";
    }
}

const std::filesystem::path& StatusFile::path() const {
    return path_;
}

const std::optional<std::string>& StatusFile::error() const {
    return error_;
}

class AdaptiveApplication::Impl {
public:
    explicit Impl(RuntimeOptions options)
        : options_(std::move(options)),
          status_file_(options_.data_dir),
          snapshot_({
              {"mode", "adaptive"},
              {"paused", options_.start_paused},
              {"state", "starting"},
              {"learning_count", 0},
              {"visual_available", false},
              {"last_error", nullptr}}),
          insights_({
              {"data", nullptr},
              {"error", nullptr},
              {"requested_profile", nullptr},
              {"revision", 0}}) {}

    ~Impl() {
        shutdown();
    }

    int run() {
        try {
            start();
        } catch (const std::exception& exception) {
            std::cerr << "Schiva Adattivo C++: "
                      << safe_error(exception.what()) << '\n';
            shutdown();
            return 2;
        }

        bool running_ui = false;
        if (!options_.no_gui) {
            running_ui = run_platform_ui(*owner_);
            if (!running_ui) {
                std::cerr
                    << "Schiva Adattivo C++: the control panel could not "
                       "start; continuing without it\n";
            }
        }
        if (!running_ui) {
            if (worker_.joinable()) {
                worker_.join();
            }
        } else {
            request_quit();
            if (worker_.joinable()) {
                worker_.join();
            }
        }
        const int result = exit_code_.load();
        shutdown();
        return result;
    }

    void request_pause() {
        {
            std::lock_guard lock(control_mutex_);
            controls_.push(Control::Pause);
        }
        control_condition_.notify_all();
    }

    void request_quit() {
        {
            std::lock_guard lock(control_mutex_);
            controls_.push(Control::Quit);
            quit_.store(true);
        }
        control_condition_.notify_all();
        if (signals_ != nullptr) {
            signals_->sign_stop();
        }
    }

    void request_insights(std::optional<std::string> profile_key) {
        std::lock_guard lock(insights_mutex_);
        insights_requested_ = true;
        insights_requested_profile_ = std::move(profile_key);
    }

    Json snapshot() const {
        std::lock_guard lock(snapshot_mutex_);
        return snapshot_;
    }

    Json insights_snapshot() const {
        std::lock_guard lock(insights_mutex_);
        return insights_snapshot_;
    }

    bool finished() const {
        return finished_.load();
    }

    int exit_code() const {
        return exit_code_.load();
    }

    void owner(AdaptiveApplication* owner) {
        owner_ = owner;
    }

private:
    enum class Control { Pause, Quit };

    void start() {
        if (!options_.start_paused && python_reference_active()) {
            throw PlatformError(
                "resume blocked: the Python Schiva instance is still running");
        }
        signals_ = create_runtime_signals();
        if (signals_->already_running()) {
            throw PlatformError(
                "another Schiva Adattivo C++ instance is already running");
        }
        worker_ = std::thread([this] { worker_main(); });
        std::unique_lock lock(ready_mutex_);
        if (!ready_condition_.wait_for(
                lock,
                std::chrono::duration<double>(kStartupTimeoutSeconds),
                [this] { return ready_; })) {
            quit_.store(true);
            if (signals_ != nullptr) {
                signals_->sign_stop();
            }
            lock.unlock();
            if (worker_.joinable()) {
                worker_.join();
            }
            throw PlatformError("the adaptive worker did not become ready");
        }
        if (startup_error_.has_value()) {
            const std::string error = *startup_error_;
            lock.unlock();
            if (worker_.joinable()) {
                worker_.join();
            }
            throw PlatformError(error);
        }
    }

    void worker_main() {
        const double started = now_seconds();
        bool failed = false;
        try {
            backend_ = create_backend();
            if (!options_.start_paused) {
                if (backend_->python_instance_active()) {
                    throw PlatformError(
                        "resume blocked: the Python Schiva instance is still "
                        "running");
                }
            }
            std::filesystem::create_directories(options_.data_dir);
            store_ = std::make_unique<LearningStore>(
                options_.data_dir / "learning.sqlite3");
            controller_ = std::make_unique<Controller>(
                *store_, options_.observe_only);
            controller_->set_paused(options_.start_paused);
            loop_ = std::make_unique<AdaptiveLoop>(
                *backend_,
                *controller_,
                options_.observe_only,
                options_.no_visual);
            backend_->start_hotkeys(
                [this] { request_pause(); },
                [this] { request_quit(); });

            Json initial = {
                {"mode", "adaptive"},
                {"paused", controller_->paused()},
                {"observe_only", options_.observe_only},
                {"state", "starting"},
                {"learning_count", 0},
                {"visual_available", false},
                {"foreground", nullptr},
                {"last_error", nullptr}};
            publish(initial);
            {
                std::lock_guard lock(ready_mutex_);
                ready_ = true;
            }
            ready_condition_.notify_all();

            double deadline = now_seconds();
            while (!quit_.load()) {
                if (signals_->stop_requested()) {
                    break;
                }
                if (apply_controls()) {
                    break;
                }
                process_insights_request();
                const double now = now_seconds();
                if (options_.duration.has_value() &&
                    now - started >= *options_.duration) {
                    break;
                }
                Json status = loop_->tick(now);
                publish(status);
                const double finished = now_seconds();
                const double period = target_period(
                    loop_->motion_active(),
                    controller_->paused());
                deadline = next_deadline(deadline, finished, period);
                wait_until_deadline(deadline);
            }
        } catch (const std::exception& exception) {
            failed = true;
            const std::string error = safe_error(exception.what());
            Json failed_status = snapshot();
            failed_status["state"] = "error";
            failed_status["last_error"] = error;
            publish(failed_status);
            if (ready_.load()) {
                std::cerr << "Schiva Adattivo C++: " << error << '\n';
            } else {
                std::lock_guard lock(ready_mutex_);
                startup_error_ = error;
            }
            exit_code_.store(1);
        } catch (...) {
            failed = true;
            const std::string error = "internal error";
            if (!ready_.load()) {
                std::lock_guard lock(ready_mutex_);
                startup_error_ = error;
            }
            exit_code_.store(1);
        }

        const bool final_paused =
            controller_ != nullptr ? controller_->paused() : true;
        close_worker_resources();
        Json final = snapshot();
        final["state"] = failed ? "error" : "stopped";
        final["paused"] = final_paused;
        status_file_.write(final, true);
        {
            std::lock_guard lock(snapshot_mutex_);
            snapshot_ = final;
        }
        finished_.store(true);
        {
            std::lock_guard lock(ready_mutex_);
            ready_ = true;
        }
        ready_condition_.notify_all();
    }

    void close_worker_resources() {
        if (loop_ != nullptr) {
            try {
                loop_->close();
            } catch (...) {
                exit_code_.store(exit_code_.load() == 0 ? 1 : exit_code_.load());
            }
            loop_.reset();
        }
        if (controller_ != nullptr) {
            try {
                controller_->close();
            } catch (...) {
                exit_code_.store(exit_code_.load() == 0 ? 1 : exit_code_.load());
            }
            controller_.reset();
        }
        if (store_ != nullptr) {
            try {
                store_->close();
            } catch (...) {
                exit_code_.store(exit_code_.load() == 0 ? 1 : exit_code_.load());
            }
            store_.reset();
        }
        if (backend_ != nullptr) {
            try {
                backend_->stop_hotkeys();
            } catch (...) {
            }
            try {
                backend_->close();
            } catch (...) {
            }
            backend_.reset();
        }
    }

    void publish(const Json& status) {
        Json published = status;
        if (status_file_.error().has_value()) {
            published["last_error"] = *status_file_.error();
        }
        {
            std::lock_guard lock(snapshot_mutex_);
            snapshot_ = published;
        }
        status_file_.write(published);
    }

    bool apply_controls() {
        for (;;) {
            Control control;
            {
                std::lock_guard lock(control_mutex_);
                if (controls_.empty()) {
                    return false;
                }
                control = controls_.front();
                controls_.pop();
            }
            if (control == Control::Quit) {
                quit_.store(true);
                if (signals_ != nullptr) {
                    signals_->sign_stop();
                }
                return true;
            }
            if (control == Control::Pause && loop_ != nullptr) {
                loop_->request_pause();
            }
        }
    }

    void process_insights_request() {
        if (loop_ != nullptr && loop_->motion_moving()) {
            loop_->note_deferred_insights();
            return;
        }
        bool requested = false;
        std::optional<std::string> profile_key;
        {
            std::lock_guard lock(insights_mutex_);
            if (!insights_requested_) {
                return;
            }
            requested = true;
            insights_requested_ = false;
            profile_key = std::move(insights_requested_profile_);
            insights_requested_profile_.reset();
        }
        if (!requested || controller_ == nullptr) {
            return;
        }
        try {
            Json data = controller_->insights(profile_key);
            if (!data.is_object()) {
                throw PlatformError(
                    "learning data does not have a valid format");
            }
            publish_insights(profile_key, data, std::nullopt);
        } catch (const std::exception& exception) {
            publish_insights(
                profile_key,
                std::nullopt,
                "Impossibile aggiornare le mappe (" +
                    json_error(exception) + ").");
        }
    }

    void publish_insights(
        const std::optional<std::string>& profile_key,
        const std::optional<Json>& data,
        const std::optional<std::string>& error) {
        std::lock_guard lock(insights_mutex_);
        ++insights_revision_;
        insights_snapshot_ = {
            {"data", data.has_value() ? *data : Json(nullptr)},
            {"error", error.has_value() ? Json(*error) : Json(nullptr)},
            {"requested_profile",
             profile_key.has_value() ? Json(*profile_key) : Json(nullptr)},
            {"revision", insights_revision_}};
    }

    void wait_until_deadline(double deadline) {
        while (!quit_.load()) {
            if (signals_->stop_requested()) {
                return;
            }
            {
                std::lock_guard lock(control_mutex_);
                if (!controls_.empty()) {
                    return;
                }
            }
            const double remaining = deadline - now_seconds();
            if (remaining <= 0.0) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::duration<double>(
                std::min(remaining, kControlWaitSeconds)));
        }
    }

    void shutdown() {
        quit_.store(true);
        if (signals_ != nullptr) {
            signals_->sign_stop();
        }
        if (worker_.joinable()) {
            if (worker_.get_id() != std::this_thread::get_id()) {
                worker_.join();
            } else {
                worker_.detach();
            }
        }
        if (signals_ != nullptr) {
            signals_->close();
            signals_.reset();
        }
    }

    RuntimeOptions options_;
    AdaptiveApplication* owner_{};
    StatusFile status_file_;
    std::unique_ptr<RuntimeSignals> signals_;
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<LearningStore> store_;
    std::unique_ptr<Controller> controller_;
    std::unique_ptr<AdaptiveLoop> loop_;
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> ready_{false};
    std::atomic<int> exit_code_{0};
    std::mutex ready_mutex_;
    std::condition_variable ready_condition_;
    std::optional<std::string> startup_error_;
    std::mutex control_mutex_;
    std::condition_variable control_condition_;
    std::queue<Control> controls_;

    mutable std::mutex snapshot_mutex_;
    Json snapshot_;

    mutable std::mutex insights_mutex_;
    bool insights_requested_{};
    std::optional<std::string> insights_requested_profile_;
    unsigned long long insights_revision_{};
    Json insights_snapshot_;
};

AdaptiveApplication::AdaptiveApplication(RuntimeOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {
    impl_->owner(this);
}

AdaptiveApplication::~AdaptiveApplication() = default;

int AdaptiveApplication::run() {
    return impl_->run();
}

void AdaptiveApplication::request_pause() {
    impl_->request_pause();
}

void AdaptiveApplication::request_quit() {
    impl_->request_quit();
}

void AdaptiveApplication::request_insights(
    std::optional<std::string> profile_key) {
    impl_->request_insights(std::move(profile_key));
}

Json AdaptiveApplication::snapshot() const {
    return impl_->snapshot();
}

Json AdaptiveApplication::insights_snapshot() const {
    return impl_->insights_snapshot();
}

bool AdaptiveApplication::finished() const {
    return impl_->finished();
}

int AdaptiveApplication::exit_code() const {
    return impl_->exit_code();
}

int run_application(const RuntimeOptions& options) {
    if (options.duration.has_value() &&
        (!std::isfinite(*options.duration) || *options.duration <= 0.0)) {
        throw std::invalid_argument(
            "--duration must be a finite positive number");
    }
    AdaptiveApplication application(options);
    return application.run();
}

Json probe_native() {
    auto backend = create_backend();
    Json result = backend->probe();
    backend->close();
    result["capture_performed"] = false;
    return result;
}

void print_json(const Json& value) {
    std::cout << value.dump(2) << '\n';
}

std::string clean_error(std::string_view value) {
    return safe_error(value);
}

}  // namespace schiva
