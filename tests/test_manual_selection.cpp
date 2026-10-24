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
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

constexpr schiva::Point kEligiblePoint{700, 400};
constexpr schiva::Point kFullScreenPoint{320, 260};

struct FakeState {
    mutable std::mutex mutex;
    std::thread::id owner;
    bool owner_violation{};
    bool paused{true};
    bool manual_mode{};
    bool lost{};
    bool motion_active{};
    schiva::Rect pip{640, 360, 960, 540};
    std::string selected_id;
    std::string selection_error;
    std::optional<schiva::Point> hover_point;
    std::optional<schiva::Point> select_point;
    int hover_calls{};
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
                "hwnd:1", "manual-window", "window", state_->pip};
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

    schiva::ManualHoverSnapshot manual_hover(
        schiva::Point point) override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->hover_calls;
        state_->hover_point = point;
        schiva::ManualHoverSnapshot hover;
        hover.supported = true;
        hover.point = point;
        if (point == kFullScreenPoint) {
            hover.found = true;
            hover.title = "Synthetic full-screen video";
            hover.rect = {0, 0, 1280, 800};
            hover.eligible = false;
            hover.reason = "Full-screen windows cannot be moved.";
        } else if (point == kEligiblePoint) {
            hover.found = true;
            hover.title = "Synthetic video";
            hover.rect = {500, 250, 820, 430};
            hover.eligible = true;
        }
        return hover;
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
            !state_->selected_id.empty();
        status.selected_id =
            status.selected ? state_->selected_id : std::string{};
        if (!state_->selection_error.empty()) {
            status.error = state_->selection_error;
        }
        if (state_->manual_mode && state_->lost) {
            status.error =
                "The selected manual window is no longer available.";
        }
        return status;
    }

    bool select_manual_at(schiva::Point point) override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->select_calls;
        state_->select_point = point;
        if (point == kFullScreenPoint) {
            state_->selection_error =
                "Full-screen windows cannot be moved.";
            return false;
        }
        if (point != kEligiblePoint) {
            state_->selection_error =
                "No movable window is under the pointer.";
            return false;
        }
        state_->selection_error.clear();
        state_->manual_mode = true;
        state_->lost = false;
        state_->selected_id = "manual-1";
        return true;
    }

    bool reset_manual_target() override {
        owner();
        std::lock_guard lock(state_->mutex);
        ++state_->reset_calls;
        state_->manual_mode = false;
        state_->lost = false;
        state_->selected_id.clear();
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
    changed = bound;
    changed.handle = 0x9999;
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.process_id = 0;
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
    changed = bound;
    changed.lifetime_marker = 0xdef;
    CHECK(!schiva::detail::manual_window_identity_matches(bound, changed));
}

void test_manual_topmost_lease_policy() {
    using schiva::detail::ManualTopmostAction;
    using schiva::detail::manual_topmost_action;
    using schiva::detail::manual_topmost_commit_promotion;
    using schiva::detail::manual_topmost_lease;

    auto originally_normal =
        manual_topmost_lease(false);
    CHECK(!originally_normal.originally_topmost);
    CHECK(!originally_normal.owns_promotion);
    CHECK(manual_topmost_action(
              originally_normal, true, false) ==
          ManualTopmostAction::Promote);
    CHECK(manual_topmost_action(
              originally_normal, true, true) ==
          ManualTopmostAction::None);

    // Ownership starts after SetWindowPos succeeds, before verification, so a
    // failed verification remains eligible for rollback.
    CHECK(manual_topmost_action(
              originally_normal, false, true) ==
          ManualTopmostAction::None);
    manual_topmost_commit_promotion(originally_normal);
    CHECK(originally_normal.owns_promotion);
    CHECK(manual_topmost_action(
              originally_normal, true, false) ==
          ManualTopmostAction::Promote);
    CHECK(manual_topmost_action(
              originally_normal, false, true) ==
          ManualTopmostAction::Restore);
    CHECK(manual_topmost_action(
              originally_normal, false, false) ==
          ManualTopmostAction::None);

    // Re-selecting the same binding retains this original bit and ownership.
    const auto reselected = originally_normal;
    CHECK(!reselected.originally_topmost);
    CHECK(reselected.owns_promotion);
    CHECK(manual_topmost_action(
              reselected, true, true) ==
          ManualTopmostAction::None);

    auto originally_topmost =
        manual_topmost_lease(true);
    CHECK(originally_topmost.originally_topmost);
    CHECK(!originally_topmost.owns_promotion);
    CHECK(manual_topmost_action(
              originally_topmost, true, true) ==
          ManualTopmostAction::None);
    CHECK(manual_topmost_action(
              originally_topmost, true, false) ==
          ManualTopmostAction::Promote);
    manual_topmost_commit_promotion(originally_topmost);
    CHECK(!originally_topmost.owns_promotion);
    CHECK(manual_topmost_action(
              originally_topmost, false, true) ==
          ManualTopmostAction::None);
    CHECK(manual_topmost_action(
              originally_topmost, false, false) ==
          ManualTopmostAction::None);

    // A promotion that never completed is not owned and must not be undone.
    const schiva::detail::ManualTopmostLease failed_promotion{
        false, false};
    CHECK(manual_topmost_action(
              failed_promotion, false, true) ==
          ManualTopmostAction::None);

    // Even a malformed ownership bit cannot demote a pre-existing topmost
    // window.
    const schiva::detail::ManualTopmostLease malformed{
        true, true};
    CHECK(manual_topmost_action(
              malformed, false, true) ==
          ManualTopmostAction::None);
}

void test_portable_manual_policy() {
    const schiva::Rect monitor{-1920, 0, 0, 1080};
    const schiva::Rect work{-1920, 0, 0, 1040};
    CHECK(!schiva::manual_window_eligibility(
               monitor, monitor, work, false)
               .eligible);
    CHECK(!schiva::manual_window_eligibility(
               work, monitor, work, false)
               .eligible);
    CHECK(!schiva::manual_window_eligibility(
               {-1800, 80, -120, 980}, monitor, work, true)
               .eligible);
    CHECK(schiva::manual_window_eligibility(
              {-1800, 80, -200, 900}, monitor, work, false)
              .eligible);

    const std::vector<schiva::ManualWindowHit> stacked{
        {{-1910, 0, -1000, 500},
         true, true, false, false, false, false, true, false, false},
        {monitor,
         true, true, false, false, false, false, true, false, false},
        {{-1700, 100, -500, 800},
         true, true, false, false, false, false, true, false, false}};
    const auto hit = schiva::manual_hit_test(stacked, {-900, 300});
    CHECK(hit.has_value());
    CHECK(*hit == 1U);
    CHECK(!schiva::manual_window_eligibility(
               stacked[*hit].rect, monitor, work, true)
               .eligible);

    const std::vector<schiva::ManualWindowHit> owned_overlay{
        {monitor,
         true, true, false, false, true, false, true, true, false},
        {{-1700, 100, -500, 800},
         true, true, false, false, false, false, true, false, false}};
    const auto underlying =
        schiva::manual_hit_test(owned_overlay, {-900, 300});
    CHECK(underlying.has_value());
    CHECK(*underlying == 1U);

    const schiva::Rect candidate{-1700, 100, -500, 800};
    const schiva::ManualWindowHit app{
        candidate, true, true, false, false,
        false, false, true, false, false};
    CHECK(schiva::manual_hit_test(
              {{{-1700, 100, -500, 800},
                true, true, false, false, true, false,
                true, false, false},
               app},
              {-900, 300}) ==
          std::optional<std::size_t>(0));
    CHECK(schiva::manual_hit_test(
              {{{-1700, 100, -500, 800},
                true, true, false, false, false, true,
                true, false, false},
               app},
              {-900, 300}) ==
          std::optional<std::size_t>(0));
    CHECK(schiva::manual_hit_test(
              {{{-1700, 100, -500, 800},
                true, true, false, false, false, false,
                true, false, true},
               app},
              {-900, 300}) ==
          std::optional<std::size_t>(1));
    CHECK(schiva::manual_hit_test(
              {{{-1700, 100, -500, 800},
                true, true, false, false, false, false,
                false, false, false},
               app},
              {-900, 300}) ==
          std::optional<std::size_t>(0));

    const std::vector<schiva::ManualWindowHit> infrastructure{
        {candidate,
         false, true, false, false, false, false, true, false, false},
        {candidate,
         true, true, true, false, false, false, true, false, false},
        {candidate,
         true, true, false, true, false, false, true, false, false},
        app};
    CHECK(schiva::manual_hit_test(infrastructure, {-900, 300}) ==
          std::optional<std::size_t>(3));
    CHECK(!schiva::manual_hit_test(infrastructure, {10, 10}).has_value());
}

void test_default_backend_contract() {
    MinimalBackend backend;
    const schiva::ManualHoverSnapshot hover =
        backend.manual_hover(kEligiblePoint);
    CHECK(!hover.supported);
    CHECK(!hover.found);
    CHECK(hover.error.has_value());
    const schiva::ManualTargetStatus status =
        backend.manual_target_status();
    CHECK(!status.supported);
    CHECK(status.mode == schiva::ManualTargetMode::Automatic);
    CHECK(!backend.select_manual_at(kEligiblePoint));
    CHECK(!backend.reset_manual_target());
}

std::string status_file_contents(
    const std::filesystem::path& directory) {
    std::ifstream status_file(directory / "status.json");
    return {
        std::istreambuf_iterator<char>(status_file),
        std::istreambuf_iterator<char>()};
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

    application.request_pause();
    application.request_manual_picker();
    application.request_pause();
    CHECK(wait_until([&application] {
        return application.manual_snapshot()["picker_open"].get<bool>();
    }));
    CHECK(application.snapshot()["paused"].get<bool>());

    // Many pointer events collapse to one latest-point worker request.
    for (int x = 0; x < 40; ++x) {
        application.request_manual_hover(schiva::Point{x, x});
    }
    application.request_manual_hover(kEligiblePoint);
    CHECK(wait_until([&application] {
        const schiva::Json hover =
            application.manual_snapshot()["hover"];
        return hover["point"]["x"].get<int>() == kEligiblePoint.x &&
               hover["point"]["y"].get<int>() == kEligiblePoint.y;
    }));
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->hover_calls <= 2);
    }

    // The click point, not the last hover, is sent for a fresh hit-test.
    application.request_manual_selection(kFullScreenPoint);
    CHECK(wait_until([&application] {
        const schiva::Json manual = application.manual_snapshot();
        return manual["selection_revision"].get<int>() > 0 &&
               manual["error"] ==
                   "Full-screen windows cannot be moved.";
    }));
    CHECK(application.manual_snapshot()["picker_open"].get<bool>());
    CHECK(application.manual_snapshot()["selected_id"].is_null());
    CHECK(application.snapshot()["paused"].get<bool>());
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->select_point == kFullScreenPoint);
    }

    const int end_before_select = [&state] {
        std::lock_guard lock(state->mutex);
        return state->end_motion_calls;
    }();
    application.request_manual_selection(kEligiblePoint);
    CHECK(wait_until([&application] {
        const schiva::Json manual = application.manual_snapshot();
        const schiva::Json runtime = application.snapshot();
        return manual["mode"] == "manual" &&
               !manual["selected_id"].is_null() &&
               runtime["paused"].get<bool>() &&
               runtime["placement_mode"] == "manual";
    }));
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->select_point == kEligiblePoint);
        CHECK(state->end_motion_calls > end_before_select);
    }

    application.request_manual_picker_closed();
    CHECK(wait_until([&application] {
        return !application.manual_snapshot()["picker_open"].get<bool>();
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    CHECK(application.manual_snapshot()["mode"] == "manual");
    const std::string selected_before_cancel =
        application.manual_snapshot()["selected_id"].get<std::string>();

    const int selection_revision_before_late =
        application.manual_snapshot()["selection_revision"].get<int>();
    const int select_calls_before_late = [&state] {
        std::lock_guard lock(state->mutex);
        return state->select_calls;
    }();
    application.request_manual_selection(kEligiblePoint);
    application.request_manual_picker();
    CHECK(wait_until([&application] {
        return application.manual_snapshot()["picker_open"].get<bool>();
    }));
    application.request_manual_hover(kEligiblePoint);
    CHECK(wait_until([&application] {
        const schiva::Json hover =
            application.manual_snapshot()["hover"];
        return hover["point"]["x"].get<int>() == kEligiblePoint.x &&
               hover["point"]["y"].get<int>() == kEligiblePoint.y;
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    CHECK(application.manual_snapshot()["selected_id"] ==
          selected_before_cancel);
    CHECK(application.manual_snapshot()["selection_revision"].get<int>() ==
          selection_revision_before_late);
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->select_calls == select_calls_before_late);
    }
    application.request_pause();
    application.request_manual_picker_closed();
    CHECK(wait_until([&application] {
        return !application.manual_snapshot()["picker_open"].get<bool>();
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    CHECK(application.manual_snapshot()["mode"] == "manual");
    CHECK(application.manual_snapshot()["selected_id"] ==
          selected_before_cancel);

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
    int moves_after_pause = 0;
    int begin_motion_after_pause = 0;
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->move_calls > moves_before_resume);
        CHECK(!state->motion_active);
        CHECK(state->end_motion_calls > end_motion_before_loss);
        moves_after_pause = state->move_calls;
        begin_motion_after_pause = state->begin_motion_calls;
    }
    CHECK(wait_until([&state] {
        std::lock_guard lock(state->mutex);
        return state->observe_calls >= 3;
    }));
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->move_calls == moves_after_pause);
        CHECK(state->begin_motion_calls == begin_motion_after_pause);
        CHECK(!state->motion_active);
    }

    // Cancel is a pause barrier and changes neither target nor selection count.
    const int selects_before_cancel = [&state] {
        std::lock_guard lock(state->mutex);
        return state->select_calls;
    }();
    application.request_manual_picker();
    CHECK(wait_until([&application] {
        return application.manual_snapshot()["picker_open"].get<bool>();
    }));
    application.request_pause();
    application.request_manual_picker_closed();
    CHECK(wait_until([&application] {
        return !application.manual_snapshot()["picker_open"].get<bool>();
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    CHECK(application.manual_snapshot()["mode"] == "manual");
    CHECK(application.manual_snapshot()["selected_id"].is_null());
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->select_calls == selects_before_cancel);
    }

    application.request_manual_picker();
    CHECK(wait_until([&application] {
        return application.manual_snapshot()["picker_open"].get<bool>();
    }));
    application.request_manual_reset();
    application.request_manual_picker_closed();
    CHECK(wait_until([&application] {
        const schiva::Json manual = application.manual_snapshot();
        return !manual["picker_open"].get<bool>() &&
               manual["mode"] == "automatic";
    }));
    CHECK(application.snapshot()["paused"].get<bool>());
    CHECK(application.snapshot()["placement_mode"] == "automatic");

    const int hover_calls_after_close = [&state] {
        std::lock_guard lock(state->mutex);
        return state->hover_calls;
    }();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    {
        std::lock_guard lock(state->mutex);
        CHECK(state->hover_calls == hover_calls_after_close);
        CHECK(!state->owner_violation);
        CHECK(state->select_calls >= 2);
        CHECK(state->reset_calls == 1);
    }

    application.request_quit();
    if (runner.joinable()) {
        runner.join();
    }
    const schiva::Json status = application.snapshot();
    CHECK(!status.contains("hover"));
    const std::string persisted =
        status_file_contents(temporary.path());
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
        return application.manual_snapshot()["picker_open"].get<bool>();
    }));
    CHECK(application.snapshot()["paused"].get<bool>());

    application.request_pause();
    application.request_manual_hover(kEligiblePoint);
    CHECK(wait_until([&application] {
        const schiva::Json hover =
            application.manual_snapshot()["hover"];
        return hover["point"]["x"].get<int>() == kEligiblePoint.x;
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
    test_manual_topmost_lease_policy();
    test_portable_manual_policy();
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
