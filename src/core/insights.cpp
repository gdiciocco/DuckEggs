#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace schiva::detail {
namespace {

constexpr int kDefaultWorkWidth = 1920;
constexpr int kDefaultWorkHeight = 1080;
constexpr int kDefaultPipWidth = 320;
constexpr int kDefaultPipHeight = 180;

double rounded_six(double value) {
    return static_cast<double>(python_round(value * 1e6)) / 1e6;
}

ActivityState activity_state_from_profile(const ProfileRow& profile) {
    return {
        ActivityMap::from_sparse(profile.short_map, 5.0),
        ActivityMap::from_sparse(profile.long_map, 180.0),
        ActivityMap::from_sparse(profile.manual_map, 600.0)};
}

std::pair<std::vector<std::optional<double>>,
          std::vector<std::optional<double>>>
map_layers(const ActivityState& state) {
    constexpr int size = kMapColumns * kMapRows;
    const bool has_activity =
        std::any_of(state.short_map.values().begin(),
                    state.short_map.values().end(),
                    [](double value) { return value != 0.0; }) ||
        std::any_of(state.long_map.values().begin(),
                    state.long_map.values().end(),
                    [](double value) { return value != 0.0; });
    const bool has_manual =
        std::any_of(state.manual_map.values().begin(),
                    state.manual_map.values().end(),
                    [](double value) { return value != 0.0; });
    std::vector<std::optional<double>> activity;
    std::vector<std::optional<double>> manual;
    activity.reserve(size);
    manual.reserve(size);
    for (int index = 0; index < size; ++index) {
        const double short_value = state.short_map.values()[index];
        const double long_value = state.long_map.values()[index];
        const double preference = state.manual_map.values()[index];
        const double activity_value =
            1.0 - std::exp(-(0.45 * short_value + 0.20 * long_value));
        const double manual_value = 1.0 - std::exp(-preference);
        activity.push_back(
            has_activity
                ? std::optional<double>(rounded_six(
                      std::clamp(activity_value, 0.0, 1.0)))
                : std::nullopt);
        manual.push_back(
            has_manual
                ? std::optional<double>(rounded_six(
                      std::clamp(manual_value, 0.0, 1.0)))
                : std::nullopt);
    }
    return {activity, manual};
}

std::vector<std::optional<double>> project_map_layer(
    const std::vector<std::optional<double>>& values, const Rect& source,
    const Rect& work) {
    constexpr int size = kMapColumns * kMapRows;
    if (static_cast<int>(values.size()) != size ||
        source.width() <= 0 || source.height() <= 0 ||
        work.width() <= 0 || work.height() <= 0) {
        return std::vector<std::optional<double>>(size, std::nullopt);
    }
    if (source == work) {
        return values;
    }

    std::vector<std::optional<double>> projected;
    projected.reserve(size);
    for (int row = 0; row < kMapRows; ++row) {
        const double work_top =
            work.top + static_cast<double>(work.height()) * row / kMapRows;
        const double work_bottom =
            work.top +
            static_cast<double>(work.height()) * (row + 1) / kMapRows;
        const double center_y = (work_top + work_bottom) / 2.0;
        for (int column = 0; column < kMapColumns; ++column) {
            const double work_left =
                work.left +
                static_cast<double>(work.width()) * column / kMapColumns;
            const double work_right =
                work.left +
                static_cast<double>(work.width()) * (column + 1) /
                    kMapColumns;
            const double center_x = (work_left + work_right) / 2.0;
            if (!(source.left <= center_x &&
                  center_x < source.right && source.top <= center_y &&
                  center_y < source.bottom)) {
                projected.push_back(std::nullopt);
                continue;
            }
            const double left =
                std::max(work_left, static_cast<double>(source.left));
            const double right =
                std::min(work_right, static_cast<double>(source.right));
            const double top =
                std::max(work_top, static_cast<double>(source.top));
            const double bottom =
                std::min(work_bottom, static_cast<double>(source.bottom));
            if (right <= left || bottom <= top) {
                projected.push_back(std::nullopt);
                continue;
            }

            const int first_column = std::clamp(
                static_cast<int>(std::floor(
                    (left - source.left) * kMapColumns / source.width())),
                0, kMapColumns - 1);
            const int last_column = std::clamp(
                static_cast<int>(std::ceil(
                    (right - source.left) * kMapColumns /
                    source.width())) -
                    1,
                0, kMapColumns - 1);
            const int first_row = std::clamp(
                static_cast<int>(std::floor(
                    (top - source.top) * kMapRows / source.height())),
                0, kMapRows - 1);
            const int last_row = std::clamp(
                static_cast<int>(std::ceil(
                    (bottom - source.top) * kMapRows /
                    source.height())) -
                    1,
                0, kMapRows - 1);
            double weighted = 0.0;
            double weight = 0.0;
            for (int source_row = first_row; source_row <= last_row;
                 ++source_row) {
                const double cell_top =
                    source.top +
                    static_cast<double>(source.height()) * source_row /
                        kMapRows;
                const double cell_bottom =
                    source.top +
                    static_cast<double>(source.height()) * (source_row + 1) /
                        kMapRows;
                const double overlap_height =
                    std::max(0.0, std::min(cell_bottom, bottom) -
                                      std::max(cell_top, top));
                if (overlap_height <= 0) {
                    continue;
                }
                for (int source_column = first_column;
                     source_column <= last_column; ++source_column) {
                    const double cell_left =
                        source.left +
                        static_cast<double>(source.width()) *
                            source_column / kMapColumns;
                    const double cell_right =
                        source.left +
                        static_cast<double>(source.width()) *
                            (source_column + 1) / kMapColumns;
                    const double overlap_width =
                        std::max(0.0, std::min(cell_right, right) -
                                          std::max(cell_left, left));
                    const int index =
                        source_row * kMapColumns + source_column;
                    if (overlap_width <= 0 ||
                        !values[index].has_value()) {
                        continue;
                    }
                    const double overlap =
                        overlap_width * overlap_height;
                    weighted += *values[index] * overlap;
                    weight += overlap;
                }
            }
            projected.push_back(
                weight > 0
                    ? std::optional<double>(rounded_six(weighted / weight))
                    : std::nullopt);
        }
    }
    return projected;
}

Json normalized_rect(const Rect& rect, const Rect& work) {
    Json result = Json::array();
    if (work.width() <= 0 || work.height() <= 0) {
        result = {0.0, 0.0, 0.0, 0.0};
        return result;
    }
    const double values[] = {
        static_cast<double>(rect.left - work.left) / work.width(),
        static_cast<double>(rect.top - work.top) / work.height(),
        static_cast<double>(rect.right - work.left) / work.width(),
        static_cast<double>(rect.bottom - work.top) / work.height()};
    for (double value : values) {
        result.push_back(rounded_six(std::clamp(value, 0.0, 1.0)));
    }
    return result;
}

std::vector<std::optional<double>> normalize_placement(
    const std::vector<std::optional<double>>& values) {
    std::optional<double> low;
    std::optional<double> high;
    for (const auto& value : values) {
        if (!value.has_value()) {
            continue;
        }
        if (!low.has_value() || *value < *low) {
            low = value;
        }
        if (!high.has_value() || *value > *high) {
            high = value;
        }
    }
    if (!low.has_value()) {
        return values;
    }
    if (*high - *low <= 1e-9) {
        std::vector<std::optional<double>> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(value.has_value()
                                 ? std::optional<double>(0.5)
                                 : std::nullopt);
        }
        return result;
    }
    std::vector<std::optional<double>> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(
            value.has_value()
                ? std::optional<double>(rounded_six(
                      (*value - *low) / (*high - *low)))
                : std::nullopt);
    }
    return result;
}

Json optional_layer_json(
    const std::vector<std::optional<double>>& values) {
    Json result = Json::array();
    for (const auto& value : values) {
        if (value.has_value()) {
            result.push_back(*value);
        } else {
            result.push_back(nullptr);
        }
    }
    return result;
}

std::vector<std::optional<double>> placement_layer(
    const LinUCBModel& model, const ActivityState& state,
    const VisualActivityTracker& visual, double now,
    const Observation& observation, int width, int height,
    bool include_visual) {
    constexpr int size = kMapColumns * kMapRows;
    const auto any_nonzero = [](const ActivityMap& map) {
        return std::any_of(map.values().begin(), map.values().end(),
                           [](double value) { return value != 0.0; });
    };
    if (observation.work_area.width() <= 0 ||
        observation.work_area.height() <= 0 || width <= 0 || height <= 0 ||
        (model.updates() <= 0 && !any_nonzero(state.short_map) &&
         !any_nonzero(state.long_map) && !any_nonzero(state.manual_map))) {
        return std::vector<std::optional<double>>(size, std::nullopt);
    }

    const std::vector<Rect> cells = workspace_centered_candidates(
        observation.work_area, width, height);
    const std::vector<Rect> eligible =
        eligible_placement_targets(cells, observation);
    if (eligible.empty()) {
        return std::vector<std::optional<double>>(size, std::nullopt);
    }

    InteractionTracker activity;
    activity.set_state(state);
    const VisualActivityTracker empty_visual;
    const PlacementScorer scorer(model, activity,
                                  include_visual ? visual : empty_visual);
    const std::optional<Rect> current =
        observation.pip.has_value()
            ? std::optional<Rect>(observation.pip->rect)
            : std::nullopt;
    std::vector<std::optional<double>> result(size, std::nullopt);
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto found = std::find(eligible.begin(), eligible.end(),
                                     cells[index]);
        if (found == eligible.end()) {
            continue;
        }
        const PlacementScore score =
            scorer.score(now, observation, cells[index], current,
                         include_visual);
        result[index] =
            std::isfinite(score.total)
                ? std::optional<double>(rounded_six(score.total))
                : std::nullopt;
    }
    return normalize_placement(result);
}

Json profile_summaries(
    const std::vector<ProfileRow>& profiles,
    const std::unordered_map<std::string, int>& decision_counts) {
    Json summaries = Json::array();
    for (const ProfileRow& profile : profiles) {
        const auto count = decision_counts.find(profile.profile_key);
        summaries.push_back(
            {{"profile_key", profile.profile_key},
             {"app_id", profile.app_id},
             {"role", profile.role},
             {"layout_bin", profile.layout_bin},
             {"decision_count",
              count == decision_counts.end() ? 0 : count->second}});
    }
    return summaries;
}

}  // namespace

Json build_insights(
    const LinUCBModel& model, const std::vector<ProfileRow>& profiles,
    const std::unordered_map<std::string, int>& decision_counts,
    const std::optional<ProfileRow>& selected_profile,
    const std::optional<std::string>& selected_key,
    const std::optional<std::string>& active_profile,
    const std::optional<ActivityState>& active_state,
    const std::optional<Observation>& observation,
    const VisualActivityTracker& visual, int learning_count,
    const std::optional<Rect>& recommendation, double now,
    const std::optional<std::string>& selection_note) {
    Rect work{0, 0, kDefaultWorkWidth, kDefaultWorkHeight};
    if (observation.has_value() &&
        observation->work_area.width() > 0 &&
        observation->work_area.height() > 0) {
        work = observation->work_area;
    }
    int pip_width = kDefaultPipWidth;
    int pip_height = kDefaultPipHeight;
    if (observation.has_value() && observation->pip.has_value() &&
        observation->pip->rect.width() > 0 &&
        observation->pip->rect.height() > 0) {
        pip_width = observation->pip->rect.width();
        pip_height = observation->pip->rect.height();
    }

    const Json summaries =
        profile_summaries(profiles, decision_counts);
    constexpr int map_size = kMapColumns * kMapRows;
    std::optional<Json> foreground_rect;
    std::optional<Json> pip_rect;
    std::optional<Json> recommendation_rect;
    if (observation.has_value() && observation->pip.has_value()) {
        pip_rect = normalized_rect(observation->pip->rect, work);
    }
    if (observation.has_value() &&
        observation->foreground.has_value()) {
        foreground_rect =
            normalized_rect(observation->foreground->rect, work);
    }

    const Json pip_size =
        Json::array({rounded_six(static_cast<double>(pip_width) /
                                 work.width()),
                     rounded_six(static_cast<double>(pip_height) /
                                 work.height())});
    if (!selected_profile.has_value() || !selected_key.has_value()) {
        const std::string note =
            selection_note.value_or(
                "No learned app profiles are available; no profile was "
                "created.");
        const auto empty_layer = optional_layer_json(
            std::vector<std::optional<double>>(map_size, std::nullopt));
        Json layers = {{"activity", empty_layer},
                       {"manual", empty_layer},
                       {"placement", empty_layer}};
        return {
            {"profiles", summaries},
            {"selected_profile", nullptr},
            {"active_profile",
             active_profile.has_value() ? Json(*active_profile)
                                        : Json(nullptr)},
            {"app_id", nullptr},
            {"layout_bin", nullptr},
            {"columns", kMapColumns},
            {"rows", kMapRows},
            {"layers", layers},
            {"learning_count", learning_count},
            {"decision_count", 0},
            {"projection", true},
            {"note", note},
            {"foreground_rect", nullptr},
            {"pip_rect",
             pip_rect.has_value() ? Json(*pip_rect) : Json(nullptr)},
            {"recommendation_rect", nullptr},
            {"pip_size", pip_size},
            {"clearance_goal", kClearanceGoal}};
    }

    const bool is_active =
        active_profile.has_value() && *selected_key == *active_profile;
    std::optional<Rect> live_foreground;
    if (observation.has_value() &&
        observation->foreground.has_value() &&
        observation->foreground->rect.width() > 0 &&
        observation->foreground->rect.height() > 0) {
        live_foreground = observation->foreground->rect;
    }
    const Rect projection_reference =
        is_active && live_foreground.has_value() ? *live_foreground : work;
    foreground_rect = normalized_rect(projection_reference, work);

    const ActivityState state =
        is_active && active_state.has_value()
            ? *active_state
            : activity_state_from_profile(*selected_profile);
    const auto [source_activity, source_manual] = map_layers(state);
    const auto activity_layer =
        project_map_layer(source_activity, projection_reference, work);
    const auto manual_layer =
        project_map_layer(source_manual, projection_reference, work);

    Observation score_observation;
    if (is_active && observation.has_value()) {
        score_observation = *observation;
    } else {
        score_observation.cursor =
            Point{work.left + work.width() / 2,
                  work.top + work.height() / 2};
        score_observation.buttons_down = false;
        score_observation.pip =
            observation.has_value() ? observation->pip : std::nullopt;
        score_observation.foreground =
            WindowInfo{"projected-profile",
                       selected_profile->app_id,
                       selected_profile->role, projection_reference};
        score_observation.work_area = work;
    }
    if (is_active && recommendation.has_value()) {
        recommendation_rect = normalized_rect(*recommendation, work);
    }
    const auto placement = placement_layer(
        model, state, visual, now, score_observation, pip_width,
        pip_height, is_active);
    const std::string note =
        is_active
            ? "Active profile. Usage and preference maps are learned data; "
              "live visual risk covers only observable pixels. Placement is "
              "a relative ranking for the entire PiP rectangle."
            : "Inactive profile: geometry was reconstructed by treating the "
              "whole work area as the app rectangle. The shared global model, "
              "work area, and current PiP dimensions are used; live visual "
              "risk is unavailable. Placement is a relative estimate for the "
              "whole rectangle, not a probability.";
    const auto count = decision_counts.find(*selected_key);
    Json layers = {{"activity", optional_layer_json(activity_layer)},
                   {"manual", optional_layer_json(manual_layer)},
                   {"placement", optional_layer_json(placement)}};
    return {
        {"profiles", summaries},
        {"selected_profile", *selected_key},
        {"active_profile",
         active_profile.has_value() ? Json(*active_profile) : Json(nullptr)},
        {"app_id", selected_profile->app_id},
        {"layout_bin", selected_profile->layout_bin},
        {"columns", kMapColumns},
        {"rows", kMapRows},
        {"layers", layers},
        {"learning_count", learning_count},
        {"decision_count",
         count == decision_counts.end() ? 0 : count->second},
        {"projection", !is_active},
        {"note", note},
        {"foreground_rect",
         foreground_rect.has_value() ? Json(*foreground_rect)
                                     : Json(nullptr)},
        {"pip_rect",
         pip_rect.has_value() ? Json(*pip_rect) : Json(nullptr)},
        {"recommendation_rect",
         recommendation_rect.has_value() ? Json(*recommendation_rect)
                                         : Json(nullptr)},
        {"pip_size", pip_size},
        {"clearance_goal", kClearanceGoal}};
}

}  // namespace schiva::detail
