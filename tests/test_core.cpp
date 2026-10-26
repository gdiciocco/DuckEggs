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

schiva::VisualFrame tiled_frame(
    int seed, std::vector<schiva::Rect> excluded = {kPip}) {
    schiva::VisualFrame result = frame_with_background(0, std::move(excluded));
    for (int row = 0; row < 9; ++row) {
        for (int column = 0; column < 16; ++column) {
            const int level =
                8 + 15 * ((column * 5 + row * 3 + seed * 4) % 15);
            set_changed_tile(result, column, row,
                             static_cast<std::uint8_t>(level));
        }
    }
    return result;
}

schiva::Decision trigger(schiva::Controller& controller,
                         const schiva::Observation& current = observation()) {
    controller.tick(0.0, current);
    return controller.tick(0.04, current);
}

std::string manual_cell_for(const schiva::Point& point,
                            const schiva::Rect& reference) {
    const int column = std::clamp(
        (point.x - reference.left) * schiva::detail::kMapColumns /
            reference.width(),
        0, schiva::detail::kMapColumns - 1);
    const int row = std::clamp(
        (point.y - reference.top) * schiva::detail::kMapRows /
            reference.height(),
        0, schiva::detail::kMapRows - 1);
    return std::to_string(row * schiva::detail::kMapColumns + column);
}

schiva::Point near_outside_point(const schiva::Rect& rect) {
    const int middle_x = rect.left + rect.width() / 2;
    const int middle_y = rect.top + rect.height() / 2;
    const std::vector<schiva::Point> candidates = {
        {rect.left - 30, middle_y},
        {rect.right + 30, middle_y},
        {middle_x, rect.top - 30},
        {middle_x, rect.bottom + 30}};
    for (const schiva::Point& candidate : candidates) {
        if (kWork.left <= candidate.x &&
            candidate.x < kWork.right &&
            kWork.top <= candidate.y &&
            candidate.y < kWork.bottom &&
            !schiva::detail::contains_point_with_margin(
                rect, candidate, schiva::detail::kInnerMargin)) {
            return candidate;
        }
    }
    throw std::runtime_error("no nearby point outside PiP in work area");
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

void test_content_transition_detection() {
    current_test = "content_transition_detection";
    schiva::detail::VisualActivityTracker settled;
    int transitions = 0;
    const auto observe = [&](double now, const schiva::VisualFrame& frame,
                             std::optional<schiva::Rect> pip = kPip,
                             std::string context = "context") {
        const auto evidence =
            settled.observe(now, frame, pip, context);
        transitions += evidence.content_transition ? 1 : 0;
        return evidence;
    };
    observe(0.0, tiled_frame(0));
    const auto first_broad = observe(0.25, tiled_frame(1));
    CHECK(first_broad.broad);
    CHECK(!first_broad.content_transition);
    CHECK(!observe(0.50, tiled_frame(1)).content_transition);
    CHECK(!observe(0.75, tiled_frame(1)).content_transition);
    const auto to_b = observe(1.00, tiled_frame(1));
    CHECK(to_b.content_transition);
    CHECK(!observe(1.25, tiled_frame(1)).content_transition);
    CHECK(!observe(1.75, tiled_frame(0)).content_transition);
    CHECK(!observe(2.00, tiled_frame(0)).content_transition);
    CHECK(!observe(2.25, tiled_frame(0)).content_transition);
    CHECK(observe(2.50, tiled_frame(0)).content_transition);
    CHECK(transitions == 2);

    schiva::detail::VisualActivityTracker short_animation;
    short_animation.observe(0.0, tiled_frame(0), kPip, "context");
    short_animation.observe(0.25, tiled_frame(1), kPip, "context");
    short_animation.observe(0.50, tiled_frame(1), kPip, "context");
    bool short_transition = false;
    for (int index = 0; index < 5; ++index) {
        short_transition =
            short_transition ||
            short_animation
                .observe(0.75 + index * 0.25, tiled_frame(0), kPip,
                         "context")
                .content_transition;
    }
    CHECK(!short_transition);

    schiva::detail::VisualActivityTracker scrolling;
    scrolling.observe(0.0, tiled_frame(0), kPip, "context");
    bool scroll_transition = false;
    for (int seed = 1; seed <= 12; ++seed) {
        scroll_transition =
            scroll_transition ||
            scrolling
                .observe(seed * 0.25, tiled_frame(seed), kPip,
                         "context")
                .content_transition;
    }
    CHECK(!scroll_transition);

    schiva::detail::VisualActivityTracker slow_ramp;
    int slow_ramp_transitions = 0;
    const auto observe_uniform =
        [&](double now, std::uint8_t level) {
            const auto evidence = slow_ramp.observe(
                now, frame_with_background(level, {kPip}), kPip,
                "context");
            slow_ramp_transitions +=
                evidence.content_transition ? 1 : 0;
            return evidence;
        };
    observe_uniform(0.0, 0);
    const std::vector<std::uint8_t> ramp{
        24, 36, 48, 60, 72, 84, 96, 108, 120, 132, 144, 156};
    for (std::size_t index = 0; index < ramp.size(); ++index) {
        CHECK(!observe_uniform(0.25 * (index + 1), ramp[index])
                   .content_transition);
    }
    CHECK(slow_ramp_transitions == 0);
    CHECK(!observe_uniform(3.25, 180).content_transition);
    CHECK(!observe_uniform(3.50, 180).content_transition);
    CHECK(!observe_uniform(3.75, 180).content_transition);
    CHECK(observe_uniform(4.00, 180).content_transition);
    for (int index = 0; index < 8; ++index) {
        CHECK(!observe_uniform(4.25 + index * 0.25, 180)
                   .content_transition);
    }
    CHECK(slow_ramp_transitions == 1);

    schiva::detail::VisualActivityTracker oscillation;
    int oscillation_transitions = 0;
    const auto observe_oscillation =
        [&](double now, std::uint8_t level) {
            const auto evidence = oscillation.observe(
                now, frame_with_background(level, {kPip}), kPip,
                "context");
            oscillation_transitions +=
                evidence.content_transition ? 1 : 0;
            return evidence;
        };
    observe_oscillation(0.0, 36);
    for (int index = 0; index < 12; ++index) {
        CHECK(!observe_oscillation(
                   0.25 * (index + 1),
                   static_cast<std::uint8_t>(
                       index % 2 == 0 ? 48 : 36))
                   .content_transition);
    }
    CHECK(oscillation_transitions == 0);
    CHECK(!observe_oscillation(3.25, 200).content_transition);
    CHECK(!observe_oscillation(3.50, 200).content_transition);
    CHECK(!observe_oscillation(3.75, 200).content_transition);
    CHECK(observe_oscillation(4.00, 200).content_transition);
    CHECK(oscillation_transitions == 1);

    schiva::detail::VisualActivityTracker localized;
    localized.observe(0.0, tiled_frame(0), kPip, "context");
    bool local_transition = false;
    for (int phase = 1; phase <= 10; ++phase) {
        auto dynamic = tiled_frame(0);
        set_pixel_region(
            dynamic, {28, 16, 40, 28},
            static_cast<std::uint8_t>(20 + phase * 20));
        local_transition =
            local_transition ||
            localized
                .observe(phase * 0.25, dynamic, kPip, "context")
                .content_transition;
    }
    CHECK(!local_transition);

    schiva::detail::VisualActivityTracker excluded_only;
    excluded_only.observe(0.0, frame_with_background(60, {kPip}), kPip,
                          "context");
    bool excluded_transition = false;
    for (int index = 0; index < 4; ++index) {
        auto changed = frame_with_background(60, {kPip});
        set_pixel_region(changed, {8, 8, 34, 22}, 220);
        excluded_transition =
            excluded_transition ||
            excluded_only
                .observe(0.25 + index * 0.25, changed, kPip, "context")
                .content_transition;
    }
    CHECK(!excluded_transition);

    schiva::detail::VisualActivityTracker moved_mask;
    moved_mask.observe(0.0, tiled_frame(0), kPip, "context");
    const schiva::Rect moved_pip{500, 250, 720, 430};
    bool moved_mask_transition = false;
    for (int index = 0; index < 4; ++index) {
        auto revealed = tiled_frame(0, {moved_pip});
        set_pixel_region(revealed, {8, 8, 34, 22}, 220);
        moved_mask_transition =
            moved_mask_transition ||
            moved_mask
                .observe(0.25 + index * 0.25, revealed, moved_pip,
                         "context")
                .content_transition;
    }
    CHECK(!moved_mask_transition);

    schiva::detail::VisualActivityTracker insufficient;
    insufficient.observe(
        0.0, frame_with_background(10, {kVisualBounds}), kPip, "context");
    bool insufficient_transition = false;
    for (int index = 0; index < 4; ++index) {
        insufficient_transition =
            insufficient_transition ||
            insufficient
                .observe(
                    0.25 + index * 0.25,
                    frame_with_background(220, {kVisualBounds}), kPip,
                    "context")
                .content_transition;
    }
    CHECK(!insufficient_transition);

    schiva::detail::VisualActivityTracker masked_gap;
    masked_gap.observe(0.0, tiled_frame(0), kPip, "context");
    masked_gap.observe(
        0.25, frame_with_background(0, {kVisualBounds}), kPip, "context");
    masked_gap.observe(
        1.00, frame_with_background(0, {kVisualBounds}), kPip, "context");
    masked_gap.observe(
        2.00, frame_with_background(0, {kVisualBounds}), kPip, "context");
    CHECK(!masked_gap.observe(2.25, tiled_frame(1), kPip, "context")
               .content_transition);
    CHECK(!masked_gap.observe(2.50, tiled_frame(1), kPip, "context")
               .content_transition);
    CHECK(!masked_gap.observe(2.75, tiled_frame(1), kPip, "context")
               .content_transition);

    schiva::detail::VisualActivityTracker gap;
    gap.observe(0.0, tiled_frame(0), kPip, "context");
    CHECK(!gap.observe(3.0, tiled_frame(0), kPip, "context")
               .content_transition);
    CHECK(!gap.observe(3.25, tiled_frame(0), kPip, "context")
               .content_transition);
    CHECK(!gap.observe(3.50, tiled_frame(0), kPip, "context")
               .content_transition);
    CHECK(!gap.observe(3.75, tiled_frame(1), kPip, "context")
               .content_transition);
    CHECK(!gap.observe(4.00, tiled_frame(1), kPip, "context")
               .content_transition);
    CHECK(!gap.observe(4.25, tiled_frame(1), kPip, "context")
               .content_transition);
    CHECK(gap.observe(4.50, tiled_frame(1), kPip, "context")
              .content_transition);
    CHECK(!gap.observe(5.00, tiled_frame(1), kPip, "context")
               .content_transition);

    schiva::detail::VisualActivityTracker geometry;
    geometry.observe(0.0, tiled_frame(0), kPip, "context");
    auto changed_geometry = tiled_frame(1);
    changed_geometry.bounds = {10, 10, 810, 460};
    CHECK(!geometry.observe(0.25, changed_geometry, kPip, "context")
               .content_transition);
    CHECK(!geometry.observe(0.50, changed_geometry, kPip, "context")
               .content_transition);
    CHECK(!geometry.observe(0.75, changed_geometry, kPip, "context")
               .content_transition);

    schiva::detail::VisualActivityTracker context_change;
    context_change.observe(0.0, tiled_frame(0), kPip, "app-a");
    CHECK(!context_change
               .observe(0.25, tiled_frame(1), kPip, "app-b")
               .content_transition);
    CHECK(!context_change
               .observe(0.50, tiled_frame(1), kPip, "app-b")
               .content_transition);
    CHECK(!context_change
               .observe(0.75, tiled_frame(1), kPip, "app-b")
               .content_transition);
}

void test_content_overlay_effective_activity() {
    current_test = "content_overlay_effective_activity";
    const schiva::Rect reference{0, 0, 1000, 700};
    const schiva::Rect target{0, 0, 100, 100};
    schiva::detail::ActivityState baseline;
    baseline.short_map.add(50, 50, reference, 4.0);
    baseline.long_map.add(50, 50, reference, 2.0);
    baseline.manual_map.add(50, 50, reference, 1.25);

    schiva::detail::InteractionTracker tracker;
    tracker.set_state(baseline);
    schiva::detail::InteractionTracker durable_only;
    durable_only.set_state(baseline);
    const double baseline_risk = tracker.risk_for(target, reference);
    const double baseline_manual =
        tracker.manual_preference_for(target, reference);
    CHECK(baseline_risk > 0.0);
    CHECK(baseline_manual > 0.0);

    tracker.begin_content_overlay(10.0);
    CHECK(tracker.content_overlay_active());
    const double overlay_risk = tracker.risk_for(target, reference);
    CHECK(overlay_risk < baseline_risk);
    CHECK_NEAR(tracker.manual_preference_for(target, reference),
               baseline_manual, 1e-12);
    CHECK(tracker.effective_state().manual_map.as_sparse() ==
          tracker.state().manual_map.as_sparse());
    CHECK(tracker.state().short_map.as_sparse() ==
          baseline.short_map.as_sparse());

    schiva::detail::LinUCBModel model;
    const schiva::detail::VisualActivityTracker visual;
    const schiva::detail::PlacementScorer scorer(model, tracker, visual);
    auto active_observation = observation(kPip, {50, 50});
    active_observation.foreground->rect = reference;
    const auto before =
        scorer.score(10.0, active_observation, target, kPip);
    CHECK_NEAR(before.activity_risk, overlay_risk, 1e-12);
    CHECK_NEAR(before.manual_preference, baseline_manual, 1e-12);

    tracker.observe(10.0, active_observation);
    tracker.observe(10.25, active_observation);
    durable_only.observe(10.0, active_observation);
    durable_only.observe(10.25, active_observation);
    const double evolved_baseline_risk =
        durable_only.risk_for(target, reference);
    const double evolved_manual =
        durable_only.manual_preference_for(target, reference);
    CHECK(tracker.state().short_map.as_sparse() ==
          durable_only.state().short_map.as_sparse());
    CHECK(tracker.state().long_map.as_sparse() ==
          durable_only.state().long_map.as_sparse());
    const double built_risk = tracker.risk_for(target, reference);
    CHECK(built_risk > overlay_risk);
    const auto after =
        scorer.score(10.25, active_observation, target, kPip);
    CHECK_NEAR(after.activity_risk, built_risk, 1e-12);
    CHECK_NEAR(after.manual_preference, evolved_manual, 1e-12);

    tracker.begin_content_overlay(10.50);
    CHECK(tracker.content_overlay_active());
    CHECK(tracker.risk_for(target, reference) < built_risk);
    CHECK(tracker.effective_state().manual_map.as_sparse() ==
          tracker.state().manual_map.as_sparse());

    tracker.clear_content_overlay();
    CHECK(!tracker.content_overlay_active());
    CHECK_NEAR(tracker.risk_for(target, reference),
               evolved_baseline_risk, 1e-12);
}

void test_content_overlay_interaction_evidence() {
    current_test = "content_overlay_interaction_evidence";
    const schiva::Rect reference{0, 0, 1000, 700};
    const schiva::Rect target{0, 0, 100, 100};
    schiva::detail::ActivityState baseline;
    baseline.short_map.add(50, 50, reference, 4.0);
    baseline.long_map.add(50, 50, reference, 2.0);
    baseline.manual_map.add(50, 50, reference, 1.25);

    schiva::detail::InteractionTracker tracker;
    tracker.set_state(baseline);
    auto idle = observation(kPip, {50, 50});
    idle.foreground->rect = reference;
    idle.input_active = false;
    idle.input_age = -1.0;
    tracker.observe(0.0, idle);

    tracker.begin_content_overlay(10.0);
    auto recent_at_transition = idle;
    recent_at_transition.input_active = true;
    recent_at_transition.input_age = 0.1;
    tracker.observe(10.0, recent_at_transition);
    CHECK(tracker.content_overlay_state().has_value());
    CHECK(tracker.content_overlay_state()->short_map.as_sparse().empty());
    CHECK(tracker.content_overlay_state()->long_map.as_sparse().empty());
    CHECK_NEAR(tracker.content_overlay_exposure(), 0.0, 1e-12);
    const double idle_risk = tracker.risk_for(target, reference);
    const auto idle_effective = tracker.effective_state();
    const auto& idle_durable_short = tracker.state().short_map.values();
    for (std::size_t index = 0;
         index < idle_durable_short.size(); ++index) {
        if (idle_durable_short[index] > 0.0) {
            CHECK_NEAR(idle_effective.short_map.values()[index],
                       0.25 * idle_durable_short[index], 1e-12);
            break;
        }
    }

    tracker.observe(15.0, idle);
    CHECK(tracker.content_overlay_state()->short_map.as_sparse().empty());
    CHECK(tracker.content_overlay_state()->long_map.as_sparse().empty());
    CHECK_NEAR(tracker.content_overlay_exposure(), 0.0, 1e-12);
    const auto evolved_effective = tracker.effective_state();
    const auto& effective_short = evolved_effective.short_map.values();
    const auto& durable_short = tracker.state().short_map.values();
    for (std::size_t index = 0; index < durable_short.size(); ++index) {
        if (durable_short[index] > 0.0) {
            CHECK_NEAR(effective_short[index],
                       0.25 * durable_short[index], 1e-12);
        }
    }

    auto active = idle;
    active.input_active = true;
    active.input_age = 0.1;
    tracker.observe(15.25, active);
    CHECK(tracker.content_overlay_exposure() > 0.0);
    CHECK(!tracker.content_overlay_state()->short_map.as_sparse().empty());
    CHECK(tracker.risk_for(target, reference) > idle_risk);

    tracker.begin_content_overlay(20.0);
    auto invalid_input = idle;
    invalid_input.input_active = true;
    invalid_input.input_age = -1.0;
    tracker.observe(20.25, invalid_input);
    CHECK(tracker.content_overlay_state()->short_map.as_sparse().empty());
    CHECK_NEAR(tracker.content_overlay_exposure(), 0.0, 1e-12);

    tracker.begin_content_overlay(25.0);
    auto active_elsewhere = observation(kPip, {700, 500});
    active_elsewhere.foreground->rect = reference;
    active_elsewhere.input_age = 0.1;
    for (int index = 0; index < 160; ++index) {
        tracker.observe(25.25 + index * 0.25, active_elsewhere);
    }
    CHECK(tracker.content_overlay_exposure() > 39.0);
    const auto faded_effective = tracker.effective_state();
    const auto& faded_long = faded_effective.long_map.values();
    const auto& durable_long = tracker.state().long_map.values();
    constexpr std::size_t target_cell = 25;
    CHECK(durable_long[target_cell] > 0.0);
    const double baseline_ratio =
        faded_long[target_cell] / durable_long[target_cell];
    CHECK(baseline_ratio >= 0.05);
    CHECK(baseline_ratio < 0.06);
    CHECK(faded_effective.manual_map.as_sparse() ==
          tracker.state().manual_map.as_sparse());
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
        const schiva::Point returning = near_outside_point(rect);
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

void test_content_adaptation_persistence() {
    current_test = "content_adaptation_persistence";
    TempDirectory temp;
    {
        schiva::LearningStore store(temp.file("no-visual.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Point far{900, 700};
        auto idle = observation(kPip, far);
        idle.input_active = false;
        idle.input_age = -1.0;
        controller.tick(0.0, idle);
        controller.tick(1.0, idle);
        controller.tick(2.0, idle);
        CHECK(!controller.status().value("content_overlay_active", false));
        CHECK(controller.status().value("content_transition_count", 0) == 0);
        CHECK(controller.status()["learning_count"] == 0);
        controller.close();
        store.close();
    }
    {
        schiva::LearningStore store(temp.file("observe-only.sqlite3"));
        schiva::Controller controller(store, true);
        const schiva::Point far{900, 700};
        controller.tick(
            0.0, observation(kPip, far, false, "pip-handle",
                             tiled_frame(0)));
        controller.tick(
            0.25, observation(kPip, far, false, "pip-handle",
                              tiled_frame(1)));
        controller.tick(
            0.50, observation(kPip, far, false, "pip-handle",
                              tiled_frame(1)));
        controller.tick(
            0.75, observation(kPip, far, false, "pip-handle",
                              tiled_frame(1)));
        CHECK(!controller.status().value("content_overlay_active", false));
        CHECK(controller.status().value("content_transition_count", 0) == 0);
        controller.tick(
            1.00, observation(kPip, far, false, "pip-handle",
                              tiled_frame(1)));
        CHECK(controller.status().value("content_overlay_active", false));
        CHECK(controller.status()["content_transition_count"] == 1);
        CHECK(!controller.status()["motion_active"].get<bool>());
        CHECK(controller.status()["learning_count"] == 0);
        CHECK(store.stats()["decision_records"] == 0);
        controller.close();
        store.close();
    }
    const auto database = temp.file("content.sqlite3");
    const std::string profile_key = "editor.exe|editor|maximized";
    schiva::detail::LinUCBModel pretrained(12, 0.35);
    const std::vector<double> pretrained_features{
        1.0, 0.25, -0.25, 0.1, 0.2, 0.0,
        0.0, 0.15, 0.0, 0.4, 0.3, 0.2};
    pretrained.update(pretrained_features, 0.35);
    {
        schiva::LearningStore store(database);
        auto profile = schiva::detail::StoreAccess::load_profile(
            store, profile_key, "editor.exe", "editor", "maximized");
        CHECK(profile.has_value());
        if (!profile.has_value()) {
            return;
        }
        profile->short_map = {{"4", 2.0}, {"9", 0.5}};
        profile->long_map = {{"4", 3.0}, {"12", 1.0}};
        profile->manual_map = {{"9", 0.75}};
        CHECK(schiva::detail::StoreAccess::save_profile(store, *profile));
        schiva::detail::StoreAccess::save_model(store, pretrained.state());

        schiva::Controller controller(store);
        const schiva::Point far{900, 700};
        const auto page_a = tiled_frame(0);
        const auto page_b = tiled_frame(1);
        controller.tick(
            0.0, observation(kPip, far, false, "pip-handle", page_a));
        CHECK(!controller.status().value("content_overlay_active", false));
        CHECK(controller.status().value("content_transition_count", 0) == 0);
        const schiva::Json before = controller.insights();
        CHECK(before["layers"]["activity"][4].get<double>() > 0.0);
        CHECK(before["layers"]["manual"][9].get<double>() > 0.0);

        controller.tick(
            0.25, observation(kPip, far, false, "pip-handle", page_b));
        controller.tick(
            0.50, observation(kPip, far, false, "pip-handle", page_b));
        CHECK(!controller.status().value("content_overlay_active", false));
        CHECK(controller.status().value("content_transition_count", 0) == 0);
        controller.tick(
            0.75, observation(kPip, far, false, "pip-handle", page_b));
        CHECK(!controller.status().value("content_overlay_active", false));
        CHECK(controller.status().value("content_transition_count", 0) == 0);
        controller.tick(
            1.00, observation(kPip, far, false, "pip-handle", page_b));
        CHECK(controller.status().value("content_overlay_active", false));
        CHECK(controller.status()["content_transition_count"] == 1);
        const schiva::Json after_b = controller.insights();
        CHECK(after_b["layers"]["activity"][4].get<double>() <
              before["layers"]["activity"][4].get<double>());
        CHECK(after_b["layers"]["manual"][9].get<double>() > 0.0);
        CHECK(controller.status()["learning_count"] == 1);
        const auto model_after_b =
            schiva::detail::StoreAccess::load_model(store);
        CHECK(model_after_b.has_value());
        CHECK(model_after_b.has_value() &&
              model_after_b->at("updates").get<int>() == 1);
        CHECK(model_after_b.has_value() &&
              model_after_b->at("inverse") ==
                  pretrained.state().at("inverse"));
        CHECK(store.stats()["decision_records"] == 0);

        controller.tick(
            1.75, observation(kPip, far, false, "pip-handle", page_a));
        controller.tick(
            2.00, observation(kPip, far, false, "pip-handle", page_a));
        controller.tick(
            2.25, observation(kPip, far, false, "pip-handle", page_a));
        controller.tick(
            2.50, observation(kPip, far, false, "pip-handle", page_a));
        CHECK(controller.status().value("content_overlay_active", false));
        CHECK(controller.status()["content_transition_count"] == 2);
        CHECK(controller.status()["learning_count"] == 1);

        auto other_app = observation(kPip, far, false, "pip-handle",
                                     page_a);
        other_app.foreground = schiva::WindowInfo{
            "other-foreground", "browser.exe", "Document", kWork};
        controller.tick(2.75, other_app);
        CHECK(!controller.status().value("content_overlay_active", false));
        CHECK(controller.status()["context_key"] ==
              "browser.exe|document|maximized");
        controller.tick(
            3.00, observation(kPip, far, false, "pip-handle", page_a));
        CHECK(!controller.status().value("content_overlay_active", false));
        CHECK(controller.status()["content_transition_count"] == 2);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore reopened(database);
        CHECK(reopened.stats()["schema_version"] == 1);
        const auto profile = schiva::detail::StoreAccess::peek_profile(
            reopened, profile_key);
        CHECK(profile.has_value());
        CHECK(profile.has_value() && profile->short_map.contains("4"));
        CHECK(profile.has_value() && profile->short_map.contains("227"));
        CHECK(profile.has_value() && profile->short_map["4"].get<double>() >
                                           1.0);
        CHECK(profile.has_value() && profile->long_map.contains("4"));
        CHECK(profile.has_value() && profile->manual_map.contains("9"));
        CHECK(profile.has_value() &&
              profile->manual_map["9"].get<double>() > 0.70);
        const auto model =
            schiva::detail::StoreAccess::load_model(reopened);
        CHECK(model.has_value() && model->at("updates").get<int>() == 1);
        CHECK(reopened.stats()["decision_records"] == 0);
        reopened.close();
    }
}

void test_content_transition_censors_pending_outcome() {
    current_test = "content_transition_censors_pending_outcome";
    TempDirectory temp;
    const schiva::Point far{900, 700};
    const schiva::Point near{510, 250};
    const auto masked_page = [](int seed, const schiva::Rect& pip) {
        return tiled_frame(seed, {pip});
    };
    const auto move_away_from_cursor =
        [&](schiva::Controller& controller, schiva::Rect& rect,
            double& now) {
            controller.tick(
                0.0, observation(kPip, near, false, "pip-handle",
                                 masked_page(0, kPip)));
            schiva::Decision decision = controller.tick(
                0.04, observation(kPip, near, false, "pip-handle",
                                  masked_page(0, kPip)));
            CHECK(decision.move_to.has_value());
            now = 0.04;
            while (controller.status()["motion_active"] == true &&
                   decision.move_to.has_value() && now < 2.0) {
                rect = rect.moved_to(decision.move_to->x,
                                     decision.move_to->y);
                now += 1.0 / 120.0;
                decision = controller.tick(
                    now, observation(rect, far, false, "pip-handle",
                                     masked_page(0, rect)));
            }
            CHECK(!controller.status()["motion_active"].get<bool>());
        };

    {
        schiva::LearningStore store(temp.file("pending.sqlite3"));
        schiva::Controller controller(store);
        schiva::Rect rect = kPip;
        double now = 0.0;
        move_away_from_cursor(controller, rect, now);
        CHECK(controller.status()["learning_count"] == 0);

        now += 0.25;
        controller.tick(
            now, observation(rect, far, false, "pip-handle",
                             masked_page(0, rect)));
        now += 0.25;
        controller.tick(
            now, observation(rect, far, false, "pip-handle",
                             masked_page(0, rect)));
        CHECK(controller.status().value("content_transition_count", 0) == 0);

        for (int index = 0; index < 3; ++index) {
            now += 0.25;
            controller.tick(
                now, observation(rect, far, false, "pip-handle",
                                 masked_page(1, rect)));
            CHECK(controller.status().value(
                      "content_transition_count", 0) == 0);
        }
        now += 0.25;
        const schiva::Decision transitioned = controller.tick(
            now, observation(rect, far, false, "pip-handle",
                             masked_page(1, rect)));
        CHECK(controller.status()["content_transition_count"] == 1);
        CHECK(controller.status().value("content_overlay_active", false));
        CHECK(controller.status()["learning_count"] == 0);
        CHECK(transitioned.state != "movement_failed");
        CHECK(transitioned.state != "manual_correction");

        controller.tick(
            now + 15.0,
            observation(rect, far, false, "pip-handle",
                        masked_page(1, rect)));
        CHECK(controller.status()["learning_count"] == 0);
        CHECK(store.stats()["decision_records"] == 0);
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("control.sqlite3"));
        schiva::Controller controller(store);
        schiva::Rect rect = kPip;
        double now = 0.0;
        move_away_from_cursor(controller, rect, now);
        for (int index = 0; index < 48; ++index) {
            now += 0.25;
            controller.tick(
                now, observation(rect, far, false, "pip-handle",
                                 masked_page(0, rect)));
        }
        CHECK(controller.status()["learning_count"] == 1);
        controller.close();
        store.close();
    }
}

void test_cursor_escape_clearance_regressions() {
    current_test = "cursor_escape_clearance_regressions";
    TempDirectory temp;
    const schiva::Rect pip{600, 450, 920, 630};
    const schiva::Point far{1750, 1000};
    const std::string profile_key = "editor.exe|editor|maximized";
    struct Case {
        const char* name;
        schiva::Point cursor;
        double x_bias;
        double y_bias;
        schiva::Point preferred;
    };
    const std::vector<Case> cases = {
        {"right", {1010, 540}, 8.0, 0.0, {1800, 540}},
        {"left", {510, 540}, -8.0, 0.0, {100, 540}},
        {"top", {760, 360}, 0.0, -8.0, {760, 80}},
        {"bottom", {760, 720}, 0.0, 8.0, {760, 1010}},
        {"bottom-right", {1010, 720}, 8.0, 8.0, {1810, 1010}},
    };

    for (std::size_t index = 0; index < cases.size(); ++index) {
        const Case& item = cases[index];
        schiva::LearningStore store(
            temp.file(std::string("cursor-") + item.name + ".sqlite3"));
        const auto loaded = schiva::detail::StoreAccess::load_profile(
            store, profile_key, "editor.exe", "editor", "maximized");
        CHECK(loaded.has_value());
        if (!loaded.has_value()) {
            store.close();
            continue;
        }
        auto profile = *loaded;
        profile.manual_map =
            schiva::Json::object();
        profile.manual_map[manual_cell_for(item.preferred, kWork)] = 1.0;
        CHECK(schiva::detail::StoreAccess::save_profile(store, profile));

        std::vector<std::vector<double>> identity(
            schiva::detail::kFeatureDimension,
            std::vector<double>(
                schiva::detail::kFeatureDimension, 0.0));
        for (int feature = 0;
             feature < schiva::detail::kFeatureDimension; ++feature) {
            identity[static_cast<std::size_t>(feature)]
                    [static_cast<std::size_t>(feature)] = 1.0;
        }
        std::vector<double> bias(
            schiva::detail::kFeatureDimension, 0.0);
        bias[1] = item.x_bias;
        bias[2] = item.y_bias;
        const schiva::detail::LinUCBModel pretrained(
            schiva::detail::kFeatureDimension, 0.35, identity, bias, 40);
        schiva::detail::StoreAccess::save_model(
            store, pretrained.state());

        schiva::Controller controller(store);
        controller.tick(
            0.0, observation(pip, far, false, "pip-handle",
                             frame_with_background(60, {pip})));
        controller.tick(
            0.04, observation(pip, item.cursor, false, "pip-handle"));
        schiva::Decision decision = controller.tick(
            0.08, observation(pip, item.cursor, false, "pip-handle"));
        CHECK(decision.reason == "cursor_near");
        CHECK(decision.move_to.has_value());
        CHECK(decision.recommendation.has_value());
        if (!decision.move_to.has_value()) {
            controller.close();
            store.close();
            continue;
        }

        schiva::Rect applied = pip;
        double previous_clearance =
            schiva::detail::distance_to_rect(applied, item.cursor);
        double now = 0.08;
        while (decision.move_to.has_value() && now < 2.0) {
            applied = applied.moved_to(
                decision.move_to->x, decision.move_to->y);
            const double clearance =
                schiva::detail::distance_to_rect(applied, item.cursor);
            CHECK(clearance + 1.0 >= previous_clearance);
            previous_clearance = clearance;
            now += 1.0 / 120.0;
            decision = controller.tick(
                now, observation(applied, item.cursor, false,
                                 "pip-handle"));
        }
        CHECK(now < 2.0);
        CHECK(!controller.status()["motion_active"].get<bool>());
        CHECK(schiva::detail::vacated_fraction(pip, applied) >=
              schiva::detail::kClearanceGoal);
        CHECK(decision.state != "no_safe_target");
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("cramped.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Rect cramped{0, 0, 1600, 900};
        const schiva::Point approach{1700, 450};
        controller.tick(
            0.0, observation(cramped, far, false, "pip-handle",
                             frame_with_background(60, {cramped})));
        controller.tick(
            0.04, observation(cramped, approach, false, "pip-handle"));
        schiva::Decision decision = controller.tick(
            0.08, observation(cramped, approach, false, "pip-handle"));
        CHECK(decision.reason == "cursor_near:cramped_fallback");
        CHECK(decision.move_to.has_value());
        CHECK(decision.recommendation.has_value());
        if (decision.recommendation.has_value()) {
            CHECK(schiva::detail::distance_to_rect(
                      *decision.recommendation, approach) <=
                  schiva::detail::kNearMargin);
        }

        schiva::Rect applied = cramped;
        double now = 0.08;
        while (decision.move_to.has_value() && now < 2.0) {
            applied = applied.moved_to(
                decision.move_to->x, decision.move_to->y);
            now += 1.0 / 120.0;
            decision = controller.tick(
                now, observation(applied, approach, false,
                                 "pip-handle"));
        }
        CHECK(now < 2.0);
        CHECK(!controller.status()["motion_active"].get<bool>());
        CHECK(schiva::detail::vacated_fraction(cramped, applied) >=
              schiva::detail::kMinFallbackClearance);
        CHECK(decision.state != "no_safe_target");
        controller.close();
        store.close();
    }

    {
        schiva::LearningStore store(temp.file("cursor-away.sqlite3"));
        schiva::Controller controller(store);
        const schiva::Point approach{510, 250};
        controller.tick(
            0.0, observation(kPip, approach, false, "pip-handle"));
        schiva::Decision decision = controller.tick(
            0.04, observation(kPip, approach, false, "pip-handle"));
        CHECK(decision.reason == "cursor_near");
        CHECK(decision.recommendation.has_value());
        CHECK(decision.move_to.has_value());
        if (!decision.move_to.has_value()) {
            controller.close();
            store.close();
            return;
        }

        schiva::Rect applied = kPip.moved_to(
            decision.move_to->x, decision.move_to->y);
        double now = 0.056;
        decision = controller.tick(
            now, observation(applied, far, false, "pip-handle"));
        CHECK(controller.status()["motion_active"] == true);
        CHECK(decision.state != "cursor_conflict");
        while (decision.move_to.has_value() && now < 2.0) {
            applied = applied.moved_to(
                decision.move_to->x, decision.move_to->y);
            now += 1.0 / 120.0;
            decision = controller.tick(
                now, observation(applied, far, false, "pip-handle"));
        }
        CHECK(now < 2.0);
        CHECK(!controller.status()["motion_active"].get<bool>());
        CHECK(schiva::detail::vacated_fraction(kPip, applied) >=
              schiva::detail::kClearanceGoal);
        CHECK(decision.state != "movement_failed");
        CHECK(decision.state != "manual_correction");
        CHECK(controller.status()["learning_count"] == 0);
        controller.close();
        store.close();
    }
}

void test_content_transition_preserves_committed_escape() {
    current_test = "content_transition_preserves_committed_escape";
    TempDirectory temp;
    schiva::LearningStore store(temp.file("preserve.sqlite3"));
    schiva::Controller controller(store);
    const schiva::Point far{1300, 800};
    const schiva::Point approach{510, 250};
    const auto page_a = tiled_frame(0);
    const auto page_b = [&](const schiva::Rect& pip) {
        return tiled_frame(1, {pip});
    };
    controller.tick(
        0.0, observation(kPip, far, false, "pip-handle", page_a));
    controller.tick(
        0.25, observation(kPip, far, false, "pip-handle",
                          page_b(kPip)));
    controller.tick(
        0.50, observation(kPip, far, false, "pip-handle",
                          page_b(kPip)));
    controller.tick(
        0.75, observation(kPip, far, false, "pip-handle",
                          page_b(kPip)));
    controller.tick(
        0.90, observation(kPip, approach, false, "pip-handle",
                          page_b(kPip)));
    schiva::Decision decision = controller.tick(
        0.95, observation(kPip, approach, false, "pip-handle",
                          page_b(kPip)));
    CHECK(decision.reason == "cursor_near");
    CHECK(decision.move_to.has_value());
    CHECK(decision.recommendation.has_value());
    const auto committed_target = decision.recommendation;

    schiva::Rect applied = kPip;
    double now = 0.95;
    constexpr double frame_interval = 1.0 / 120.0;
    while (now + frame_interval < 1.01 &&
           decision.move_to.has_value()) {
        applied = applied.moved_to(
            decision.move_to->x, decision.move_to->y);
        now += frame_interval;
        decision = controller.tick(
            now, observation(applied, approach, false, "pip-handle"));
    }
    CHECK(now < 1.01);
    CHECK(controller.status().value(
              "content_transition_count", 0) == 0);
    if (decision.move_to.has_value()) {
        applied = applied.moved_to(
            decision.move_to->x, decision.move_to->y);
    }
    now = 1.01;
    decision = controller.tick(
        now, observation(applied, approach, false, "pip-handle",
                         page_b(applied)));
    CHECK(controller.status()["content_transition_count"] == 1);
    CHECK(controller.status()["motion_active"] == true);
    CHECK(decision.recommendation == committed_target);
    CHECK(decision.move_to.has_value());

    while (decision.move_to.has_value() && now < 2.0) {
        applied = applied.moved_to(
            decision.move_to->x, decision.move_to->y);
        now += 1.0 / 120.0;
        decision = controller.tick(
            now, observation(applied, approach, false, "pip-handle"));
    }
    CHECK(now < 2.0);
    CHECK(!controller.status()["motion_active"].get<bool>());
    CHECK(schiva::detail::vacated_fraction(kPip, applied) >=
          schiva::detail::kClearanceGoal);
    CHECK(controller.status()["learning_count"] == 0);

    now += 0.13;
    const schiva::Point returning = near_outside_point(applied);
    schiva::Decision returning_decision = controller.tick(
        now, observation(applied, returning, false, "pip-handle",
                         page_b(applied)));
    CHECK(controller.status()["learning_count"] == 0);
    CHECK(store.stats()["decision_records"] == 0);
    now += 0.13;
    if (returning_decision.move_to.has_value()) {
        applied = applied.moved_to(
            returning_decision.move_to->x,
            returning_decision.move_to->y);
    }
    returning_decision = controller.tick(
        now, observation(applied, returning, false, "pip-handle",
                         page_b(applied)));
    CHECK(controller.status()["learning_count"] == 0);
    CHECK(store.stats()["decision_records"] == 0);
    controller.close();
    store.close();
}

void test_localized_visual_survives_content_adaptation() {
    current_test = "localized_visual_survives_content_adaptation";
    TempDirectory temp;
    const schiva::Point far{700, 400};
    const auto localized = [](int seed, const schiva::Rect& pip,
                              std::uint8_t level) {
        auto frame = tiled_frame(seed, {pip});
        set_pixel_region(frame, {36, 24, 44, 28}, level);
        return frame;
    };
    const auto run = [&](bool adapt, const std::string& name) {
        schiva::LearningStore store(temp.file(name + ".sqlite3"));
        schiva::Controller controller(store);
        if (adapt) {
            controller.tick(
                0.0, observation(kPip, far, false, "pip-handle",
                                 tiled_frame(0, {kPip})));
            for (double now : {0.25, 0.50, 0.75, 1.00}) {
                controller.tick(
                    now, observation(kPip, far, false, "pip-handle",
                                     tiled_frame(1, {kPip})));
            }
            CHECK(controller.status()["content_transition_count"] == 1);
            CHECK(controller.status().value(
                      "content_overlay_active", false));
        } else {
            for (double now : {0.0, 0.25, 0.50, 0.75, 1.00}) {
                controller.tick(
                    now, observation(kPip, far, false, "pip-handle",
                                     tiled_frame(1, {kPip})));
            }
            CHECK(!controller.status().contains(
                "content_transition_count"));
            CHECK(!controller.status().value(
                "content_overlay_active", false));
        }

        schiva::Decision decision;
        double now = 1.25;
        for (std::uint8_t level : {std::uint8_t{60},
                                   std::uint8_t{210}}) {
            now += 0.5;
            decision = controller.tick(
                now, observation(kPip, far, false, "pip-handle",
                                 localized(1, kPip, level)));
        }
        CHECK(decision.reason == "sustained_visual");
        CHECK(decision.move_to.has_value());
        schiva::Rect applied = kPip;
        while (decision.move_to.has_value() && now < 3.0) {
            applied = applied.moved_to(
                decision.move_to->x, decision.move_to->y);
            now += 1.0 / 120.0;
            decision = controller.tick(
                now, observation(applied, far, false, "pip-handle"));
        }
        CHECK(now < 3.0);
        CHECK(!controller.status()["motion_active"].get<bool>());
        CHECK(schiva::detail::vacated_fraction(kPip, applied) >=
              schiva::detail::kClearanceGoal);
        controller.close();
        store.close();
    };
    run(false, "control");
    run(true, "adapted");
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

        controller.tick(
            0.35,
            observation(applied, {510, 250}, true));
        const schiva::Decision collision_release = controller.tick(
            0.36, observation(applied));
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
        {"content_transition_detection",
         test_content_transition_detection},
        {"content_overlay_effective_activity",
         test_content_overlay_effective_activity},
        {"content_overlay_interaction_evidence",
         test_content_overlay_interaction_evidence},
        {"broad_controller_suppression",
         test_broad_controller_suppression},
        {"broad_motion_and_reward_safety",
         test_broad_motion_and_reward_safety},
        {"storage_schema_and_fixture", test_storage_schema_and_fixture},
        {"content_adaptation_persistence",
         test_content_adaptation_persistence},
        {"content_transition_censors_pending_outcome",
         test_content_transition_censors_pending_outcome},
        {"cursor_escape_clearance_regressions",
         test_cursor_escape_clearance_regressions},
        {"content_transition_preserves_committed_escape",
         test_content_transition_preserves_committed_escape},
        {"localized_visual_survives_content_adaptation",
         test_localized_visual_survives_content_adaptation},
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
