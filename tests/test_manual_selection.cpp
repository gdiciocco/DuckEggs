#include "platform/manual_window_identity.hpp"
#include "schiva/platform.hpp"
#include "schiva/runtime.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path() /
                ("schiva-manual-selection-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

bool wait_until(
    const std::function<bool()>& predicate,
    double timeout_seconds = 3.0) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double>(timeout_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

class FakeSignals final : public schiva::RuntimeSignals {
public:
    bool already_running() const override { return false; }
    bool stop_requested() const override { return stop_.load(); }
    void sign_stop() override { stop_.store(true); }
    void close() override { sign_stop(); }

private:
    std::atomic<bool> stop_{false};
};

struct FakeState {
    mutable std::mutex mutex;
    std::thread::id owner;
    bool owner_violation{};
    bool paused{true};
    bool manual_mode{};
    bool lost{};
    bool motion_active{};
    schiva::Rect pip{640, 360, 960, 540};
    std::string selected_token;
    std::string selection_error;
    int window_generation{};
    int windows_calls{};
    int select_calls{};
    int reset_calls{};
    int observe_calls{};
    int end_motion_calls{};
    int begin_motion_calls{};
    int move_calls{};
};

class FakeBackend final : public schiva::Backend {
public:
    explicit FakeBackend(std::shared_ptr<FakeState> state)
        : state_(std::move(state)) {}

    ~FakeBackend() override { close(); }

    void set_paused(bool paused) override {
        owner();
        std::lock_guard lock(state_->mutex);
        state_->paused = paused;
    }

    schiva::Observation observe(double, bool) override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->observe_calls;
        schiva::Observation observation;
        observation.cursor = {610, 350};
        observation.work_area = {0, 0, 1280, 800};
        if (state_->manual_mode && !state_->lost) {
            observation.pip = schiva::WindowInfo{
                "hwnd:1", "manual-window", "window",
                state_->pip};
        }
        return observation;
    }

    bool begin_motion(const std::string& pip_id) override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->begin_motion_calls;
        state_->motion_active =
            pip_id == "hwnd:1" && state_->manual_mode &&
            !state_->lost && !state_->paused;
        return state_->motion_active;
    }

    void end_motion() override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->end_motion_calls;
        state_->motion_active = false;
    }

    bool move(const std::string& pip_id, int left, int top) override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->move_calls;
        const bool allowed =
            pip_id == "hwnd:1" && state_->manual_mode &&
            !state_->lost && !state_->paused &&
            state_->motion_active;
        if (allowed) {
            state_->pip = state_->pip.moved_to(left, top);
        }
        return allowed;
    }

    schiva::ManualWindowSnapshot manual_windows() override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->windows_calls;
        ++state_->window_generation;
        const std::string generation =
            std::to_string(state_->window_generation);
        return {
            true,
            std::nullopt,
            {
                {"candidate-" + generation + "-video",
                 "Synthetic video",
                 "player.exe",
                 {500, 250, 820, 430}},
                {"candidate-" + generation + "-browser",
                 "Synthetic browser",
                 "browser.exe",
                 {100, 100, 1100, 700}},
            }};
    }

    schiva::ManualTargetStatus manual_target_status() const override {
        owner();
        std::lock_guard lock(state_->mutex);
        schiva::ManualTargetStatus status;
        status.supported = true;
        status.mode = state_->manual_mode
                          ? schiva::ManualTargetMode::Manual
                          : schiva::ManualTargetMode::Automatic;
        status.selected =
            state_->manual_mode && !state_->lost &&
            !state_->selected_token.empty();
        status.selected_id = status.selected
                                 ? state_->selected_token
                                 : std::string{};
        if (!state_->selection_error.empty()) {
            status.error = state_->selection_error;
        }
        if (state_->manual_mode && state_->lost) {
            status.error =
                "The selected manual window is no longer available.";
        }
        return status;
    }

    bool select_manual_window(
        std::string_view selection_token) override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->select_calls;
        const std::string current =
            "candidate-" + std::to_string(state_->window_generation) +
            "-video";
        const std::string other =
            "candidate-" + std::to_string(state_->window_generation) +
            "-browser";
        if (selection_token != current && selection_token != other) {
            state_->selection_error =
                "This window list is stale. Refresh and choose again.";
            return false;
        }
        state_->selection_error.clear();
        state_->manual_mode = true;
        state_->lost = false;
        state_->selected_token = std::string(selection_token);
        return true;
    }

    bool reset_manual_target() override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->reset_calls;
        state_->manual_mode = false;
        state_->lost = false;
        state_->selected_token.clear();
        state_->selection_error.clear();
        return true;
    }

    schiva::Json capabilities() const override {
        owner();
        std::lock_guard lock(state_->mutex);
        return {
            {"supported", true},
            {"manual_selection_supported", true},
            {"candidate_count",
             state_->manual_mode && !state_->lost ? 1 : 0},
            {"last_error", nullptr}};
    }

    bool python_instance_active() const override {
        owner();
        return false;
    }

    void close() override {
        if (closed_.exchange(true)) {
            return;
        }
        owner();
    }

private:
    void owner() const {
        std::lock_guard lock(state_->mutex);
        if (!state_->owner_violation) {
            if (state_->owner == std::thread::id{}) {
                state_->owner = std::this_thread::get_id();
            } else if (state_->owner != std::this_thread::get_id()) {
                state_->owner_violation = true;
            }
        }
    }

    std::shared_ptr<FakeState> state_;
    std::atomic<bool> closed_{false};
};

class MinimalBackend final : public schiva::Backend {
public:
    void set_paused(bool) override {}
    schiva::Observation observe(double, bool) override { return {}; }
    bool begin_motion(const std::string&) override { return false; }
    void end_motion() override {}
    bool move(const std::string&, int, int) override { return false; }
    schiva::Json capabilities() const override { return {}; }
    bool python_instance_active() const override { return false; }
    void close() override {}
};

void test_identity_predicate() {
    const schiva::detail::ManualWindowIdentity bound{
        0x1234,
        42,
        987654,
        L"VideoWindow",
        L"Player.EXE",
        0xabc};
    CHECK(schiva::detail::manual_window_identity_matches(bound, bound));
    auto changed = bound;
    changed.lifetime_marker = 0;
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.process_id = 43;
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.process_creation_time = 0;
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.process_creation_time = 987655;
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.class_name = L"OtherWindow";
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.executable = L"browser.exe";
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.executable = L"player.exe";
    CHECK(schiva::detail::manual_window_identity_matches(bound, changed));
}

void test_default_backend_contract() {
    MinimalBackend backend;
    const schiva::ManualWindowSnapshot windows = backend.manual_windows();
    CHECK(!windows.supported);
    CHECK(windows.error.has_value());
    const schiva::ManualTargetStatus status =
        backend.manual_target_status();
    CHECK(!status.supported);
    CHECK(status.mode == schiva::ManualTargetMode::Automatic);
    CHECK(!backend.select_manual_window("candidate"));
    CHECK(!backend.reset_manual_target());
}

void test_runtime_manual_commands() {
    TemporaryDirectory temporary;
    auto state = std::make_shared<FakeState>();
    schiva::RuntimeOptions options;
    options.data_dir = temporary.path();
    options.no_gui = true;
    options.no_visual = true;
    options.start_paused = true;
    options.backend_factory = [state] {
        return std::make_unique<FakeBackend>(state);
    };
    options.signals_factory = [] {
        return std::make_unique<FakeSignals>();
    };

    schiva::AdaptiveApplication application(options);
    std::thread runner([&application] {
        (void)application.run();
    });

    CHECK(wait_until([&application] {
        return application.snapshot().value("state", "") != "starting";
    }));
    CHECK(application.manual_snapshot()["mode"] == "automatic");
    CHECK(!application.manual_snapshot()["picker_open"].get<bool>());

    // The toggle is queued before opening; opening then explicitly forces
    // pause. A toggle queued while the chooser owns the session is ignored.
    application.request_pause();
    application.request_manual_picker();
    application.request_pause();
    CHECK(wait_until([&application] {
        const schiva::Json manual = application.manual_snapshot();
        return manual["picker_open"].get<bool>() &&
               manual["windows"].size() == 2;
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    CHECK(application.manual_snapshot()["supported"].get<bool>());

    // Closing the chooser is itself a worker-thread pause barrier, even when
    // a pause/resume toggle was queued around the opening request.
    application.request_pause();
    application.request_manual_picker_closed();
    CHECK(wait_until([&application] {
        return !application.manual_snapshot()["picker_open"].get<bool>();
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    application.request_manual_picker();
    CHECK(wait_until([&application] {
        return application.manual_snapshot()["picker_open"].get<bool>();
    }));

    const schiva::Json initial_manual =
        application.manual_snapshot();
    const std::string first_token =
        initial_manual["windows"][0]["selection_token"]
            .get<std::string>();
    const int end_before_select = [&state] {
        std::lock_guard lock(state->mutex);
        return state->end_motion_calls;
    }();
    application.request_manual_selection(first_token);
    CHECK(wait_until([&application, first_token] {
        const schiva::Json manual = application.manual_snapshot();
        const schiva::Json runtime = application.snapshot();
        return manual["mode"] == "manual" &&
               manual["selected_id"] == first_token &&
               runtime["paused"].get<bool>() &&
               runtime["placement_mode"] == "manual";
    }));
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->end_motion_calls > end_before_select);
    }

    application.request_manual_refresh();
    CHECK(wait_until([&application, first_token] {
        const schiva::Json manual = application.manual_snapshot();
        return manual["windows"].size() == 2 &&
               manual["windows"][0]["selection_token"] != first_token;
    }));
    const int stale_attempt_revision =
        application.manual_snapshot()["selection_revision"].get<int>();
    application.request_manual_selection(first_token);
    CHECK(wait_until([&application, stale_attempt_revision] {
        const schiva::Json manual = application.manual_snapshot();
        return manual["selection_revision"].get<int>() >
                   stale_attempt_revision &&
               manual["error"].is_string();
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    CHECK(application.manual_snapshot()["mode"] == "manual");
    CHECK(application.manual_snapshot()["selected_id"] == first_token);

    application.request_manual_reset();
    CHECK(wait_until([&application] {
        return application.manual_snapshot()["mode"] == "automatic";
    }));
    CHECK(application.snapshot()["paused"].get<bool>());

    const std::string token_before_refresh =
        application.manual_snapshot()["windows"][0]["selection_token"]
            .get<std::string>();
    application.request_manual_refresh();
    CHECK(wait_until([&application, token_before_refresh] {
        const schiva::Json manual = application.manual_snapshot();
        return manual["windows"].size() == 2 &&
               manual["windows"][0]["selection_token"] !=
                   token_before_refresh;
    }));
    const std::string current_token =
        application.manual_snapshot()["windows"][1]["selection_token"]
            .get<std::string>();
    application.request_manual_selection(current_token);
    CHECK(wait_until([&application, current_token] {
        const schiva::Json manual = application.manual_snapshot();
        return manual["mode"] == "manual" &&
               manual["selected_id"] == current_token;
    }));
    CHECK(application.snapshot()["paused"].get<bool>());

    application.request_manual_picker_closed();
    CHECK(wait_until([&application] {
        return !application.manual_snapshot()["picker_open"].get<bool>();
    }));
    const int moves_before_resume = [&state] {
        std::lock_guard lock(state->mutex);
        return state->move_calls;
    }();
    application.request_pause();
    CHECK(wait_until([&application] {
        return !application.snapshot()["paused"].get<bool>();
    }));
    CHECK(wait_until([&state, moves_before_resume] {
        std::lock_guard lock(state->mutex);
        return state->motion_active &&
               state->move_calls > moves_before_resume;
    }));
    const int end_motion_before_loss = [&state] {
        std::lock_guard lock(state->mutex);
        return state->end_motion_calls;
    }();

    {
        std::lock_guard lock(state->mutex);
        state->lost = true;
    }
    CHECK(wait_until([&application] {
        const schiva::Json runtime = application.snapshot();
        const schiva::Json manual = application.manual_snapshot();
        return runtime["paused"].get<bool>() &&
               runtime["placement_mode"] == "manual" &&
               !runtime["motion_active"].get<bool>() &&
               manual["mode"] == "manual" &&
               manual["selected_id"].is_null() &&
               manual["error"].is_string();
    }));
    int observes_after_loss = 0;
    int moves_after_pause = 0;
    int begin_motion_after_pause = 0;
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->move_calls > moves_before_resume);
        CHECK(!state->motion_active);
        CHECK(state->end_motion_calls > end_motion_before_loss);
        observes_after_loss = state->observe_calls;
        moves_after_pause = state->move_calls;
        begin_motion_after_pause = state->begin_motion_calls;
    }
    CHECK(wait_until([&state, observes_after_loss] {
        std::lock_guard lock(state->mutex);
        return state->observe_calls >= observes_after_loss + 3;
    }));
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->move_calls == moves_after_pause);
        CHECK(state->begin_motion_calls == begin_motion_after_pause);
        CHECK(!state->motion_active);
    }

    {
        std::lock_guard lock(state->mutex);
        CHECK(!state->owner_violation);
        CHECK(state->windows_calls >= 3);
        CHECK(state->select_calls >= 3);
        CHECK(state->reset_calls == 1);
    }

    application.request_quit();
    if (runner.joinable()) {
        runner.join();
    }
    const schiva::Json status = application.snapshot();
    CHECK(!status.contains("windows"));
    CHECK(status.dump().find("Synthetic video") == std::string::npos);
    std::ifstream status_file(temporary.path() / "status.json");
    const std::string persisted{
        std::istreambuf_iterator<char>(status_file),
        std::istreambuf_iterator<char>()};
    CHECK(!persisted.empty());
    CHECK(persisted.find("Synthetic video") == std::string::npos);
    CHECK(persisted.find("player.exe") == std::string::npos);
}

void test_manual_picker_reopen_keeps_session_latch() {
    TemporaryDirectory temporary;
    auto state = std::make_shared<FakeState>();
    schiva::RuntimeOptions options;
    options.data_dir = temporary.path();
    options.no_gui = true;
    options.no_visual = true;
    options.start_paused = true;
    options.backend_factory = [state] {
        return std::make_unique<FakeBackend>(state);
    };
    options.signals_factory = [] {
        return std::make_unique<FakeSignals>();
    };

    schiva::AdaptiveApplication application(options);
    application.request_manual_picker();
    application.request_manual_picker_closed();
    application.request_manual_picker();

    std::thread runner([&application] {
        (void)application.run();
    });
    CHECK(wait_until([&application] {
        const schiva::Json manual = application.manual_snapshot();
        return manual["picker_open"].get<bool>() &&
               manual["windows"].size() == 2;
    }));
    CHECK(application.snapshot()["paused"].get<bool>());

    const int manual_revision =
        application.manual_snapshot()["revision"].get<int>();
    application.request_pause();
    application.request_manual_refresh();
    CHECK(wait_until([&application, manual_revision] {
        return application.manual_snapshot()["revision"].get<int>() >
               manual_revision;
    }));
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->paused);
    }
    CHECK(application.snapshot()["paused"].get<bool>());

    application.request_manual_picker_closed();
    CHECK(wait_until([&application] {
        return !application.manual_snapshot()["picker_open"].get<bool>();
    }));
    application.request_pause();
    CHECK(wait_until([&application] {
        return !application.snapshot()["paused"].get<bool>();
    }));

    application.request_quit();
    if (runner.joinable()) {
        runner.join();
    }
    {
        std::lock_guard lock(state->mutex);
        CHECK(!state->owner_violation);
    }
}

}  // namespace

int main() {
    test_identity_predicate();
    test_default_backend_contract();
    test_runtime_manual_commands();
    test_manual_picker_reopen_keeps_session_latch();
    if (failures != 0) {
        std::cerr << failures << " manual-selection checks failed\n";
        return 1;
    }
    std::cout << "All manual-selection checks passed\n";
    return 0;
}
