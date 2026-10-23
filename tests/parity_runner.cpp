#include "schiva/core.hpp"
#include "../src/core/internal.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using schiva::Json;

schiva::Rect rect_from_json(const Json& value) {
    if (!value.is_array() || value.size() != 4) {
        throw std::runtime_error("rect must be a four-element array");
    }
    return {value[0].get<int>(), value[1].get<int>(),
            value[2].get<int>(), value[3].get<int>()};
}

Json rect_to_json(const schiva::Rect& value) {
    return Json::array({value.left, value.top, value.right, value.bottom});
}

std::optional<schiva::Rect> optional_rect_from_json(const Json& value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    return rect_from_json(value);
}

Json optional_rect_to_json(const std::optional<schiva::Rect>& value) {
    return value.has_value() ? rect_to_json(*value) : Json(nullptr);
}

schiva::Point point_from_json(const Json& value) {
    if (!value.is_array() || value.size() != 2) {
        throw std::runtime_error("point must be a two-element array");
    }
    return {value[0].get<int>(), value[1].get<int>()};
}

Json point_to_json(const schiva::Point& value) {
    return Json::array({value.x, value.y});
}

std::optional<schiva::WindowInfo> window_from_json(const Json& value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    return schiva::WindowInfo{
        value.at("window_id").get<std::string>(),
        value.at("app_id").get<std::string>(),
        value.at("role").get<std::string>(),
        rect_from_json(value.at("rect"))};
}

schiva::VisualFrame frame_from_json(const Json& value) {
    schiva::VisualFrame frame;
    frame.width = value.at("width").get<int>();
    frame.height = value.at("height").get<int>();
    frame.bounds = rect_from_json(value.at("bounds"));
    if (frame.width <= 0 || frame.height <= 0) {
        throw std::runtime_error("invalid fixture frame dimensions");
    }
    if (value.contains("pixels")) {
        frame.pixels =
            value.at("pixels").get<std::vector<std::uint8_t>>();
        if (frame.pixels.size() !=
            static_cast<std::size_t>(frame.width) * frame.height) {
            throw std::runtime_error(
                "fixture pixel count does not match frame dimensions");
        }
    } else {
        frame.pixels.assign(
            static_cast<std::size_t>(frame.width) * frame.height,
            static_cast<std::uint8_t>(value.value("base", 0)));
    }
    for (const Json& patch : value.value("patches", Json::array())) {
        const schiva::Rect area = rect_from_json(patch.at("rect"));
        const auto level =
            static_cast<std::uint8_t>(patch.at("level").get<int>());
        for (int y = area.top; y < area.bottom; ++y) {
            for (int x = area.left; x < area.right; ++x) {
                if (x >= 0 && x < frame.width && y >= 0 &&
                    y < frame.height) {
                    frame.pixels[static_cast<std::size_t>(y) *
                                     frame.width +
                                 x] = level;
                }
            }
        }
    }
    for (const Json& excluded : value.value("excluded", Json::array())) {
        frame.excluded.push_back(rect_from_json(excluded));
    }
    return frame;
}

schiva::Observation observation_from_json(const Json& value) {
    schiva::Observation observation;
    observation.cursor = point_from_json(value.at("cursor"));
    observation.buttons_down = value.value("buttons_down", false);
    observation.pip = window_from_json(value.value("pip", Json(nullptr)));
    observation.foreground =
        window_from_json(value.value("foreground", Json(nullptr)));
    observation.work_area = rect_from_json(value.at("work_area"));
    for (const Json& window :
         value.value("windows", Json::array())) {
        const auto decoded = window_from_json(window);
        if (decoded.has_value()) {
            observation.windows.push_back(*decoded);
        }
    }
    if (value.contains("visual_frame") &&
        !value.at("visual_frame").is_null()) {
        observation.visual_frame =
            frame_from_json(value.at("visual_frame"));
    }
    observation.caret =
        value.contains("caret")
            ? optional_rect_from_json(value.at("caret"))
            : std::nullopt;
    observation.input_active = value.value("input_active", false);
    observation.input_age = value.value("input_age", 0.0);
    for (const Json& exclusion :
         value.value("interaction_exclusions", Json::array())) {
        observation.interaction_exclusions.push_back(
            rect_from_json(exclusion));
    }
    return observation;
}

Json decision_to_json(const schiva::Decision& value) {
    return {
        {"state", value.state},
        {"move_to", value.move_to.has_value()
                        ? point_to_json(*value.move_to)
                        : Json(nullptr)},
        {"pip_id", value.pip_id.has_value() ? Json(*value.pip_id)
                                            : Json(nullptr)},
        {"reason", value.reason},
        {"recommendation",
         optional_rect_to_json(value.recommendation)},
        {"context_key",
         value.context_key.has_value() ? Json(*value.context_key)
                                       : Json(nullptr)},
        {"learning_count", value.learning_count}};
}

Json run_linucb(const Json& cases) {
    Json output = Json::array();
    for (const Json& test : cases) {
        schiva::detail::LinUCBModel model(
            test.at("dimension").get<int>(),
            test.value("alpha", 0.35));
        for (const Json& update : test.at("updates")) {
            model.update(
                update.at("features").get<std::vector<double>>(),
                update.at("reward").get<double>());
        }
        Json queries = Json::array();
        for (const Json& query : test.at("queries")) {
            const auto features = query.get<std::vector<double>>();
            queries.push_back(
                {{"predict", model.predict(features)},
                 {"uncertainty", model.uncertainty(features)},
                 {"score", model.score(features)}});
        }
        output.push_back(
            {{"name", test.at("name")},
             {"state", model.state()},
             {"queries", queries}});
    }
    return output;
}

Json run_geometry(const Json& cases) {
    Json output = Json::array();
    for (const Json& test : cases) {
        const std::string operation = test.at("operation");
        Json value;
        if (operation == "candidates") {
            Json candidates = Json::array();
            const schiva::Rect work =
                rect_from_json(test.at("work"));
            for (const schiva::Rect& target :
                 schiva::detail::candidate_rects(
                     work, test.at("size")[0].get<int>(),
                     test.at("size")[1].get<int>(),
                     point_from_json(test.at("cursor")),
                     optional_rect_from_json(
                         test.value("current", Json(nullptr))))) {
                candidates.push_back(rect_to_json(target));
            }
            value = candidates;
        } else if (operation == "path") {
            const auto path = schiva::detail::plan_safe_path(
                point_from_json(test.at("start")),
                point_from_json(test.at("target")),
                test.at("size")[0].get<int>(),
                test.at("size")[1].get<int>(),
                point_from_json(test.at("cursor")),
                test.value("safety_margin", 28),
                optional_rect_from_json(
                    test.value("work", Json(nullptr))));
            if (path.has_value()) {
                value = Json::array();
                for (const schiva::Point& point : *path) {
                    value.push_back(point_to_json(point));
                }
            } else {
                value = nullptr;
            }
        } else if (operation == "first_step") {
            const auto point = schiva::detail::first_safe_step(
                point_from_json(test.at("start")),
                point_from_json(test.at("target")),
                test.at("size")[0].get<int>(),
                test.at("size")[1].get<int>(),
                point_from_json(test.at("cursor")),
                test.value("safety_margin", 28),
                optional_rect_from_json(
                    test.value("work", Json(nullptr))),
                test.value("max_step", 96));
            value = point.has_value() ? point_to_json(*point) : Json(nullptr);
        } else if (operation == "trajectory") {
            std::vector<schiva::Point> path;
            for (const Json& point : test.at("path")) {
                path.push_back(point_from_json(point));
            }
            auto trajectory =
                schiva::detail::MotionTrajectory::from_path(path);
            Json positions = Json::array();
            for (const Json& step : test.at("steps")) {
                positions.push_back(
                    point_to_json(trajectory.advance(step.get<double>())));
            }
            value = {{"duration", trajectory.duration()},
                     {"positions", positions},
                     {"target", point_to_json(trajectory.target())},
                     {"complete", trajectory.complete()}};
        } else {
            throw std::runtime_error("unknown geometry operation");
        }
        output.push_back({{"name", test.at("name")}, {"value", value}});
    }
    return output;
}

Json run_activity(const Json& cases) {
    Json output = Json::array();
    for (const Json& test : cases) {
        const Json& map_spec = test.at("map");
        schiva::detail::ActivityMap map(
            map_spec.value("columns", 24), map_spec.value("rows", 14),
            map_spec.value("half_life", 8.0));
        for (const Json& operation : map_spec.at("operations")) {
            if (operation.at("op") == "add") {
                map.add(operation.at("point")[0].get<double>(),
                        operation.at("point")[1].get<double>(),
                        rect_from_json(operation.at("reference")),
                        operation.at("weight").get<double>());
            } else if (operation.at("op") == "decay") {
                map.decay(operation.at("elapsed").get<double>());
            } else {
                throw std::runtime_error("unknown activity operation");
            }
        }
        Json densities = Json::array();
        for (const Json& density : test.at("densities")) {
            densities.push_back(map.density_for_rect(
                rect_from_json(density.at("target")),
                rect_from_json(density.at("reference"))));
        }
        schiva::detail::InteractionTracker tracker;
        Json tracker_risks = Json::array();
        for (const Json& observation_json : test.at("tracker_observations")) {
            tracker.observe(observation_json.at("now").get<double>(),
                            observation_from_json(
                                observation_json.at("observation")));
        }
        for (const Json& risk : test.at("tracker_risks")) {
            tracker_risks.push_back(
                {{"activity", tracker.risk_for(
                                  rect_from_json(risk.at("target")),
                                  rect_from_json(risk.at("reference")))},
                 {"manual", tracker.manual_preference_for(
                                rect_from_json(risk.at("target")),
                                rect_from_json(risk.at("reference")))}});
        }
        output.push_back(
            {{"name", test.at("name")},
             {"sparse", map.as_sparse()},
             {"densities", densities},
             {"tracker_risks", tracker_risks},
             {"tracker_had_activity", tracker.had_activity()}});
    }
    return output;
}

Json run_visual(const Json& cases) {
    Json output = Json::array();
    for (const Json& test : cases) {
        const int columns = test.value("columns", 16);
        const int rows = test.value("rows", 9);
        schiva::detail::VisualActivityTracker tracker(columns, rows);
        Json observations = Json::array();
        for (const Json& step : test.at("observations")) {
            const auto pip =
                optional_rect_from_json(step.value("pip", Json(nullptr)));
            std::optional<std::string> context;
            if (step.contains("context") &&
                !step.at("context").is_null()) {
                context = step.at("context").get<std::string>();
            }
            const auto evidence = tracker.observe(
                step.at("now").get<double>(),
                frame_from_json(step.at("frame")), pip, context);
            Json risks = Json::array();
            for (const Json& target : step.at("risk_targets")) {
                risks.push_back(tracker.risk_for(
                    rect_from_json(target), std::nullopt,
                    step.at("now").get<double>()));
            }
            observations.push_back(
                {{"valid", evidence.valid},
                 {"burst", evidence.burst},
                 {"sustained", evidence.sustained},
                 {"conflict", evidence.conflict},
                 {"change_ratio", evidence.change_ratio},
                 {"available",
                  tracker.availability_at(
                      step.at("now").get<double>())},
                 {"grace",
                  tracker.grace_active_at(step.at("now").get<double>())},
                 {"risks", risks}});
        }
        output.push_back(
            {{"name", test.at("name")}, {"observations", observations}});
    }
    return output;
}

std::filesystem::path temporary_database(const std::string& name) {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("schiva-parity-" + name + "-" + std::to_string(stamp) +
            ".sqlite3");
}

Json run_controllers(const Json& cases) {
    Json output = Json::array();
    int index = 0;
    for (const Json& test : cases) {
        const auto path = temporary_database(std::to_string(index++));
        Json decisions = Json::array();
        try {
            schiva::LearningStore store(path);
            schiva::Controller controller(
                store, test.value("observe_only", false));
            for (const Json& event : test.at("events")) {
                decisions.push_back(decision_to_json(controller.tick(
                    event.at("now").get<double>(),
                    observation_from_json(event.at("observation")))));
            }
            const Json status = controller.status();
            const Json insights = controller.insights();
            controller.close();
            store.close();
            output.push_back(
                {{"name", test.at("name")},
                 {"decisions", decisions},
                 {"status", status},
                 {"insights", insights}});
            std::error_code error;
            std::filesystem::remove(path, error);
        } catch (...) {
            std::error_code error;
            std::filesystem::remove(path, error);
            throw;
        }
    }
    return output;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: parity_runner FIXTURE.json\n";
        return 2;
    }
    try {
        std::ifstream input(argv[1]);
        if (!input) {
            throw std::runtime_error("unable to open parity fixture");
        }
        Json fixture;
        input >> fixture;
        const Json& cases = fixture.at("input");
        Json result = {
            {"linucb", run_linucb(cases.at("linucb"))},
            {"geometry", run_geometry(cases.at("geometry"))},
            {"activity", run_activity(cases.at("activity"))},
            {"visual", run_visual(cases.at("visual"))},
            {"controllers", run_controllers(cases.at("controllers"))}};
        std::cout << result.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "parity runner failed: " << error.what() << '\n';
        return 1;
    }
}
