#include "schiva/heatmap.hpp"
#include "schiva/platform.hpp"
#include "schiva/runtime.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
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

template <typename Exception, typename Function>
void check_throws(Function&& function, int line) {
    try {
        function();
        std::cerr << "FAIL line " << line << ": exception not thrown\n";
        ++failures;
    } catch (const Exception&) {
    } catch (const std::exception& exception) {
        std::cerr << "FAIL line " << line
                  << ": wrong exception: " << exception.what() << '\n';
        ++failures;
    }
}

#define CHECK_THROWS(type, expression) \
    check_throws<type>([&] { expression; }, __LINE__)

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path() /
                ("schiva-cpp-platform-test-" + std::to_string(stamp));
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

void test_title_and_window_identity() {
    CHECK(schiva::is_target_window(
        L"Chrome_WidgetWin_1",
        L"Picture in picture",
        L"C:\\Program Files\\Google\\Chrome\\chrome.exe"));
    CHECK(schiva::is_target_window(
        L"Chrome_WidgetWin_1",
        L"Immagine nell\u2019immagine",
        L"chrome.exe"));
    CHECK(!schiva::is_target_window(
        L"Chrome_WidgetWin_1",
        L"Picture in Picture - Chrome",
        L"chrome.exe"));
    CHECK(!schiva::is_target_window(
        L"Chrome_WidgetWin_1",
        L"Picture in picture",
        L"msedge.exe"));
    CHECK(!schiva::is_target_window(
        L"OtherWindowClass",
        L"Picture in picture",
        L"chrome.exe"));
    CHECK(schiva::normalize_title(L"  PICTURE__in\tpicture  ") ==
          L"picture in picture");

    CHECK(schiva::window_id(0x1234) == "hwnd:1234");
    CHECK(schiva::parse_window_id("hwnd:1234") == 0x1234U);
    CHECK_THROWS(schiva::PlatformError, schiva::parse_window_id("1234"));
    CHECK_THROWS(schiva::PlatformError, schiva::parse_window_id("hwnd:0"));
}

std::filesystem::path repository_root() {
    return std::filesystem::absolute(
               std::filesystem::path(__FILE__))
               .parent_path()
               .parent_path();
}

std::wstring utf8_to_wide(std::string_view input) {
    std::wstring result;
    result.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        const auto first =
            static_cast<unsigned char>(input[index]);
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        if (first <= 0x7F) {
            codepoint = first;
            length = 1;
        } else if (first >= 0xC2 && first <= 0xDF) {
            codepoint = first & 0x1F;
            length = 2;
        } else if (first >= 0xE0 && first <= 0xEF) {
            codepoint = first & 0x0F;
            length = 3;
        } else if (first >= 0xF0 && first <= 0xF4) {
            codepoint = first & 0x07;
            length = 4;
        } else {
            throw std::invalid_argument("invalid UTF-8 lead byte");
        }
        if (input.size() - index < length) {
            throw std::invalid_argument("truncated UTF-8 sequence");
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(
                input[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                throw std::invalid_argument(
                    "invalid UTF-8 continuation byte");
            }
            codepoint =
                (codepoint << 6) | (continuation & 0x3F);
        }
        if ((length == 3 && codepoint < 0x800) ||
            (length == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0x10FFFF) {
            throw std::invalid_argument("invalid UTF-8 code point");
        }
        if constexpr (sizeof(wchar_t) == 2) {
            if (codepoint <= 0xFFFF) {
                result.push_back(static_cast<wchar_t>(codepoint));
            } else {
                codepoint -= 0x10000;
                result.push_back(
                    static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
                result.push_back(
                    static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
            }
        } else {
            result.push_back(static_cast<wchar_t>(codepoint));
        }
        index += length;
    }
    return result;
}

void test_official_chromium_titles() {
    const auto dataset_path =
        repository_root() / "design" / "chromium-pip-titles.json";
    std::ifstream input(dataset_path, std::ios::binary);
    CHECK(input.is_open());
    if (!input.is_open()) {
        return;
    }
    const nlohmann::json dataset = nlohmann::json::parse(input);
    CHECK(dataset.at("symbol") ==
          "IDS_PICTURE_IN_PICTURE_TITLE_TEXT");
    CHECK(dataset.at("message_id") == "7750228210027921155");
    CHECK(dataset.at("source_sha") ==
          "41020d875c4518f25b5b27a76c7d418235a50f31");
    const auto& titles = dataset.at("titles");
    CHECK(titles.size() == 81);
    CHECK(dataset.at("locale_count").at("available") == 81);
    CHECK(dataset.at("locale_count").at("found") == 81);
    CHECK(dataset.at("locale_count").at("missing").empty());

    const auto english =
        utf8_to_wide(dataset.at("source_text").get<std::string>());
    CHECK(english == L"Picture in picture");
    CHECK(schiva::title_matches(english));

    std::size_t checked_locales = 0;
    for (const auto& [locale, value] : titles.items()) {
        (void)locale;
        const std::wstring title =
            utf8_to_wide(value.get<std::string>());
        CHECK(schiva::title_matches(title));
        CHECK(schiva::is_target_window(
            L"Chrome_WidgetWin_1", title, L"chrome.exe"));
        ++checked_locales;
    }
    CHECK(checked_locales == 81);

    const std::wstring legacy_italian =
        L"Immagine nell\u2019immagine";
    CHECK(schiva::title_matches(legacy_italian));
    CHECK(schiva::is_target_window(
        L"Chrome_WidgetWin_1", legacy_italian, L"chrome.exe"));

    const auto czech =
        utf8_to_wide(titles.at("cs").get<std::string>());
    CHECK(czech.find(L'\u00A0') != std::wstring::npos);
    CHECK(schiva::title_matches(czech));
    const auto khmer =
        utf8_to_wide(titles.at("km").get<std::string>());
    CHECK(khmer.back() == L'\u200B');
    CHECK(schiva::title_matches(khmer));

    CHECK(schiva::title_matches(L"PICTURE__IN\tPICTURE"));
    CHECK(schiva::title_matches(L"picture-in-picture"));
    CHECK(schiva::title_matches(L"  Picture in picture  "));

    const std::wstring japanese =
        utf8_to_wide(titles.at("ja").get<std::string>());
    CHECK(!schiva::title_matches(L"Prefix " + japanese));
    CHECK(!schiva::title_matches(japanese + L" suffix"));
    CHECK(!schiva::title_matches(
        L"\u202EPicture in picture\u202C"));
    CHECK(!schiva::title_matches(
        L"Picture in picture\U0001F600"));

    CHECK(!schiva::is_target_window(
        L"Chrome_WidgetWin_1", english, L"msedge.exe"));
    CHECK(!schiva::is_target_window(
        L"Chrome_WidgetWin_1", english, L"brave.exe"));
    CHECK(!schiva::is_target_window(
        L"Chrome_WidgetWin_1", english, L"chrome.exe.fake"));
    CHECK(!schiva::is_target_window(
        L"OtherWindowClass", english, L"chrome.exe"));
    CHECK(!schiva::is_target_window(
        L"Chrome_WidgetWin_1", english, L"chrome.exe", false, true));
    CHECK(!schiva::is_target_window(
        L"Chrome_WidgetWin_1", english, L"chrome.exe", true, false));
}

void test_geometry_helpers() {
    const schiva::Rect bounds{100, 50, 300, 250};
    const auto clipped = schiva::clipped_intersection(
        schiva::Rect{0, 0, 150, 100}, bounds);
    CHECK(clipped.has_value());
    CHECK(clipped->left == 100);
    CHECK(clipped->top == 50);
    CHECK(clipped->right == 150);
    CHECK(clipped->bottom == 100);
    CHECK(!schiva::clipped_intersection(
               schiva::Rect{0, 0, 90, 40}, bounds)
               .has_value());

    CHECK(!schiva::segment_is_safe(
        {100, 100},
        {700, 100},
        {320, 180},
        {500, 200}));
    CHECK(schiva::segment_is_safe(
        {100, 100},
        {100, 700},
        {320, 180},
        {500, 200}));
    CHECK(schiva::point_near_rect({100, 100}, bounds, 20));
    CHECK(!schiva::point_near_rect({20, 20}, bounds, 20));
}

void test_motion_geometry_guard() {
    const schiva::Rect expected{600, 200, 1000, 425};
    int apply_count = 0;

    // Simulate a manual drag between observation and apply. The stale
    // expected rectangle must prevent the injected apply from running.
    const schiva::Rect manual_observation = expected.moved_to(650, 230);
    if (schiva::motion_rect_matches(expected, manual_observation)) {
        ++apply_count;
    }
    CHECK(apply_count == 0);

    const schiva::Rect refreshed_observation = expected;
    if (schiva::motion_rect_matches(expected, refreshed_observation)) {
        ++apply_count;
    }
    CHECK(apply_count == 1);

    const schiva::Rect applied = expected.moved_to(40, 40);
    CHECK(schiva::motion_rect_matches(applied, applied));
    CHECK(!schiva::motion_rect_matches(expected, applied));
}

void test_scheduler_and_status() {
    CHECK(schiva::kVersion == "0.4.0-alpha.4");
    CHECK(std::abs(schiva::target_period(true, false) - 1.0 / 120.0) <
          1.0e-12);
    CHECK(std::abs(schiva::target_period(false, false) - 1.0 / 60.0) <
          1.0e-12);
    CHECK(std::abs(schiva::target_period(false, true) - 1.0 / 15.0) <
          1.0e-12);
    const double period = 1.0 / 120.0;
    const double advanced =
        schiva::next_deadline(10.0, 10.5001, period);
    CHECK(advanced > 10.5001);
    CHECK(advanced <= 10.5001 + period);
    CHECK(std::abs(
              schiva::next_deadline(10.0, 10.003, period) -
              (10.0 + period)) <
          1.0e-12);

    TemporaryDirectory temporary;
    schiva::StatusFile status(temporary.path());
    status.write(
        {
            {"mode", "adaptive"},
            {"state", "testing"},
            {"paused", true},
            {"learning_count", 0}},
        true);
    const schiva::Json read = status.read();
    CHECK(read["state"] == "testing");
    CHECK(read["paused"] == true);
    CHECK(read.contains("timestamp"));
    CHECK(std::filesystem::exists(status.path()));
    std::size_t temporary_files = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(temporary.path())) {
        if (entry.path().filename().string().find(".tmp-") !=
            std::string::npos) {
            ++temporary_files;
        }
    }
    CHECK(temporary_files == 0);
}

schiva::VisualFrame test_frame(int value) {
    schiva::VisualFrame frame;
    frame.width = 2;
    frame.height = 2;
    frame.pixels = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value)};
    frame.bounds = schiva::Rect{0, 0, 100, 100};
    return frame;
}

void test_capture_mailbox() {
    std::atomic<int> calls{0};
    schiva::VisualCaptureWorker worker(
        [&calls](const schiva::Rect&, const std::vector<schiva::Rect>&) {
            ++calls;
            return test_frame(40);
        });
    CHECK(worker.request(7, {0, 0, 100, 100}, {}));
    const auto result = worker.wait_for_result(7, 1.0);
    CHECK(result.has_value());
    CHECK(result->width == 2);
    CHECK(result->pixels.front() == 40);
    CHECK(!worker.poll(7).has_value());
    CHECK(!worker.poll(8).has_value());
    CHECK(worker.thread_started());
    CHECK(worker.metrics()["capture_duration_ms"]["samples"].get<int>() >=
          1);

    std::mutex mutex;
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    schiva::VisualCaptureWorker stale(
        [&mutex, &started, &release](
            const schiva::Rect&,
            const std::vector<schiva::Rect>&) {
            started.store(true);
            for (;;) {
                {
                    std::lock_guard lock(mutex);
                    if (release.load()) {
                        break;
                    }
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
            return test_frame(90);
        });
    CHECK(stale.request(1, {0, 0, 100, 100}, {}));
    for (int waited = 0; waited < 500 && !started.load(); ++waited) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(started.load());
    stale.invalidate(2);
    {
        std::lock_guard lock(mutex);
        release.store(true);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK(!stale.poll(1).has_value());
    CHECK(!stale.poll(2).has_value());

    schiva::VisualCaptureWorker failing(
        [](const schiva::Rect&, const std::vector<schiva::Rect>&)
            -> schiva::VisualFrame {
            throw schiva::PlatformError("synthetic capture denial");
        });
    CHECK(failing.request(3, {0, 0, 100, 100}, {}));
    (void)failing.wait_for_result(3, 1.0);
    const auto error = failing.capture_error(3);
    CHECK(error.has_value());
    CHECK(error->find("synthetic capture denial") != std::string::npos);
}

void test_capture_waits_and_bounded_close() {
    std::atomic<int> calls{0};
    schiva::VisualCaptureWorker worker(
        [&calls](const schiva::Rect&, const std::vector<schiva::Rect>&) {
            ++calls;
            return test_frame(60);
        });
    CHECK(worker.request(1, {0, 0, 100, 100}, {}));
    CHECK(worker.wait_for_result(1, 1.0).has_value());

    const int validity_before_pause =
        worker.metrics()["validity"].get<int>();
    worker.pause();
    schiva::Json paused_metrics;
    for (int attempt = 0; attempt < 500; ++attempt) {
        paused_metrics = worker.metrics();
        if (paused_metrics["waiting_for_work"].get<bool>() &&
            paused_metrics["validity"].get<int>() ==
                validity_before_pause + 1) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(paused_metrics["waiting_for_work"].get<bool>());
    CHECK(paused_metrics["validity"].get<int>() ==
          validity_before_pause + 1);
    const std::uint64_t paused_wakes =
        paused_metrics["condition_wakes"].get<std::uint64_t>();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto still_paused = worker.metrics();
    CHECK(still_paused["waiting_for_work"].get<bool>());
    CHECK(still_paused["condition_wakes"].get<std::uint64_t>() ==
          paused_wakes);
    CHECK(calls.load() == 1);

    worker.resume();
    worker.set_suppressed(true);
    const int suppressed_validity =
        still_paused["validity"].get<int>() + 1;
    schiva::Json suppressed_metrics;
    for (int attempt = 0; attempt < 500; ++attempt) {
        suppressed_metrics = worker.metrics();
        if (suppressed_metrics["waiting_for_work"].get<bool>() &&
            suppressed_metrics["validity"].get<int>() ==
                suppressed_validity) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const std::uint64_t suppressed_wakes =
        suppressed_metrics["condition_wakes"].get<std::uint64_t>();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto still_suppressed = worker.metrics();
    CHECK(still_suppressed["waiting_for_work"].get<bool>());
    CHECK(still_suppressed["condition_wakes"].get<std::uint64_t>() ==
          suppressed_wakes);
    CHECK(calls.load() == 1);
    worker.close();
    const auto clean_close = worker.metrics();
    CHECK(clean_close["thread_finished"].get<bool>());
    CHECK(!clean_close["close_timed_out"].get<bool>());
    CHECK(!clean_close["thread_detached"].get<bool>());

    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    schiva::VisualCaptureWorker stuck(
        [&started, &release](
            const schiva::Rect&,
            const std::vector<schiva::Rect>&) {
            started.store(true);
            while (!release.load()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
            return test_frame(70);
        },
        0.5,
        0.5,
        0.25,
        0.05);
    CHECK(stuck.request(2, {0, 0, 100, 100}, {}));
    for (int attempt = 0; attempt < 500 && !started.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(started.load());
    const auto close_started = std::chrono::steady_clock::now();
    stuck.close();
    const double close_elapsed =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - close_started)
            .count();
    CHECK(close_elapsed >= 0.025);
    CHECK(close_elapsed < 1.0);
    const auto timed_out = stuck.metrics();
    CHECK(timed_out["close_timed_out"].get<bool>());
    CHECK(timed_out["thread_detached"].get<bool>());
    release.store(true);
    bool finished = false;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (stuck.metrics()["thread_finished"].get<bool>()) {
            finished = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(finished);
}

void test_heatmap_helpers() {
    using namespace schiva::heatmap;
    CHECK(clamp_unit(schiva::Json(-4.0)).value_or(-1.0) == 0.0);
    CHECK(clamp_unit(schiva::Json(4.0)).value_or(-1.0) == 1.0);
    CHECK(!clamp_unit(schiva::Json(true)).has_value());
    CHECK(!clamp_unit(schiva::Json(nullptr)).has_value());
    const auto dimensions = grid_dimensions(
        {{"columns", 999}, {"rows", -2}});
    CHECK(dimensions.first == 128);
    CHECK(dimensions.second == 1);
    CHECK(grid_dimensions(schiva::Json::object()) ==
          std::make_pair(24, 14));

    const schiva::Json snapshot = {
        {"profiles",
         schiva::Json::array(
             {{{"profile_key", "active|window|maximized"},
               {"app_id", "active.exe"},
               {"role", "Window"},
               {"layout_bin", "maximized"},
               {"decision_count", 4}},
              {{"profile_key", "old|dialog|free"},
               {"app_id", "old.exe"},
               {"role", "Dialog"},
               {"layout_bin", "free"},
               {"decision_count", 2}}})},
        {"selected_profile", "active|window|maximized"},
        {"active_profile", "active|window|maximized"},
        {"columns", 24},
        {"rows", 14},
        {"layers",
         {{"activity", schiva::Json::array({-5.0, 0.42, 20.0})},
          {"manual", schiva::Json::array({nullptr})},
          {"placement", schiva::Json::array({nullptr})}}},
        {"learning_count", 7},
        {"decision_count", 99},
        {"pip_size", schiva::Json::array({0.2, 0.12})}};
    CHECK(std::abs(layer_value(snapshot, "activity", 0).value() - 0.0) <
          1.0e-12);
    CHECK(std::abs(layer_value(snapshot, "activity", 1).value() - 0.42) <
          1.0e-12);
    CHECK(std::abs(layer_value(snapshot, "activity", 2).value() - 1.0) <
          1.0e-12);
    CHECK(!layer_value(snapshot, "activity", 3).has_value());
    CHECK(layer_color("activity", schiva::Json(nullptr)) ==
          std::string(kUnknownColor));
    CHECK(profile_choices(snapshot).size() == 2);
    CHECK(learning_summary(snapshot).find("Decisioni profilo: 4") !=
          std::string::npos);
    const auto footprint = candidate_footprint(
        12, 7, 24, 14, schiva::Json::array({0.2, 0.12}));
    CHECK(footprint.has_value());
    CHECK(std::abs(
              (footprint->right - footprint->left) - 0.2) < 1.0e-12);
    CHECK(std::abs(
              (footprint->bottom - footprint->top) - 0.12) <
          1.0e-12);
    CHECK(cold_start_message(
              {{"profiles", schiva::Json::array()},
               {"layers", schiva::Json::object()},
               {"learning_count", 0}})
              .has_value());
}

void execute_sql(sqlite3* database, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        const std::string detail =
            message != nullptr ? message : "unknown SQLite error";
        sqlite3_free(message);
        throw std::runtime_error(detail);
    }
}

std::string file_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

void test_python_import() {
    TemporaryDirectory temporary;
    const auto source = temporary.path() / "python-backup.sqlite3";
    const auto data_dir = temporary.path() / "cpp-data";
    sqlite3* database = nullptr;
    CHECK(sqlite3_open(source.string().c_str(), &database) == SQLITE_OK);
    execute_sql(
        database,
        "CREATE TABLE profiles ("
        "profile_key TEXT PRIMARY KEY, app_id TEXT NOT NULL, "
        "role TEXT NOT NULL, layout_bin TEXT NOT NULL, "
        "short_map TEXT NOT NULL, long_map TEXT NOT NULL, "
        "manual_map TEXT NOT NULL);"
        "CREATE TABLE decision_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, profile_key TEXT NOT NULL, "
        "features_json TEXT NOT NULL, action TEXT NOT NULL, "
        "reward REAL NOT NULL);"
        "CREATE TABLE model_state ("
        "id INTEGER PRIMARY KEY CHECK (id = 1), "
        "state_json TEXT NOT NULL, updates INTEGER NOT NULL);"
        "INSERT INTO profiles VALUES "
        "('active|window|maximized','active.exe','Window','maximized',"
        "'{}','{}','{}');"
        "INSERT INTO decision_records "
        "(profile_key,features_json,action,reward) VALUES "
        "('active|window|maximized','[0.1]','a',0.1);"
        "INSERT INTO model_state VALUES (1,'{\"updates\":1}',1);"
        "PRAGMA user_version=1;");
    sqlite3_close(database);
    const std::string before = file_bytes(source);

    const schiva::ImportReport report =
        schiva::import_python_backup(source, data_dir);
    CHECK(report.schema_version == 1);
    CHECK(report.profiles == 1);
    CHECK(report.decision_records == 1);
    CHECK(report.model_rows == 1);
    CHECK(std::filesystem::exists(report.destination));
    CHECK(file_bytes(source) == before);

    sqlite3* imported = nullptr;
    CHECK(sqlite3_open(report.destination.string().c_str(), &imported) ==
          SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    CHECK(sqlite3_prepare_v2(
              imported,
              "SELECT COUNT(*) FROM profiles",
              -1,
              &statement,
              nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(sqlite3_column_int(statement, 0) == 1);
    sqlite3_finalize(statement);
    sqlite3_close(imported);

    CHECK_THROWS(
        schiva::PlatformError,
        schiva::import_python_backup(source, data_dir));
}

}  // namespace

int main() {
    test_title_and_window_identity();
    test_official_chromium_titles();
    test_geometry_helpers();
    test_motion_geometry_guard();
    test_scheduler_and_status();
    test_capture_mailbox();
    test_capture_waits_and_bounded_close();
    test_heatmap_helpers();
    test_python_import();
    if (failures != 0) {
        std::cerr << failures << " platform checks failed\n";
        return 1;
    }
    std::cout << "All portable platform checks passed\n";
    return 0;
}
