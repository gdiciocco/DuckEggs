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
const schiva::Rect kVisualBounds{0, 0, 800, 450};
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

schiva::VisualFrame frame_with_background(
    std::uint8_t background,
    std::vector<schiva::Rect> excluded = {}) {
    schiva::VisualFrame result;
    result.width = 64;
    result.height = 36;
    result.pixels.assign(
        static_cast<std::size_t>(result.width) * result.height, background);
    result.bounds = kVisualBounds;
    result.excluded = std::move(excluded);
    return result;
}

void set_pixel_region(schiva::VisualFrame& frame, schiva::Rect area,
                      std::uint8_t level) {
    for (int y = std::max(0, area.top);
         y < std::min(frame.height, area.bottom); ++y) {
        for (int x = std::max(0, area.left);
             x < std::min(frame.width, area.right); ++x) {
            frame.pixels[static_cast<std::size_t>(y) * frame.width + x] =
                level;
        }
    }
}

void set_changed_tile(schiva::VisualFrame& frame, int column, int row,
                      std::uint8_t level) {
    set_pixel_region(frame, {column * 4, row * 4, column * 4 + 4,
                             row * 4 + 4},
                     level);
}

schiva::VisualFrame striped_frame(bool inverted) {
    schiva::VisualFrame result = frame_with_background(0, {kPip});
    for (int y = 0; y < result.height; ++y) {
        const bool upper_half = y % 4 < 2;
        const auto level = static_cast<std::uint8_t>(
            inverted == upper_half ? 220 : 20);
        for (int x = 0; x < result.width; ++x) {
            result.pixels[static_cast<std::size_t>(y) * result.width + x] =
                level;
        }
    }
    return result;
}

schiva::VisualFrame textlike_frame(std::uint8_t ink) {
    schiva::VisualFrame result = frame_with_background(40, {kPip});
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            if (x % 4 < 2 && y % 4 < 2) {
                result.pixels[static_cast<std::size_t>(y) * result.width +
                              x] = ink;
            }
        }
    }
    return result;
}

schiva::VisualFrame local_visual_frame(std::uint8_t level) {
    schiva::VisualFrame result = frame_with_background(60, {kPip});
    set_pixel_region(result, {36, 24, 44, 28}, level);
    return result;
}

schiva::Decision trigger(schiva::Controller& controller,
                         const schiva::Observation& current = observation()) {
    controller.tick(0.0, current);
    return controller.tick(0.04, current);
}

void execute_sql(sqlite3* database, const char* sql) {
    char* message = nullptr;
    const int result =
        sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (result == SQLITE_OK) {
        return;
    }
    const std::string detail =
        message == nullptr ? sqlite3_errmsg(database) : message;
    sqlite3_free(message);
    throw std::runtime_error("synthetic schema setup failed: " + detail);
}

void bind_text(sqlite3_stmt* statement, int index,
               const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(),
                          static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error("synthetic schema text bind failed");
    }
}

void create_synthetic_schema1_fixture(
    const std::filesystem::path& path,
    const schiva::Json& model_state) {
    sqlite3* database = nullptr;
    if (sqlite3_open(path.string().c_str(), &database) != SQLITE_OK) {
        if (database != nullptr) {
            sqlite3_close(database);
        }
        throw std::runtime_error("unable to create synthetic schema fixture");
    }
    try {
        execute_sql(
            database,
            "PRAGMA user_version = 1;"
            "CREATE TABLE profiles ("
            "profile_key TEXT PRIMARY KEY,"
            "app_id TEXT NOT NULL,"
            "role TEXT NOT NULL,"
            "layout_bin TEXT NOT NULL,"
            "short_map TEXT NOT NULL,"
            "long_map TEXT NOT NULL,"
            "manual_map TEXT NOT NULL);"
            "CREATE TABLE decision_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "profile_key TEXT NOT NULL,"
            "features_json TEXT NOT NULL,"
            "action TEXT NOT NULL,"
            "reward REAL NOT NULL);"
            "CREATE TABLE model_state ("
            "id INTEGER PRIMARY KEY CHECK (id = 1),"
            "state_json TEXT NOT NULL,"
            "updates INTEGER NOT NULL);");

        const struct {
            const char* key;
            const char* app;
            const char* role;
            const char* layout;
            const char* short_map;
            const char* long_map;
            const char* manual_map;
        } profiles[] = {
            {"editor.exe|editor|maximized", "editor.exe", "editor",
             "maximized", R"({"4":1.25,"9":0.5})",
             R"({"4":2.5,"12":1.0})", R"({"9":0.75})"},
            {"browser.exe|document|snapped-left", "browser.exe",
             "document", "snapped-left", R"({"2":3.0})",
             R"({"2":1.5})", "{}"},
            {"terminal.exe|terminal|free-8x6-normal", "terminal.exe",
             "terminal", "free-8x6-normal", R"({"20":0.25})",
             R"({"20":0.5})", R"({"20":1.0})"}};
        sqlite3_stmt* profile_insert = nullptr;
        const char* profile_sql =
            "INSERT INTO profiles "
            "(profile_key, app_id, role, layout_bin, short_map, "
            "long_map, manual_map) VALUES (?, ?, ?, ?, ?, ?, ?)";
        if (sqlite3_prepare_v2(database, profile_sql, -1, &profile_insert,
                               nullptr) != SQLITE_OK) {
            throw std::runtime_error(
                "unable to prepare synthetic profiles");
        }
        for (const auto& profile : profiles) {
            bind_text(profile_insert, 1, profile.key);
            bind_text(profile_insert, 2, profile.app);
            bind_text(profile_insert, 3, profile.role);
            bind_text(profile_insert, 4, profile.layout);
            bind_text(profile_insert, 5, profile.short_map);
            bind_text(profile_insert, 6, profile.long_map);
            bind_text(profile_insert, 7, profile.manual_map);
            if (sqlite3_step(profile_insert) != SQLITE_DONE) {
                sqlite3_finalize(profile_insert);
                throw std::runtime_error(
                    "unable to insert synthetic profile");
            }
            sqlite3_reset(profile_insert);
            sqlite3_clear_bindings(profile_insert);
        }
        sqlite3_finalize(profile_insert);

        sqlite3_stmt* decision_insert = nullptr;
        const char* decision_sql =
            "INSERT INTO decision_records "
            "(profile_key, features_json, action, reward) "
            "VALUES (?, ?, ?, ?)";
        if (sqlite3_prepare_v2(database, decision_sql, -1,
                               &decision_insert, nullptr) != SQLITE_OK) {
            throw std::runtime_error(
                "unable to prepare synthetic decisions");
        }
        const struct {
            const char* key;
            const char* features;
            const char* action;
            double reward;
        } decisions[] = {
            {"editor.exe|editor|maximized", "[1,0.5,0.25,0,0,0,0,0,0,0,0,0]",
             "anchor-03", 0.1},
            {"editor.exe|editor|maximized", "[1,0.25,0.5,0,0,0,0,0,0,0,0,0]",
             "anchor-07", -0.2},
            {"browser.exe|document|snapped-left",
             "[1,-0.5,0.25,0,0,0,0,0,0,0,0,0]", "anchor-11", 0.3},
            {"terminal.exe|terminal|free-8x6-normal",
             "[1,0,0.5,0,0,0,0,0,0,0,0,0]", "anchor-19", 0.0}};
        for (const auto& decision : decisions) {
            bind_text(decision_insert, 1, decision.key);
            bind_text(decision_insert, 2, decision.features);
            bind_text(decision_insert, 3, decision.action);
            if (sqlite3_bind_double(decision_insert, 4,
                                    decision.reward) != SQLITE_OK ||
                sqlite3_step(decision_insert) != SQLITE_DONE) {
                sqlite3_finalize(decision_insert);
                throw std::runtime_error(
                    "unable to insert synthetic decision");
            }
            sqlite3_reset(decision_insert);
            sqlite3_clear_bindings(decision_insert);
        }
        sqlite3_finalize(decision_insert);

        sqlite3_stmt* model_insert = nullptr;
        if (sqlite3_prepare_v2(
                database,
                "INSERT INTO model_state (id, state_json, updates) "
                "VALUES (1, ?, ?)", -1, &model_insert,
                nullptr) != SQLITE_OK) {
            throw std::runtime_error(
                "unable to prepare synthetic model state");
        }
        const std::string state_json = model_state.dump();
        bind_text(model_insert, 1, state_json);
        sqlite3_bind_int(
            model_insert, 2, model_state.at("updates").get<int>());
        if (sqlite3_step(model_insert) != SQLITE_DONE) {
            sqlite3_finalize(model_insert);
            throw std::runtime_error(
                "unable to insert synthetic model state");
        }
        sqlite3_finalize(model_insert);
        sqlite3_close(database);
    } catch (...) {
        sqlite3_close(database);
        throw;
    }
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

void test_broad_visual_detection() {
    current_test = "broad_visual_detection";
    schiva::detail::VisualActivityTracker balanced(16, 9);
    const auto balanced_first =
        balanced.observe(0.0, striped_frame(false), kPip, "context");
    const auto balanced_scroll =
        balanced.observe(0.25, striped_frame(true), kPip, "context");
    CHECK(!balanced_first.broad);
    CHECK(balanced_scroll.broad);
    CHECK(!balanced_scroll.burst);
    CHECK(!balanced_scroll.sustained);
    CHECK(!balanced_scroll.conflict);
    CHECK(balanced.broad_active_at(0.26));
    CHECK(!balanced.grace_active_at(0.26));
    CHECK(!balanced.conflict_for(kPip));
    CHECK_NEAR(balanced.risk_for({300, 100, 400, 200}, std::nullopt,
                                0.26),
               0.0, 1e-12);
    CHECK(std::all_of(balanced.burst_mask().begin(),
                      balanced.burst_mask().end(),
                      [](double value) { return value == 0.0; }));
    CHECK(std::all_of(balanced.sustained_mask().begin(),
                      balanced.sustained_mask().end(),
                      [](double value) { return value == 0.0; }));

    schiva::detail::VisualActivityTracker text(16, 9);
    text.observe(0.0, frame_with_background(40, {kPip}), kPip, "context");
    const auto text_redraw =
        text.observe(0.25, textlike_frame(200), kPip, "context");
    CHECK(text_redraw.broad);

    schiva::detail::VisualActivityTracker successive(16, 9);
    auto partial = frame_with_background(0);
    for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 8; ++column) {
            set_changed_tile(partial, column, row, 200);
        }
    }
    auto partial_successor = frame_with_background(0);
    for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 8; ++column) {
            set_changed_tile(partial_successor, column, row, 100);
        }
    }
    successive.observe(0.0, frame_with_background(0), std::nullopt,
                       "context");
    const auto partial_first =
        successive.observe(0.25, partial, std::nullopt, "context");
    const auto partial_second =
        successive.observe(0.5, partial_successor, std::nullopt,
                           "context");
    CHECK(!partial_first.broad);
    CHECK(partial_second.broad);

    schiva::detail::VisualActivityTracker static_partial(16, 9);
    static_partial.observe(0.0, frame_with_background(0), std::nullopt,
                           "context");
    static_partial.observe(0.25, partial, std::nullopt, "context");
    CHECK(!static_partial
               .observe(0.5, partial, std::nullopt, "context")
               .broad);

    schiva::detail::VisualActivityTracker long_gap(16, 9);
    long_gap.observe(0.0, frame_with_background(0), std::nullopt,
                     "context");
    CHECK(!long_gap.observe(0.25, partial, std::nullopt, "context").broad);
    CHECK(!long_gap.observe(1.76, partial_successor, std::nullopt,
                            "context")
               .broad);

    schiva::detail::VisualActivityTracker heavily_masked(16, 9);
    const std::vector<schiva::Rect> heavy_mask{
        {450, 0, 800, 450}, {0, 300, 800, 450}};
    auto heavy_before = frame_with_background(0, heavy_mask);
    auto heavy_after = frame_with_background(0, heavy_mask);
    set_pixel_region(heavy_after, {0, 0, 36, 24}, 220);
    heavily_masked.observe(0.0, heavy_before, std::nullopt, "context");
    const auto sparse_valid =
        heavily_masked.observe(0.25, heavy_after, std::nullopt, "context");
    CHECK(!sparse_valid.broad);

    schiva::detail::VisualActivityTracker tiny(16, 9);
    auto scattered = frame_with_background(60);
    for (int row = 0; row < 9; ++row) {
        for (int column = 0; column < 16; ++column) {
            scattered.pixels[static_cast<std::size_t>(row * 4) * 64 +
                             column * 4] = 220;
        }
    }
    tiny.observe(0.0, frame_with_background(60), std::nullopt, "context");
    CHECK(!tiny.observe(0.25, scattered, std::nullopt, "context").broad);

    schiva::detail::VisualActivityTracker masked(16, 9);
    masked.observe(0.0, frame_with_background(0, {kVisualBounds}), kPip,
                   "context");
    const auto all_masked = masked.observe(
        0.25, frame_with_background(240, {kVisualBounds}), kPip,
        "context");
    CHECK(!all_masked.broad);
    CHECK_NEAR(all_masked.change_ratio, 0.0, 1e-12);

    schiva::detail::VisualActivityTracker local_then_global(16, 9);
    local_then_global.observe(0.0, local_visual_frame(60), kPip,
                              "context");
    local_then_global.observe(0.5, local_visual_frame(210), kPip,
                              "context");
    const auto local =
        local_then_global.observe(1.0, local_visual_frame(140), kPip,
                                  "context");
    CHECK(local.sustained);
    CHECK(!local.broad);
    const auto global = local_then_global.observe(
        1.5, frame_with_background(220, {kPip}), kPip, "context");
    CHECK(global.broad);
    CHECK(std::all_of(local_then_global.sustained_mask().begin(),
                      local_then_global.sustained_mask().end(),
                      [](double value) { return value == 0.0; }));

    schiva::detail::VisualActivityTracker no_frame(16, 9);
    no_frame.observe(0.0, frame_with_background(0, {kPip}), kPip,
                     "context");
    no_frame.observe(0.5, frame_with_background(220, {kPip}), kPip,
                     "context");
    CHECK(no_frame.broad_active_at(1.49));
    CHECK(!no_frame.broad_active_at(1.5));

    schiva::detail::VisualActivityTracker geometry(16, 9);
    geometry.observe(0.0, frame_with_background(0, {kPip}), kPip,
                     "context");
    geometry.observe(0.5, frame_with_background(220, {kPip}), kPip,
                     "context");
    auto changed_bounds = frame_with_background(220, {kPip});
    changed_bounds.bounds = {10, 10, 1930, 1090};
    const auto reset_geometry =
        geometry.observe(0.75, changed_bounds, kPip, "context");
    CHECK(!reset_geometry.broad);
    CHECK(!geometry.broad_active_at(0.75));
}

void test_broad_controller_suppression() {
    current_test = "broad_controller_suppression";
    TempDirectory temp;
    {
        schiva::LearningStore store(temp.file("broad.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Point far{700, 400};
        controller.tick(
            0.0, observation(kPip, far, false, "pip-handle",
                             local_visual_frame(60)));
        const schiva::Decision first = controller.tick(
            0.5, observation(kPip, far, false, "pip-handle",
                             frame_with_background(220, {kPip})));
        const schiva::Decision second = controller.tick(
            1.0, observation(kPip, far, false, "pip-handle",
                             frame_with_background(180, {kPip})));
        CHECK(!first.move_to.has_value());
        CHECK(!second.move_to.has_value());
        CHECK(controller.status()["visual_broad_update"] == true);
        const schiva::Decision expired = controller.tick(
            2.1, observation(kPip, far));
        CHECK(!expired.move_to.has_value());
        CHECK(!controller.status().contains("visual_broad_update"));
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("cursor.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Point far{700, 400};
        const schiva::Point near{500, 250};
        controller.tick(0.0, observation(kPip, far, false, "pip-handle",
                                         frame_with_background(60, {kPip})));
        controller.tick(0.04,
                        observation(kPip, near, false, "pip-handle",
                                    frame_with_background(220, {kPip})));
        const schiva::Decision fleeing = controller.tick(
            0.08, observation(kPip, near, false, "pip-handle",
                              frame_with_background(180, {kPip})));
        CHECK(fleeing.reason == "cursor_near");
        CHECK(fleeing.move_to.has_value());
        CHECK(controller.status()["visual_broad_update"] == true);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("recovery.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Point far{700, 400};
        auto normal = frame_with_background(60, {kPip});
        controller.tick(0.0, observation(kPip, far, false, "pip-handle",
                                         normal));
        controller.tick(0.5, observation(kPip, far, false, "pip-handle",
                                         frame_with_background(220, {kPip})));
        controller.tick(1.0, observation(kPip, far, false, "pip-handle",
                                         normal));
        const schiva::Decision held = controller.tick(
            1.5, observation(kPip, far, false, "pip-handle", normal));
        CHECK(!held.move_to.has_value());
        controller.tick(2.1,
                        observation(kPip, far, false, "pip-handle",
                                    local_visual_frame(140)));
        const schiva::Decision localized = controller.tick(
            2.6, observation(kPip, far, false, "pip-handle",
                             local_visual_frame(210)));
        CHECK(localized.reason == "sustained_visual");
        CHECK(localized.move_to.has_value());
        CHECK(!controller.status().contains("visual_broad_update"));
        controller.close();
        store.close();
    }
}

void test_broad_motion_and_reward_safety() {
    current_test = "broad_motion_and_reward_safety";
    TempDirectory temp;
    const schiva::Point far{700, 400};
    const schiva::Point near{510, 250};
    const auto completed_cursor_motion =
        [&](schiva::Controller& controller) {
            controller.tick(
                0.0, observation(kPip, near, false, "pip-handle",
                                 local_visual_frame(60)));
            schiva::Decision decision = controller.tick(
                0.04, observation(kPip, near, false, "pip-handle",
                                  local_visual_frame(60)));
            schiva::Rect rect = kPip;
            double now = 0.04;
            while (controller.status()["motion_active"] == true &&
                   decision.move_to.has_value() && now < 2.0) {
                rect = rect.moved_to(decision.move_to->x,
                                     decision.move_to->y);
                now += 1.0 / 120.0;
                decision = controller.tick(
                    now, observation(rect, near, false, "pip-handle",
                                     local_visual_frame(60)));
            }
            CHECK(!controller.status()["motion_active"].get<bool>());
            return std::pair{rect, now};
        };

    {
        schiva::LearningStore store(temp.file("visual-cancel.sqlite3"));
        schiva::Controller controller(store);
        controller.tick(0.0, observation(kPip, far, false, "pip-handle",
                                         local_visual_frame(60)));
        controller.tick(0.5, observation(kPip, far, false, "pip-handle",
                                         local_visual_frame(210)));
        const schiva::Decision moving = controller.tick(
            1.0, observation(kPip, far, false, "pip-handle",
                             local_visual_frame(140)));
        CHECK(moving.reason == "sustained_visual");
        CHECK(moving.move_to.has_value());
        const schiva::Decision cancelled = controller.tick(
            1.04, observation(kPip, far, false, "pip-handle",
                              frame_with_background(220, {kPip})));
        CHECK(!cancelled.move_to.has_value());
        CHECK(controller.status()["motion_active"] == false);
        CHECK(controller.status()["learning_count"] == 0);
        CHECK(cancelled.state != "movement_failed");
        CHECK(cancelled.state != "manual_correction");
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("cursor-preserved.sqlite3"));
        schiva::Controller controller(store);
        controller.tick(0.0, observation(kPip, near, false, "pip-handle",
                                         local_visual_frame(60)));
        const schiva::Decision moving = controller.tick(
            0.04, observation(kPip, near, false, "pip-handle",
                              local_visual_frame(60)));
        CHECK(moving.reason == "cursor_near");
        CHECK(moving.move_to.has_value());
        const schiva::Rect applied = kPip.moved_to(
            moving.move_to->x, moving.move_to->y);
        controller.tick(
            0.08, observation(applied, near, false, "pip-handle",
                              frame_with_background(220, {kPip})));
        CHECK(controller.status()["motion_active"] == true);
        CHECK(controller.status()["visual_broad_update"] == true);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("quiet-censor.sqlite3"));
        schiva::Controller controller(store);
        auto [rect, now] = completed_cursor_motion(controller);
        now += 0.05;
        controller.tick(
            now, observation(rect, far, false, "pip-handle",
                             frame_with_background(220, {kPip})));
        CHECK(controller.status()["visual_broad_update"] == true);
        for (int index = 0; index < 60; ++index) {
            now += 0.25;
            controller.tick(now,
                            observation(rect, far, false, "pip-handle",
                                        local_visual_frame(60)));
        }
        CHECK(controller.status()["learning_count"] == 0);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("return-feedback.sqlite3"));
        schiva::Controller controller(store);
        auto [rect, now] = completed_cursor_motion(controller);
        schiva::Point returning{rect.left - 30, rect.top - 30};
        if (returning.x < 0 || returning.y < 0) {
            returning = {rect.right + 30, rect.bottom + 30};
        }
        now += 0.05;
        controller.tick(
            now, observation(rect, returning, false, "pip-handle",
                             frame_with_background(220, {kPip})));
        CHECK(controller.status()["visual_broad_update"] == true);
        now += 0.13;
        controller.tick(now, observation(rect, returning, false,
                                         "pip-handle"));
        CHECK(controller.status()["learning_count"] == 1);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("caret-feedback.sqlite3"));
        schiva::Controller controller(store);
        auto [rect, now] = completed_cursor_motion(controller);
        auto caret_observation =
            observation(rect, far, false, "pip-handle",
                        frame_with_background(220, {kPip}));
        caret_observation.caret = schiva::Rect{
            rect.left + 20, rect.top + 20, rect.left + 40, rect.top + 40};
        now += 0.05;
        controller.tick(now, caret_observation);
        CHECK(controller.status()["visual_broad_update"] == true);
        CHECK(controller.status()["learning_count"] == 1);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("manual-preserved.sqlite3"));
        schiva::Controller controller(store);
        auto [rect, now] = completed_cursor_motion(controller);
        const schiva::Rect corrected{500, 400, 820, 580};
        now += 0.05;
        const schiva::Decision manual = controller.tick(
            now, observation(corrected, far, false, "pip-handle",
                             frame_with_background(220, {kPip})));
        CHECK(manual.state == "manual_correction");
        CHECK(controller.status()["learning_count"] == 1);
        controller.close();
        store.close();
    }
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

    schiva::detail::LinUCBModel synthetic_model(12, 0.35);
    const std::vector<std::pair<std::vector<double>, double>> updates = {
        {{1.0, 0.5, 0.25, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
         0.1},
        {{1.0, 0.25, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
         -0.2},
        {{1.0, -0.5, 0.25, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
         0.3},
        {{1.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
         0.0}};
    for (const auto& update : updates) {
        synthetic_model.update(update.first, update.second);
    }
    const auto synthetic = temp.file("synthetic-schema1.sqlite3");
    create_synthetic_schema1_fixture(synthetic, synthetic_model.state());
    schiva::LearningStore store(synthetic);
    const schiva::Json stats = store.stats();
    CHECK(stats["schema_version"] == 1);
    CHECK(stats["profiles"] == 3);
    CHECK(stats["decision_records"] == 4);
    const auto profiles = schiva::detail::StoreAccess::list_profiles(store);
    CHECK(profiles.size() == 3);
    CHECK(profiles.size() == 3 &&
          profiles[0].profile_key == "browser.exe|document|snapped-left");
    const auto editor = schiva::detail::StoreAccess::peek_profile(
        store, "editor.exe|editor|maximized");
    CHECK(editor.has_value());
    CHECK(editor.has_value() && editor->short_map["4"] == 1.25);
    CHECK(editor.has_value() && editor->long_map["4"] == 2.5);
    CHECK(editor.has_value() && editor->manual_map["9"] == 0.75);
    const auto model_json =
        schiva::detail::StoreAccess::load_model(store);
    CHECK(model_json.has_value());
    CHECK(model_json.has_value() &&
          model_json->at("dimension") == 12);
    CHECK(model_json.has_value() &&
          model_json->at("updates").get<int>() == 4);
    if (model_json.has_value()) {
        const auto restored =
            schiva::detail::LinUCBModel::from_state(*model_json);
        const std::vector<double> query{
            1.0, 0.25, 0.5, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        CHECK_NEAR(restored.predict(query),
                   synthetic_model.predict(query), 1e-12);
    }
    schiva::Controller controller(store);
    CHECK(controller.status()["learning_count"] == 4);
    controller.close();
    store.close();

    schiva::LearningStore reopened(synthetic);
    CHECK(reopened.stats()["profiles"] == 3);
    CHECK(reopened.stats()["decision_records"] == 4);
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
        const auto target = decision.recommendation;
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
        CHECK(decision.state == "cooldown");
        CHECK(!decision.move_to.has_value());
        if (target.has_value()) {
            CHECK(schiva::detail::vacated_fraction(
                      kPip, *target) >= 0.65);
            CHECK(rect == *target);
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

        const schiva::Rect interpolation_collision{140, 100, 460, 280};
        controller.tick(
            0.35,
            observation(interpolation_collision, {510, 250}, true));
        const schiva::Decision collision_release = controller.tick(
            0.36, observation(interpolation_collision));
        CHECK(collision_release.state == "moving_stalled");
        CHECK(controller.status()["learning_count"] == 0);

        const schiva::Rect corrected{500, 400, 820, 580};
        controller.tick(0.40, observation(corrected, {900, 700}, true));
        const schiva::Decision manual =
            controller.tick(0.41, observation(corrected, {900, 700}));
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
        CHECK(snapshot["note"] ==
              "Active profile. Usage and preference maps are learned data; "
              "live visual risk covers only observable pixels. Placement is "
              "a relative ranking for the entire PiP rectangle.");
        const schiva::Json unknown = controller.insights("never-created");
        CHECK(unknown["note"] ==
              "The requested profile is not learned; no profile was "
              "created.");

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
        const schiva::Json cold_insights = cold.insights();
        CHECK(cold_insights["note"] ==
              "No learned app profiles are available; no profile was "
              "created.");
        cold.tick(0.0, observation());
        const schiva::Json cold_after_tick = cold.insights();
        for (const auto& layer :
             cold_after_tick["layers"].items()) {
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
        {"broad_visual_detection", test_broad_visual_detection},
        {"broad_controller_suppression",
         test_broad_controller_suppression},
        {"broad_motion_and_reward_safety",
         test_broad_motion_and_reward_safety},
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
