#include "internal.hpp"

#include <algorithm>
#include <cmath>

namespace schiva::detail {
namespace {

std::vector<Rect> geometry_eligible_targets(
    const std::vector<Rect>& candidates,
    const Observation& observation, bool protect_pointer) {
    if (!observation.pip.has_value() ||
        observation.work_area.width() <= 0 ||
        observation.work_area.height() <= 0) {
        return {};
    }
    const int width = observation.pip->rect.width();
    const int height = observation.pip->rect.height();
    if (width <= 0 || height <= 0) {
        return {};
    }
    const std::optional<Rect> protected_area =
        protect_pointer
            ? std::optional<Rect>(
                  mouse_work_region(observation.cursor, width, height))
            : std::nullopt;
    const Point start{observation.pip->rect.left,
                      observation.pip->rect.top};
    std::vector<Rect> eligible;
    for (const Rect& target : candidates) {
        if (target.width() != width || target.height() != height ||
            target.left < observation.work_area.left ||
            target.top < observation.work_area.top ||
            target.right > observation.work_area.right ||
            target.bottom > observation.work_area.bottom ||
            (protected_area.has_value() &&
             intersects(target, *protected_area))) {
            continue;
        }
        if (!first_safe_step(start, {target.left, target.top}, width,
                             height, observation.cursor, kSafetyMargin,
                             observation.work_area)
                 .has_value()) {
            continue;
        }
        eligible.push_back(target);
    }
    return eligible;
}

}  // namespace

std::vector<Rect> eligible_placement_targets(
    const std::vector<Rect>& candidates, const Observation& observation,
    double clearance_goal, double minimum_fallback) {
    if (!observation.pip.has_value()) {
        return {};
    }
    std::vector<Rect> eligible =
        geometry_eligible_targets(candidates, observation, true);
    if (eligible.empty()) {
        eligible =
            geometry_eligible_targets(candidates, observation, false);
    }
    if (eligible.empty()) {
        return {};
    }

    std::vector<Rect> clearing;
    for (const Rect& target : eligible) {
        if (vacated_fraction(observation.pip->rect, target) >=
            clearance_goal) {
            clearing.push_back(target);
        }
    }
    if (!clearing.empty()) {
        return clearing;
    }

    double best_clearance = 0.0;
    for (const Rect& target : eligible) {
        best_clearance =
            std::max(best_clearance,
                     vacated_fraction(observation.pip->rect, target));
    }
    if (best_clearance < minimum_fallback) {
        return {};
    }
    std::vector<Rect> fallback;
    for (const Rect& target : eligible) {
        if (vacated_fraction(observation.pip->rect, target) >=
            best_clearance - 1e-9) {
            fallback.push_back(target);
        }
    }
    return fallback;
}

PlacementScorer::PlacementScorer(const LinUCBModel& model,
                                 const InteractionTracker& activity,
                                 const VisualActivityTracker& visual)
    : model_(model), activity_(activity), visual_(visual) {}

PlacementScorer::Risks PlacementScorer::risks(
    double now, const Observation& observation, const Rect& target,
    bool include_visual) const {
    const Rect reference = observation.foreground.has_value()
                               ? observation.foreground->rect
                               : observation.work_area;
    Risks result;
    result.activity = activity_.risk_for(target, reference);
    result.manual =
        activity_.manual_preference_for(target, reference);
    result.visual =
        include_visual
            ? visual_.risk_for(target, observation.visual_frame, now)
            : 0.0;
    return result;
}

std::vector<double> PlacementScorer::feature_vector(
    const Observation& observation, const Rect& target,
    const Risks& risks) {
    const Rect& work = observation.work_area;
    if (!observation.pip.has_value() || work.width() <= 0 ||
        work.height() <= 0) {
        return std::vector<double>(kFeatureDimension, 0.0);
    }
    const Rect& current = observation.pip->rect;
    const auto [target_x, target_y] = normalized_position(target, work);
    const auto [current_x, current_y] =
        normalized_position(current, work);
    const double cursor_x = std::clamp(
        static_cast<double>(observation.cursor.x - work.left) /
            work.width(),
        0.0, 1.0);
    const double cursor_y = std::clamp(
        static_cast<double>(observation.cursor.y - work.top) /
            work.height(),
        0.0, 1.0);
    const double diagonal =
        std::max(1.0, std::hypot(work.width(), work.height()));
    const double cursor_distance =
        std::min(1.0,
                 distance_to_rect(target, observation.cursor) / diagonal);
    const double combined_risk =
        std::min(1.0,
                 0.55 * risks.activity + 0.45 * risks.visual);
    return {
        1.0,
        target_x * 2.0 - 1.0,
        target_y * 2.0 - 1.0,
        current_x * 2.0 - 1.0,
        current_y * 2.0 - 1.0,
        cursor_x * 2.0 - 1.0,
        cursor_y * 2.0 - 1.0,
        std::clamp(target_x - current_x, -1.0, 1.0),
        std::clamp(target_y - current_y, -1.0, 1.0),
        cursor_distance,
        combined_risk,
        risks.manual};
}

double PlacementScorer::static_utility(
    const Rect& target, const Observation& observation, const Risks& risks,
    std::optional<double> clearance) {
    const double diagonal = std::max(
        1.0, std::hypot(observation.work_area.width(),
                        observation.work_area.height()));
    const double distance =
        std::min(1.0,
                 distance_to_rect(target, observation.cursor) / diagonal);
    return 0.18 * distance - 0.35 * risks.activity -
           0.50 * risks.visual + 0.08 * risks.manual +
           0.10 * clearance.value_or(0.0);
}

std::vector<double> PlacementScorer::features(
    double now, const Observation& observation, const Rect& target,
    bool include_visual) const {
    const Risks risk_values =
        risks(now, observation, target, include_visual);
    return feature_vector(observation, target, risk_values);
}

PlacementScore PlacementScorer::score(
    double now, const Observation& observation, const Rect& target,
    const std::optional<Rect>& current, bool include_visual) const {
    const Risks risk_values =
        risks(now, observation, target, include_visual);
    PlacementScore result;
    result.features = feature_vector(observation, target, risk_values);
    result.clearance =
        current.has_value()
            ? std::optional<double>(
                  vacated_fraction(*current, target))
            : std::nullopt;
    result.activity_risk = risk_values.activity;
    result.visual_risk = risk_values.visual;
    result.manual_preference = risk_values.manual;
    result.static_utility =
        static_utility(target, observation, risk_values, result.clearance);
    result.learned = model_.score(result.features);
    result.total = result.learned + result.static_utility;
    return result;
}

std::vector<Rect> workspace_centered_candidates(const Rect& work,
                                                int width, int height) {
    std::vector<Rect> candidates;
    candidates.reserve(kMapColumns * kMapRows);
    for (int row = 0; row < kMapRows; ++row) {
        const double center_y =
            work.top + (row + 0.5) * work.height() / kMapRows;
        for (int column = 0; column < kMapColumns; ++column) {
            const double center_x =
                work.left + (column + 0.5) * work.width() / kMapColumns;
            const int left =
                static_cast<int>(python_round(center_x - width / 2.0));
            const int top =
                static_cast<int>(python_round(center_y - height / 2.0));
            candidates.push_back(
                {left, top, left + width, top + height});
        }
    }
    return candidates;
}

}  // namespace schiva::detail
