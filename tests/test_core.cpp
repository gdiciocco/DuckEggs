#include "schiva/core.hpp"
#include "../src/core/internal.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;
std::string current_test;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL [" << current_test << "] " << __FILE__        \
                      << ":" << __LINE__ << ": " #condition << '\n';         \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                              \
    do {                                                                     \
        const double actual_value = (actual);                                \
        const double expected_value = (expected);                            \
        if (!(std::abs(actual_value - expected_value) <= (tolerance))) {      \
            std::cerr << "FAIL [" << current_test << "] " << __FILE__        \
                      << ":" << __LINE__ << ": " << actual_value             \
                      << " != " << expected_value << '\n';                   \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

#define CHECK_THROWS(expression)                                             \
    do {                                                                     \
        bool threw = false;                                                  \
        try {                                                                \
            (void)(expression);                                              \
        } catch (const std::exception&) {                                    \
            threw = true;                                                    \
        }                                                                    \
        if (!threw) {                                                        \
            std::cerr << "FAIL [" << current_test << "] " << __FILE__        \
                      << ":" << __LINE__ << ": expected exception: "          \
                      << #expression << '\n';                                \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

class TempDirectory {
public:
    TempDirectory() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("schiva-core-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] std::filesystem::path file(
        const std::string& name) const {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

const schiva::Rect kWork{0, 0, 1920, 1080};
const schiva::Rect kPip{100, 100, 420, 280};
const schiva::WindowInfo kForeground{
    "foreground", "editor.exe", "Editor", kWork};

schiva::Observation observation(
    schiva::Rect pip_rect = kPip, schiva::Point cursor = {510, 250},
    bool buttons = false, std::string pip_id = "pip-handle",
    std::optional<schiva::VisualFrame> visual = std::nullopt,
    std::vector<schiva::Rect> exclusions = {},
    std::optional<schiva::WindowInfo> foreground = kForeground) {
    schiva::Observation result;
    result.cursor = cursor;
    result.buttons_down = buttons;
    result.pip = schiva::WindowInfo{
        std::move(pip_id), "chrome.exe", "Chrome_WidgetWin_1", pip_rect};
    result.foreground = std::move(foreground);
    result.work_area = kWork;
    result.visual_frame = std::move(visual);
    result.input_active = true;
    result.input_age = 0.1;
    result.interaction_exclusions = std::move(exclusions);
    return result;
}

schiva::Decision trigger(schiva::Controller& controller,
                         const schiva::Observation& current = observation()) {
    controller.tick(0.0, current);
    return controller.tick(0.04, current);
}

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::absolute(std::filesystem::path(__FILE__))
               .parent_path() /
           "fixtures" / name;
}

void test_domain_and_linucb() {
    current_test = "domain_and_linucb";
    const schiva::Rect rect{10, 20, 110, 80};
    CHECK(rect.width() == 100);
    CHECK(rect.height() == 60);
    CHECK((rect.moved_to(-5, 7) == schiva::Rect{-5, 7, 95, 67}));

    schiva::detail::LinUCBModel model(3, 0.0);
    model.update({1.0, 0.0, 0.0}, 1.0);
    const schiva::Json state = model.state();
    CHECK_NEAR(state["inverse"][0][0].get<double>(), 0.5, 1e-12);
    CHECK_NEAR(state["inverse"][1][1].get<double>(), 1.0, 1e-12);
    CHECK_NEAR(model.predict({1.0, 0.0, 0.0}), 0.5, 1e-12);
    CHECK_NEAR(model.uncertainty({1.0, 0.0, 0.0}),
               std::sqrt(0.5), 1e-12);
    model.update({1.0, 0.0, 0.0}, 1.0);
    CHECK_NEAR(state.empty() ? 0.0 : model.predict({1.0, 0.0, 0.0}),
               2.0 / 3.0, 1e-12);
    const auto restored = schiva::detail::LinUCBModel::from_state(model.state());
    CHECK_NEAR(restored.predict({1.0, 0.0, 0.0}),
               model.predict({1.0, 0.0, 0.0}), 1e-12);
    CHECK(restored.updates() == 2);
    CHECK_THROWS(model.update({1.0, 2.0}, 1.0));
    CHECK_THROWS(model.update({1.0, 0.0, 0.0}, 2.0));
    CHECK_THROWS(schiva::detail::LinUCBModel(
        2, 0.35, {{1.0, 2.0}, {2.0, 1.0}}, {0.0, 0.0}, 0));
}

void test_geometry_activity_visual() {
    current_test = "geometry_activity_visual";
    const auto candidates = schiva::detail::candidate_rects(
        kWork, kPip.width(), kPip.height(), {640, 450}, kPip);
    CHECK(candidates.size() > 8);
    CHECK(std::none_of(candidates.begin(), candidates.end(),
                       [](const schiva::Rect& target) {
                           return target == kPip;
                       }));

    const auto path = schiva::detail::plan_safe_path(
        {100, 400}, {700, 420}, 300, 200, {450, 500}, 28, kWork);
    CHECK(path.has_value());
    CHECK(path.has_value() && path->size() > 2);
    if (path.has_value()) {
        for (std::size_t index = 1; index < path->size(); ++index) {
            CHECK(schiva::detail::segment_is_safe(
                (*path)[index - 1], (*path)[index], 300, 200, {450, 500},
                28));
        }
    }

    const auto trajectory = schiva::detail::MotionTrajectory::from_path(
        {{{100, 100}, {400, 100}, {400, 500}}});
    auto at_120 = trajectory;
    auto at_60 = trajectory;
    for (int index = 0; index < 6; ++index) {
        at_120.advance(1.0 / 120.0);
    }
    at_60.advance(1.0 / 20.0);
    CHECK(at_120.position() == at_60.position());
    CHECK(at_120.position().y == 100);

    schiva::detail::ActivityMap map(10, 1, 8.0);
    const schiva::Rect reference{0, 0, 100, 10};
    map.add(5, 5, reference, 2.0);
    CHECK_NEAR(map.density_for_rect(reference, reference), 0.2, 1e-12);
    map.decay(8.0);
    CHECK_NEAR(map.values()[0], 1.0, 1e-12);

    schiva::detail::VisualActivityTracker tracker(8, 5);
    const schiva::Rect bounds{0, 0, 80, 50};
    const schiva::Rect pip{30, 20, 50, 40};
    auto frame = [&](int level) {
        schiva::VisualFrame result;
        result.width = 80;
        result.height = 50;
        result.pixels.assign(80 * 50, 80);
        for (int y = 20; y < 30; ++y) {
            for (int x = 56; x < 72; ++x) {
                result.pixels[static_cast<std::size_t>(y) * 80 + x] =
                    static_cast<std::uint8_t>(level);
            }
        }
        result.bounds = bounds;
        result.excluded = {pip};
        return result;
    };
    const auto first =
        tracker.observe(0.0, frame(80), pip, "context");
    const auto burst =
        tracker.observe(0.5, frame(220), pip, "context");
    const auto sustained =
        tracker.observe(1.0, frame(140), pip, "context");
    CHECK(!first.burst);
    CHECK(burst.burst && !burst.sustained);
    CHECK(sustained.burst && sustained.sustained);
    CHECK(tracker.grace_active_at(1.01));
    CHECK(tracker.risk_for({56, 20, 72, 30}, std::nullopt, 1.0) > 0.0);
}

void test_storage_schema_and_fixture() {
    current_test = "storage_schema_and_fixture";
    TempDirectory temp;
    const auto database = temp.file("newer.sqlite3");
    sqlite3* connection = nullptr;
    CHECK(sqlite3_open(database.string().c_str(), &connection) == SQLITE_OK);
    CHECK(sqlite3_exec(connection, "PRAGMA user_version = 99",
                       nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(
              connection,
              "CREATE TABLE sentinel(value TEXT);"
              "INSERT INTO sentinel VALUES ('keep');",
              nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(connection);
    CHECK_THROWS(schiva::LearningStore(database));
    connection = nullptr;
    CHECK(sqlite3_open(database.string().c_str(), &connection) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    CHECK(sqlite3_prepare_v2(connection, "SELECT value FROM sentinel", -1,
                             &statement, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(std::string(reinterpret_cast<const char*>(
              sqlite3_column_text(statement, 0))) == "keep");
    sqlite3_finalize(statement);
    sqlite3_close(connection);

    const auto copied = temp.file("backup.sqlite3");
    std::filesystem::copy_file(
        fixture("learning-schema1-backup.sqlite3"), copied);
    schiva::LearningStore store(copied);
    const schiva::Json stats = store.stats();
    CHECK(stats["schema_version"] == 1);
    CHECK(stats["profiles"] == 15);
    CHECK(stats["decision_records"] == 46);
    const auto profiles = schiva::detail::StoreAccess::list_profiles(store);
    CHECK(profiles.size() == 15);
    const auto model_json =
        schiva::detail::StoreAccess::load_model(store);
    CHECK(model_json.has_value());
    CHECK(model_json.has_value() &&
          model_json->at("updates").get<int>() == 46);
    schiva::Controller controller(store);
    CHECK(controller.status()["learning_count"] == 46);
    controller.close();
    store.close();

    schiva::LearningStore reopened(copied);
    CHECK(reopened.stats()["profiles"] == 15);
    CHECK(reopened.stats()["decision_records"] == 46);
    reopened.close();
}

void test_storage_roundtrip_and_bounds() {
    current_test = "storage_roundtrip_and_bounds";
    TempDirectory temp;
    const auto database = temp.file("state.sqlite3");
    {
        schiva::LearningStore store(database);
        const auto profile = schiva::detail::StoreAccess::load_profile(
            store, "editor.exe|editor|maximized", "editor.exe", "editor",
            "maximized");
        CHECK(profile.has_value());
        auto changed = *profile;
        changed.short_map = {{"4", 1.25}};
        changed.long_map = {{"4", 2.5}};
        changed.manual_map = {{"9", 0.75}};
        CHECK(schiva::detail::StoreAccess::save_profile(store, changed));
        schiva::detail::LinUCBModel model(2, 0.35);
        model.update({1.0, -0.5}, 0.1);
        schiva::detail::StoreAccess::save_model(store, model.state());
        CHECK(schiva::detail::StoreAccess::add_decision(
            store, changed.profile_key, {1.0, -0.5}, "anchor-03", 0.1));
        store.close();
    }
    {
        schiva::LearningStore store(database);
        const auto profile = schiva::detail::StoreAccess::peek_profile(
            store, "editor.exe|editor|maximized");
        CHECK(profile.has_value());
        CHECK(profile.has_value() && profile->short_map["4"] == 1.25);
        CHECK(profile.has_value() && profile->manual_map["9"] == 0.75);
        const auto model = schiva::detail::StoreAccess::load_model(store);
        CHECK(model.has_value() && model->at("updates") == 1);
        CHECK(store.stats()["decision_records"] == 1);
        store.close();
    }

    const auto bounded = temp.file("bounded.sqlite3");
    schiva::LearningStore store(bounded);
    for (int index = 0; index < 100; ++index) {
        CHECK(schiva::detail::StoreAccess::load_profile(
                  store, "profile-" + std::to_string(index), "app", "role",
                  "layout")
                  .has_value());
    }
    CHECK(!schiva::detail::StoreAccess::load_profile(
               store, "overflow", "app", "role", "layout")
               .has_value());
    for (int index = 0; index < 1000; ++index) {
        CHECK(schiva::detail::StoreAccess::add_decision(
            store, "profile-0", {1.0}, "a" + std::to_string(index), 0.0));
    }
    CHECK(!schiva::detail::StoreAccess::add_decision(
        store, "profile-0", {1.0}, "overflow", 0.0));
    CHECK(store.stats()["profiles"] == 100);
    CHECK(store.stats()["decision_records"] == 1000);
    CHECK(store.stats()["dropped_records"] == 1);
    store.close();
}

void test_smooth_motion_and_clearance() {
    current_test = "smooth_motion_and_clearance";
    TempDirectory temp;
    schiva::LearningStore store(temp.file("motion.sqlite3"));
    {
        schiva::Controller controller(store);
        schiva::Decision decision = trigger(controller);
        CHECK(decision.state == "moving");
        CHECK(decision.move_to.has_value());
        CHECK(decision.recommendation.has_value());
        CHECK(controller.status()["motion_active"] == true);
        schiva::Rect rect = kPip;
        double now = 0.04;
        double maximum_step = 0.0;
        int steps = 0;
        while (decision.move_to.has_value() && steps < 100) {
            maximum_step =
                std::max(maximum_step,
                         std::hypot(decision.move_to->x - rect.left,
                                    decision.move_to->y - rect.top));
            CHECK(schiva::detail::segment_is_safe(
                {rect.left, rect.top}, *decision.move_to, rect.width(),
                rect.height(), {510, 250}, 28));
            rect = rect.moved_to(decision.move_to->x, decision.move_to->y);
            now += 1.0 / 120.0;
            decision = controller.tick(now, observation(rect));
            ++steps;
        }
        CHECK(steps > 1);
        CHECK(maximum_step < 80.0);
        CHECK(decision.recommendation.has_value());
        if (decision.recommendation.has_value()) {
            CHECK(schiva::detail::vacated_fraction(
                      kPip, *decision.recommendation) >= 0.65);
            CHECK(rect == *decision.recommendation);
        }
        CHECK(controller.status()["learning_count"] == 0);
        controller.close();
    }
    store.close();
}

void test_holds_stalls_manual_and_observe_only() {
    current_test = "holds_stalls_manual_and_observe_only";
    TempDirectory temp;
    {
        schiva::LearningStore store(temp.file("controller.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Decision moving = trigger(controller);
        CHECK(moving.move_to.has_value());
        if (!moving.move_to.has_value()) {
            return;
        }
        const schiva::Rect applied = kPip.moved_to(
            moving.move_to->x, moving.move_to->y);
        const schiva::Decision held =
            controller.tick(0.30, observation(applied, {510, 250}, true));
        CHECK(held.state == "mouse_button_hold");
        CHECK(!held.move_to.has_value());
        const schiva::Decision resumed =
            controller.tick(0.316, observation(applied));
        CHECK(resumed.state == "moving");
        CHECK(resumed.move_to.has_value());

        const schiva::Rect corrected{140, 100, 460, 280};
        controller.tick(0.35, observation(corrected, {510, 250}, true));
        const schiva::Decision manual =
            controller.tick(0.36, observation(corrected));
        CHECK(manual.state == "manual_correction");
        CHECK(controller.status()["learning_count"] == 1);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("stall.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Decision moving = trigger(controller);
        CHECK(moving.move_to.has_value());
        const schiva::Decision stalled =
            controller.tick(2.0, observation(kPip));
        CHECK(stalled.state == "moving_stalled");
        CHECK(!stalled.move_to.has_value());
        CHECK(controller.status()["motion_active"] == true);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("observe.sqlite3"));
        schiva::Controller observe(store, true);
        const schiva::Decision decision = trigger(observe);
        CHECK(decision.state == "observe_only");
        CHECK(!decision.move_to.has_value());
        CHECK(decision.recommendation.has_value());
        for (int index = 0; index < 50; ++index) {
            observe.tick(0.2 + index * 0.25, observation());
        }
        CHECK(observe.status()["learning_count"] == 0);
        observe.close();
        store.close();
    }
}

void test_delayed_outcome_and_censoring() {
    current_test = "delayed_outcome_and_censoring";
    TempDirectory temp;
    {
        schiva::LearningStore store(temp.file("reward.sqlite3"));
        schiva::Controller controller(store);
        schiva::Decision decision = trigger(controller);
        CHECK(decision.recommendation.has_value());
        schiva::Rect rect = kPip;
        double now = 0.04;
        while (controller.status()["motion_active"] == true && now < 1.0) {
            if (decision.move_to.has_value()) {
                rect = rect.moved_to(decision.move_to->x,
                                     decision.move_to->y);
            }
            now += 1.0 / 120.0;
            decision = controller.tick(now, observation(rect));
        }
        while (controller.status()["learning_count"] == 0 && now < 15.0) {
            now += 0.25;
            controller.tick(
                now, observation(rect, {1100, 700}, false, "pip-handle",
                                 std::nullopt, {}));
        }
        CHECK(controller.status()["learning_count"] == 1);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("censor.sqlite3"));
        schiva::Controller controller(store);
        trigger(controller);
        for (int index = 0; index < 50; ++index) {
            controller.tick(0.2 + index * 0.25,
                            observation(kPip, {510, 250}, false,
                                        "replacement-pip"));
        }
        CHECK(controller.status()["learning_count"] == 0);
        controller.close();
        store.close();
    }
}

void test_visual_helper_and_insights() {
    current_test = "visual_helper_and_insights";
    TempDirectory temp;
    schiva::LearningStore store(temp.file("visual.sqlite3"));
    {
        schiva::Controller controller(store);
        auto frame = [](int level) {
            schiva::VisualFrame result;
            result.width = 80;
            result.height = 45;
            result.pixels.assign(80 * 45, 60);
            for (int y = 22; y < 30; ++y) {
                for (int x = 30; x < 37; ++x) {
                    result.pixels[static_cast<std::size_t>(y) * 80 + x] =
                        static_cast<std::uint8_t>(level);
                }
            }
            result.bounds = {0, 0, 800, 450};
            result.excluded = {{100, 80, 300, 220}};
            return result;
        };
        const schiva::Rect work{0, 0, 800, 450};
        auto local_observation = [&](int level) {
            schiva::Observation result;
            result.cursor = {700, 400};
            result.pip =
                schiva::WindowInfo{"p", "chrome.exe", "pip",
                                   {100, 80, 300, 220}};
            result.foreground =
                schiva::WindowInfo{"f", "editor.exe", "Editor", work};
            result.work_area = work;
            result.visual_frame = frame(level);
            return result;
        };
        controller.tick(0.0, local_observation(60));
        controller.tick(0.5, local_observation(210));
        const schiva::Decision visual =
            controller.tick(1.0, local_observation(140));
        CHECK(visual.reason == "sustained_visual");
        CHECK(visual.move_to.has_value());
        CHECK(controller.status()["visual_available"] == true);

        const schiva::Json snapshot = controller.insights();
        CHECK(snapshot["columns"] == 24);
        CHECK(snapshot["rows"] == 14);
        CHECK(snapshot["clearance_goal"] == 0.65);
        CHECK(snapshot["layers"]["activity"].size() == 336);
        CHECK(snapshot["layers"]["manual"].size() == 336);
        CHECK(snapshot["layers"]["placement"].size() == 336);

        bool wrong_thread_error = false;
        std::thread worker([&]() {
            try {
                (void)controller.insights();
            } catch (const std::runtime_error&) {
                wrong_thread_error = true;
            }
        });
        worker.join();
        CHECK(wrong_thread_error);

        const schiva::Decision helper = controller.tick(
            1.1, observation(kPip, {510, 250}, false, "pip-handle",
                             std::nullopt, {{450, 200, 650, 320}}));
        CHECK(helper.state == "helper_interaction");
        CHECK(!controller.status()["motion_active"].get<bool>());
        CHECK(controller.status()["learning_count"] == 0);
        controller.close();
    }
    store.close();

    {
        schiva::LearningStore cold_store(temp.file("cold.sqlite3"));
        schiva::Controller cold(cold_store);
        cold.tick(0.0, observation());
        const schiva::Json cold_insights = cold.insights();
        for (const auto& layer :
             cold_insights["layers"].items()) {
            CHECK(layer.value().size() == 336);
            CHECK(std::all_of(
                layer.value().begin(), layer.value().end(),
                [](const schiva::Json& value) { return value.is_null(); }));
        }
        CHECK(cold.insights("never-created")["selected_profile"].is_null());
        cold.close();
        cold_store.close();
    }
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests = {
        {"domain_and_linucb", test_domain_and_linucb},
        {"geometry_activity_visual", test_geometry_activity_visual},
        {"storage_schema_and_fixture", test_storage_schema_and_fixture},
        {"storage_roundtrip_and_bounds", test_storage_roundtrip_and_bounds},
        {"smooth_motion_and_clearance", test_smooth_motion_and_clearance},
        {"holds_stalls_manual_and_observe_only",
         test_holds_stalls_manual_and_observe_only},
        {"delayed_outcome_and_censoring", test_delayed_outcome_and_censoring},
        {"visual_helper_and_insights", test_visual_helper_and_insights}};
    for (const auto& [name, test] : tests) {
        try {
            test();
        } catch (const std::exception& error) {
            std::cerr << "FAIL [" << name << "] uncaught exception: "
                      << error.what() << '\n';
            ++failures;
        }
    }
    if (failures != 0) {
        std::cerr << failures << " core check(s) failed\n";
        return 1;
    }
    std::cout << "core checks passed (" << tests.size() << " groups)\n";
    return 0;
}
