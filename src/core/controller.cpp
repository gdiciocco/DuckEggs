#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace schiva {
namespace {

constexpr double kTriggerSeconds = 0.035;
constexpr double kExitGraceSeconds = 0.25;
constexpr double kManualGraceSeconds = 0.80;
constexpr double kCooldownSeconds = 0.25;
constexpr double kRetrySeconds = 0.25;
constexpr double kRewardDelaySeconds = 11.5;
constexpr int kPositionTolerance = 8;
constexpr int kAppliedPositionTolerance = 1;
constexpr double kAppliedPositionTimeoutSeconds = 0.25;
constexpr double kExposureSeconds = 0.50;
constexpr double kQuietExposureReward = 0.10;
constexpr double kManualCorrectionReward = -1.0;
constexpr double kQuietInputMaxAgeSeconds = 1.0;
constexpr double kReturnApproachSeconds = 0.12;
constexpr double kMaintenanceIntervalSeconds = 1.0 / 30.0;

bool same_rect(const Rect& first, const Rect& second,
               int tolerance = 0) {
    return std::abs(first.left - second.left) <= tolerance &&
           std::abs(first.top - second.top) <= tolerance &&
           std::abs(first.right - second.right) <= tolerance &&
           std::abs(first.bottom - second.bottom) <= tolerance;
}

bool same_point(const Point& first, const Point& second,
                int tolerance = 0) {
    return std::abs(first.x - second.x) <= tolerance &&
           std::abs(first.y - second.y) <= tolerance;
}

bool sustained_visual_only(const std::string& reason) {
    return reason == "sustained_visual" ||
           reason.rfind("sustained_visual:", 0) == 0;
}

std::string ascii_lower(std::string value) {
    for (char& character : value) {
        const unsigned char byte =
            static_cast<unsigned char>(character);
        if (byte >= 'A' && byte <= 'Z') {
            character = static_cast<char>(byte - 'A' + 'a');
        }
    }
    return value;
}

std::string workspace_layout_bin(const Rect& work) {
    if (work.width() <= 0 || work.height() <= 0) {
        return "invalid";
    }
    const double ratio =
        static_cast<double>(work.width()) / work.height();
    const std::string orientation =
        ratio < 0.8 ? "portrait" : ratio > 1.25 ? "landscape" : "square";
    const int width_bucket =
        std::max(1, static_cast<int>(
                        detail::python_round(work.width() / 256.0)));
    const int height_bucket =
        std::max(1, static_cast<int>(
                        detail::python_round(work.height() / 256.0)));
    return orientation + "-" + std::to_string(width_bucket) + "x" +
           std::to_string(height_bucket);
}

std::string foreground_layout_bin(const Rect& foreground,
                                  const Rect& work) {
    if (foreground.width() <= 0 || foreground.height() <= 0 ||
        work.width() <= 0 || work.height() <= 0) {
        return "invalid";
    }
    const int tolerance_x =
        std::max(4, static_cast<int>(
                        detail::python_round(work.width() * 0.02)));
    const int tolerance_y =
        std::max(4, static_cast<int>(
                        detail::python_round(work.height() * 0.02)));
    const bool left_aligned =
        foreground.left <= work.left + tolerance_x;
    const bool right_aligned =
        foreground.right >= work.right - tolerance_x;
    const bool top_aligned = foreground.top <= work.top + tolerance_y;
    const bool bottom_aligned =
        foreground.bottom >= work.bottom - tolerance_y;
    const double width_ratio =
        static_cast<double>(foreground.width()) / work.width();
    const double height_ratio =
        static_cast<double>(foreground.height()) / work.height();

    if (left_aligned && right_aligned && top_aligned && bottom_aligned) {
        return "maximized";
    }
    if (top_aligned && bottom_aligned && width_ratio >= 0.45 &&
        width_ratio <= 0.55) {
        if (left_aligned) {
            return "snapped-left";
        }
        if (right_aligned) {
            return "snapped-right";
        }
    }
    const int width_bucket = std::max(
        1, std::min(10, static_cast<int>(
                             detail::python_round(width_ratio * 10))));
    const int height_bucket = std::max(
        1, std::min(10, static_cast<int>(
                             detail::python_round(height_ratio * 10))));
    const double relative_aspect =
        (static_cast<double>(foreground.width()) / foreground.height()) /
        (static_cast<double>(work.width()) / work.height());
    const std::string aspect =
        relative_aspect < 0.8
            ? "portrait"
            : relative_aspect > 1.25 ? "wide" : "normal";
    return "free-" + std::to_string(width_bucket) + "x" +
           std::to_string(height_bucket) + "-" + aspect;
}

struct Context {
    std::string key;
    std::string profile_key;
    std::string app_id;
    std::string role;
    std::string layout_bin;
};

std::optional<Context> context_for(const Observation& observation) {
    if (!observation.pip.has_value() ||
        observation.pip->rect.width() <= 0 ||
        observation.pip->rect.height() <= 0) {
        return std::nullopt;
    }
    std::string app_id;
    std::string role;
    std::string layout;
    if (observation.foreground.has_value() &&
        observation.foreground->rect.width() > 0 &&
        observation.foreground->rect.height() > 0) {
        app_id = ascii_lower(observation.foreground->app_id).substr(0, 80);
        role = ascii_lower(observation.foreground->role).substr(0, 80);
        layout = foreground_layout_bin(observation.foreground->rect,
                                       observation.work_area);
    } else {
        app_id = "desktop";
        role = "foreground_unavailable";
        layout = workspace_layout_bin(observation.work_area);
    }
    const std::string key = app_id + "|" + role + "|" + layout;
    return Context{key, key, app_id, role, layout};
}

detail::ActivityState activity_from_profile(
    const detail::ProfileRow& profile) {
    return {
        detail::ActivityMap::from_sparse(profile.short_map, 5.0),
        detail::ActivityMap::from_sparse(profile.long_map, 180.0),
        detail::ActivityMap::from_sparse(profile.manual_map, 600.0)};
}

}  // namespace

struct Controller::Impl {
    struct PendingOutcome {
        std::string context_key;
        std::string profile_key;
        std::string pip_id;
        std::optional<std::string> foreground_id;
        std::vector<double> features;
        std::string action;
        Rect target;
        double due_at{};
        double exposure{};
        bool quiet_censored{};
    };

    struct Motion {
        Rect target;
        Rect source;
        std::string reason;
        double clearance{};
        bool pointer_escape{};
        detail::MotionTrajectory trajectory;
        std::optional<Point> last_emitted;
        std::optional<Point> previous_emitted;
        std::optional<double> awaiting_since;
    };

    Impl(LearningStore& input_store, bool input_observe_only)
        : store(input_store),
          observe_only(input_observe_only),
          owner(std::this_thread::get_id()) {
        const auto persisted = detail::StoreAccess::load_model(store);
        try {
            model = persisted.has_value()
                        ? std::optional<detail::LinUCBModel>(
                              detail::LinUCBModel::from_state(*persisted))
                        : std::optional<detail::LinUCBModel>(
                              detail::LinUCBModel(
                                  detail::kFeatureDimension));
        } catch (const std::exception&) {
            model = detail::LinUCBModel(detail::kFeatureDimension);
        }
        learning_count = model->updates();
    }

    void ensure_open() const {
        if (closed) {
            throw std::runtime_error("controller is closed");
        }
    }

    void ensure_owner() const {
        if (std::this_thread::get_id() != owner) {
            throw std::runtime_error(
                "controller operations must use the owner worker thread");
        }
    }

    Decision decision(
        const std::string& next_state,
        std::optional<Point> move_to = std::nullopt,
        std::optional<std::string> pip_id = std::nullopt,
        std::string reason = {},
        std::optional<Rect> recommendation = std::nullopt) {
        state = next_state;
        Decision result;
        result.state = next_state;
        result.move_to = move_to;
        result.pip_id = std::move(pip_id);
        result.reason = std::move(reason);
        result.recommendation = recommendation;
        result.context_key =
            context.has_value()
                ? std::optional<std::string>(context->key)
                : std::nullopt;
        result.learning_count = learning_count;
        return result;
    }

    void invalidate_opportunity() {
        pending.reset();
        motion.reset();
        last_emitted_rect.reset();
        manual_drag_seen = false;
        return_since.reset();
    }

    void start_content_overlay(double now) {
        // The old outcome belongs to pre-transition content. Detach it while
        // retaining committed motion and its own-interpolation bookkeeping.
        pending.reset();
        return_since.reset();
        activity.begin_content_overlay(now);
        ++content_transition_count;
    }

    void reset_transient(bool visual_reset = false) {
        activity.reset_presence();
        manual_until = -1.0;
        inside = false;
        exit_grace_until = -1.0;
        near_since.reset();
        cooldown_until = -1.0;
        retry_after = -1.0;
        return_since.reset();
        if (visual_reset) {
            visual.reset();
            visual_available = false;
        }
    }

    void save_profile(std::optional<double> now = std::nullopt,
                      bool force = false) {
        if (!profile.has_value()) {
            return;
        }
        if (!force && now.has_value() &&
            last_maintenance_at.has_value() &&
            *now >= *last_maintenance_at &&
            *now - *last_maintenance_at <
                kMaintenanceIntervalSeconds) {
            return;
        }
        profile->short_map = activity.state().short_map.as_sparse();
        profile->long_map = activity.state().long_map.as_sparse();
        profile->manual_map = activity.state().manual_map.as_sparse();
        detail::StoreAccess::save_profile(store, *profile);
    }

    bool maintenance_due(double now) {
        if (!last_maintenance_at.has_value() ||
            now < *last_maintenance_at ||
            now - *last_maintenance_at >=
                kMaintenanceIntervalSeconds) {
            last_maintenance_at = now;
            return true;
        }
        return false;
    }

    void switch_context(const Context& next, const Observation&) {
        if (context.has_value() && context->key == next.key) {
            return;
        }
        save_profile(std::nullopt, true);
        invalidate_opportunity();
        reset_transient(true);
        context = next;
        profile = detail::StoreAccess::load_profile(
            store, next.profile_key, next.app_id, next.role, next.layout_bin);
        if (profile.has_value()) {
            activity.set_state(activity_from_profile(*profile));
        } else {
            activity.set_state(detail::ActivityState{});
        }
        last_observed_pip.reset();
        last_pip_id.reset();
        last_maintenance_at.reset();
    }

    void learn(const PendingOutcome& outcome, double reward) {
        model->update(outcome.features, reward);
        learning_count = model->updates();
        detail::StoreAccess::save_model(store, model->state());
        detail::StoreAccess::add_decision(
            store, outcome.profile_key, outcome.features, outcome.action,
            reward);
    }

    void cancel_motion_for_conflict(double now) {
        pending.reset();
        motion.reset();
        last_emitted_rect.reset();
        manual_drag_seen = false;
        near_since.reset();
        return_since.reset();
        cooldown_until =
            std::max(cooldown_until, now + kCooldownSeconds);
        retry_after = std::max(retry_after, now + kRetrySeconds);
    }

    void manual_correction(double now, const Observation& observation) {
        const std::optional<std::string> foreground_id =
            observation.foreground.has_value()
                ? std::optional<std::string>(
                      observation.foreground->window_id)
                : std::nullopt;
        if (!observe_only && pending.has_value() &&
            context.has_value() &&
            pending->context_key == context->key &&
            observation.pip.has_value() &&
            pending->pip_id == observation.pip->window_id &&
            pending->foreground_id == foreground_id) {
            learn(*pending, kManualCorrectionReward);
        }
        pending.reset();
        motion.reset();
        last_emitted_rect.reset();
        manual_drag_seen = false;
        manual_until = now + kManualGraceSeconds;
        near_since.reset();
        return_since.reset();
        const Rect reference =
            observation.foreground.has_value()
                ? observation.foreground->rect
                : observation.work_area;
        if (observation.pip.has_value()) {
            activity.add_manual_preference(observation.pip->rect, reference);
        }
        save_profile(std::nullopt, true);
        state = "manual_correction";
    }

    bool matches_own_interpolation(const Rect& rect) const {
        if (last_emitted_rect.has_value() &&
            same_rect(rect, *last_emitted_rect, kPositionTolerance)) {
            return true;
        }
        if (!motion.has_value() ||
            !motion->previous_emitted.has_value()) {
            return false;
        }
        return std::abs(rect.left - motion->previous_emitted->x) <=
                   kPositionTolerance &&
               std::abs(rect.top - motion->previous_emitted->y) <=
                   kPositionTolerance;
    }

    bool detect_manual_correction(double now,
                                  const Observation& observation) {
        if (!observation.pip.has_value()) {
            return false;
        }
        const Rect& rect = observation.pip->rect;
        const bool changed_from_previous =
            last_observed_pip.has_value() &&
            !same_rect(rect, *last_observed_pip, kPositionTolerance);
        const bool emitted_matches = matches_own_interpolation(rect);
        if (observation.buttons_down) {
            if (changed_from_previous && !emitted_matches) {
                manual_drag_seen = true;
                motion.reset();
                near_since.reset();
            }
            return false;
        }
        const bool external_change =
            changed_from_previous && !emitted_matches;
        if (manual_drag_seen || external_change) {
            manual_correction(now, observation);
            return true;
        }
        return false;
    }

    std::optional<detail::VisualEvidence> observe_visual(
        double now, const Observation& observation) {
        if (!observation.visual_frame.has_value()) {
            visual_available = visual.availability_at(now);
            return std::nullopt;
        }
        const detail::VisualEvidence evidence = visual.observe(
            now, *observation.visual_frame,
            observation.pip.has_value()
                ? std::optional<Rect>(observation.pip->rect)
                : std::nullopt,
            context.has_value()
                ? std::optional<std::string>(context->key)
                : std::nullopt);
        visual_available = evidence.valid;
        return evidence;
    }

    void update_hover(double now, const Observation& observation) {
        if (!observation.pip.has_value()) {
            return;
        }
        if (detail::contains_point_with_margin(
                observation.pip->rect, observation.cursor,
                detail::kInnerMargin)) {
            inside = true;
            exit_grace_until = -1.0;
            near_since.reset();
        } else if (inside) {
            inside = false;
            exit_grace_until = now + kExitGraceSeconds;
        }
    }

    static bool helper_interaction(const Observation& observation) {
        for (const Rect& rect :
             observation.interaction_exclusions) {
            if (rect.left <= observation.cursor.x &&
                observation.cursor.x < rect.right &&
                rect.top <= observation.cursor.y &&
                observation.cursor.y < rect.bottom) {
                return true;
            }
        }
        return false;
    }

    static bool caret_conflict(const std::optional<Rect>& caret,
                               const Rect& pip) {
        return caret.has_value() &&
               detail::intersects(detail::expanded(pip, 12), *caret);
    }

    void settle_pending(double now, const Observation& observation,
                        double elapsed) {
        if (!pending.has_value() || !context.has_value()) {
            return;
        }
        if (pending->context_key != context->key ||
            !observation.pip.has_value()) {
            pending.reset();
            return_since.reset();
            return;
        }
        const std::optional<std::string> foreground_id =
            observation.foreground.has_value()
                ? std::optional<std::string>(
                      observation.foreground->window_id)
                : std::nullopt;
        if (observation.pip->window_id != pending->pip_id ||
            foreground_id != pending->foreground_id) {
            pending.reset();
            return_since.reset();
            return;
        }

        const Rect& pip_rect = observation.pip->rect;
        const Rect& reference =
            observation.foreground.has_value()
                ? observation.foreground->rect
                : observation.work_area;
        const bool inside_pip_control =
            detail::contains_point_with_margin(
                pip_rect, observation.cursor, detail::kInnerMargin);
        const bool recent_input =
            observation.input_active &&
            std::isfinite(observation.input_age) &&
            observation.input_age >= 0.0 &&
            observation.input_age <= kQuietInputMaxAgeSeconds;
        const bool in_reference =
            reference.left <= observation.cursor.x &&
            observation.cursor.x < reference.right &&
            reference.top <= observation.cursor.y &&
            observation.cursor.y < reference.bottom;
        const bool arrived =
            same_rect(pip_rect, pending->target, kPositionTolerance);
        const bool near_selected_target =
            detail::distance_to_rect(pending->target,
                                     observation.cursor) <=
            detail::kNearMargin;
        const bool returning =
            arrived && !observation.buttons_down &&
            !inside_pip_control &&
            in_reference && near_selected_target && recent_input;
        if (returning) {
            if (!return_since.has_value()) {
                return_since = now;
            }
            if (now - *return_since >= kReturnApproachSeconds) {
                learn(*pending, kManualCorrectionReward);
                cancel_motion_for_conflict(now);
                return;
            }
        } else {
            return_since.reset();
        }

        if (arrived && caret_conflict(observation.caret,
                                      pending->target)) {
            learn(*pending, kManualCorrectionReward);
            cancel_motion_for_conflict(now);
            return;
        }
        if (arrived &&
            recent_input && !observation.buttons_down &&
            !inside_pip_control && !returning &&
            !pending->quiet_censored) {
            pending->exposure +=
                std::min(std::max(elapsed, 0.0), 1.0);
        }
        if (now < pending->due_at) {
            return;
        }
        if (arrived &&
            pending->exposure >= kExposureSeconds &&
            !pending->quiet_censored && !return_since.has_value()) {
            learn(*pending, kQuietExposureReward);
        }
        pending.reset();
        return_since.reset();
    }

    std::string trigger_reason(
        double now, const Observation& observation,
        const std::optional<detail::VisualEvidence>& evidence) {
        if (!observation.pip.has_value()) {
            return {};
        }
        const Rect& rect = observation.pip->rect;
        if (inside) {
            near_since.reset();
            return {};
        }
        const bool blocked =
            observation.buttons_down ||
            now < exit_grace_until ||
            now < manual_until ||
            now < cooldown_until ||
            now < retry_after;
        const bool near = detail::contains_point_with_margin(
            rect, observation.cursor, detail::kNearMargin);
        if (near && !blocked) {
            if (!near_since.has_value()) {
                near_since = now;
            }
        } else {
            near_since.reset();
        }
        if (observation.buttons_down || now < exit_grace_until ||
            now < manual_until || now < cooldown_until ||
            now < retry_after) {
            return {};
        }
        if (near_since.has_value() &&
            now - *near_since >= kTriggerSeconds) {
            return "cursor_near";
        }
        const bool fresh_visual_conflict =
            evidence.has_value() && evidence->sustained &&
            evidence->conflict;
        if (!visual.broad_active_at(now) &&
            (fresh_visual_conflict ||
             (visual.grace_active_at(now) &&
              visual.conflict_for(rect)))) {
            return "sustained_visual";
        }
        if (caret_conflict(observation.caret, rect)) {
            return "caret_conflict";
        }
        return {};
    }

    std::optional<std::tuple<Rect, std::vector<double>,
                             std::string, double>>
    choose_target(double now, const Observation& observation,
                  bool pointer_escape) {
        if (!observation.pip.has_value()) {
            return std::nullopt;
        }
        const int width = observation.pip->rect.width();
        const int height = observation.pip->rect.height();
        std::vector<Rect> candidates =
            detail::candidate_rects(observation.work_area, width, height,
                                    observation.cursor,
                                    observation.pip->rect);
        if (candidates.empty()) {
            return std::nullopt;
        }
        if (pointer_escape) {
            // Apply the route contract before clearance ranking. Otherwise a
            // clearing target without a monotonic route can hide a valid
            // lower-clearance fallback.
            candidates.erase(
                std::remove_if(
                    candidates.begin(), candidates.end(),
                    [&](const Rect& target) {
                        return !detail::plan_safe_path(
                                    {observation.pip->rect.left,
                                     observation.pip->rect.top},
                                    {target.left, target.top}, width,
                                    height, observation.cursor,
                                    detail::kSafetyMargin,
                                    observation.work_area, true)
                                    .has_value();
                    }),
                candidates.end());
            if (candidates.empty()) {
                retry_after = now + kRetrySeconds;
                return std::nullopt;
            }
        }
        candidates = detail::eligible_placement_targets(
            candidates, observation);
        if (candidates.empty()) {
            retry_after = now + kRetrySeconds;
            return std::nullopt;
        }

        const detail::PlacementScorer scorer(*model, activity, visual);
        std::optional<std::tuple<double, double, int, Rect,
                                 std::vector<double>, std::string, double>>
            best;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const Rect& target = candidates[index];
            const detail::PlacementScore score =
                scorer.score(now, observation, target,
                             observation.pip->rect);
            std::string action = "anchor-";
            if (index < 10) {
                action += "0";
            }
            action += std::to_string(index);
            const double clearance = score.clearance.value_or(0.0);
            const auto candidate = std::make_tuple(
                score.total, score.learned,
                -static_cast<int>(index), target, score.features, action,
                clearance);
            if (!best.has_value() ||
                std::get<0>(candidate) > std::get<0>(*best) ||
                (std::get<0>(candidate) == std::get<0>(*best) &&
                 (std::get<1>(candidate) > std::get<1>(*best) ||
                  (std::get<1>(candidate) == std::get<1>(*best) &&
                   std::get<2>(candidate) >
                       std::get<2>(*best))))) {
                best = candidate;
            }
        }
        if (!best.has_value()) {
            retry_after = now + kRetrySeconds;
            return std::nullopt;
        }
        return std::make_tuple(std::get<3>(*best), std::get<4>(*best),
                               std::get<5>(*best), std::get<6>(*best));
    }

    Decision tick(double now, const Observation& observation) {
        ensure_open();
        ensure_owner();
        if (!std::isfinite(now)) {
            throw std::invalid_argument("tick time must be finite");
        }
        const double elapsed =
            current_now.has_value()
                ? std::max(0.0, now - *current_now)
                : 0.0;
        current_now = now;
        last_observation = observation;

        if (paused_value) {
            visual_available = false;
            invalidate_opportunity();
            near_since.reset();
            return decision("paused", std::nullopt, std::nullopt, "paused");
        }
        const bool valid_pip =
            observation.pip.has_value() &&
            observation.pip->rect.width() > 0 &&
            observation.pip->rect.height() > 0;
        if (!valid_pip) {
            save_profile(std::nullopt, true);
            context.reset();
            profile.reset();
            last_observed_pip.reset();
            last_pip_id.reset();
            invalidate_opportunity();
            reset_transient(true);
            activity.clear_content_overlay();
            last_maintenance_at.reset();
            return decision("no_pip", std::nullopt, std::nullopt,
                            "pip_unavailable");
        }
        const std::optional<Context> next_context =
            context_for(observation);
        if (!next_context.has_value()) {
            return decision("no_pip", std::nullopt, std::nullopt,
                            "invalid_pip_geometry");
        }
        switch_context(*next_context, observation);
        if (last_pip_id.has_value() &&
            observation.pip->window_id != *last_pip_id) {
            invalidate_opportunity();
            last_observed_pip.reset();
            near_since.reset();
            activity.reset_presence();
            activity.clear_content_overlay();
        }
        last_pip_id = observation.pip->window_id;

        if (helper_interaction(observation)) {
            pending.reset();
            motion.reset();
            last_emitted_rect.reset();
            return_since.reset();
            near_since.reset();
            inside = false;
            exit_grace_until =
                std::max(exit_grace_until, now + kExitGraceSeconds);
            cooldown_until =
                std::max(cooldown_until, now + kCooldownSeconds);
            const bool due = maintenance_due(now);
            if (due) {
                activity.observe(now, observation);
            }
            last_observed_pip = observation.pip->rect;
            if (due) {
                save_profile(std::nullopt, true);
            }
            return decision("helper_interaction", std::nullopt,
                            std::nullopt, "own_ui_interaction");
        }

        const bool due = maintenance_due(now);
        std::optional<detail::VisualEvidence> evidence;
        if (due || observation.visual_frame.has_value()) {
            evidence = observe_visual(now, observation);
        }
        if (evidence.has_value() && evidence->content_transition) {
            start_content_overlay(now);
        }
        if (due) {
            activity.observe(now, observation);
        }
        update_hover(now, observation);

        const std::optional<Rect> previous_pip_rect =
            last_observed_pip;
        const bool manual =
            detect_manual_correction(now, observation);
        last_observed_pip = observation.pip->rect;
        if (manual) {
            save_profile(std::nullopt, true);
            return decision("manual_correction", std::nullopt,
                            std::nullopt, "manual_geometry_change",
                            observation.pip->rect);
        }
        if (visual.broad_active_at(now)) {
            if (motion.has_value() &&
                sustained_visual_only(motion->reason)) {
                pending.reset();
                motion.reset();
                last_emitted_rect.reset();
            } else if (pending.has_value()) {
                pending->quiet_censored = true;
            }
        }
        settle_pending(now, observation, elapsed);
        if (due) {
            save_profile(std::nullopt, true);
        }

        if (motion.has_value()) {
            if (observation.buttons_down || inside ||
                now < exit_grace_until) {
                near_since.reset();
                if (motion->awaiting_since.has_value()) {
                    motion->awaiting_since = now;
                }
                return decision(
                    observation.buttons_down
                        ? "mouse_button_hold"
                        : inside ? "pointer_over_pip"
                                 : "pointer_exit_grace",
                    std::nullopt, std::nullopt, "movement_frozen",
                    motion->target);
            }
            if (same_rect(observation.pip->rect, motion->target,
                          kAppliedPositionTolerance)) {
                motion.reset();
                cooldown_until = std::max(
                    cooldown_until, now + kCooldownSeconds);
            } else {
                const Point current_point{
                    observation.pip->rect.left,
                    observation.pip->rect.top};
                if (motion->last_emitted.has_value() &&
                    previous_pip_rect.has_value() &&
                    *previous_pip_rect == observation.pip->rect &&
                    !same_point(current_point, *motion->last_emitted,
                                kAppliedPositionTolerance)) {
                    if (!motion->awaiting_since.has_value()) {
                        motion->awaiting_since = now;
                    } else if (now - *motion->awaiting_since >=
                               kAppliedPositionTimeoutSeconds) {
                        const Rect target = motion->target;
                        movement_failed();
                        return decision(
                            "movement_failed", std::nullopt,
                            std::nullopt, "applied_position_timeout",
                            target);
                    }
                    return decision(
                        "moving_stalled", std::nullopt, std::nullopt,
                        "awaiting_applied_position", motion->target);
                }
                motion->awaiting_since.reset();

                const int width = observation.pip->rect.width();
                const int height = observation.pip->rect.height();
                const bool pointer_threatens_escape =
                    motion->pointer_escape &&
                    (detail::contains_point_with_margin(
                         observation.pip->rect, observation.cursor,
                         detail::kNearMargin) ||
                     detail::contains_point_with_margin(
                         motion->source, observation.cursor,
                         detail::kNearMargin));
                std::vector<Point> remaining =
                    motion->trajectory.remaining_waypoints();
                if (!detail::route_is_safe(
                        current_point, remaining, width, height,
                        observation.cursor, detail::kSafetyMargin,
                        observation.work_area,
                        pointer_threatens_escape)) {
                    const auto path = detail::plan_safe_path(
                        current_point,
                        {motion->target.left, motion->target.top}, width,
                        height, observation.cursor, detail::kSafetyMargin,
                        observation.work_area,
                        pointer_threatens_escape);
                    if (!path.has_value()) {
                        cancel_motion_for_conflict(now);
                        return decision(
                            "cursor_conflict", std::nullopt,
                            std::nullopt, "no_safe_committed_route");
                    }
                    motion->trajectory =
                        detail::MotionTrajectory::from_path(*path);
                    motion->last_emitted.reset();
                    motion->awaiting_since.reset();
                }
                remaining = motion->trajectory.remaining_waypoints();
                motion->previous_emitted = motion->last_emitted;
                Point next_point =
                    motion->trajectory.advance(elapsed);
                if (!detail::segment_is_safe(
                        current_point, next_point, width, height,
                        observation.cursor, detail::kSafetyMargin)) {
                    const std::optional<Point> corner =
                        remaining.empty()
                            ? std::nullopt
                            : std::optional<Point>(
                                  remaining.front());
                    if (!corner.has_value() ||
                        !detail::segment_is_safe(
                            current_point, *corner, width, height,
                            observation.cursor,
                            detail::kSafetyMargin)) {
                        cancel_motion_for_conflict(now);
                        return decision(
                            "cursor_conflict", std::nullopt,
                            std::nullopt, "unsafe_integer_segment");
                    }
                    next_point = *corner;
                }
                motion->last_emitted = next_point;
                last_emitted_rect =
                    observation.pip->rect.moved_to(next_point.x,
                                                   next_point.y);
                return decision(
                    "moving", next_point, observation.pip->window_id,
                    motion->reason, motion->target);
            }
        }

        const std::string reason =
            trigger_reason(now, observation, evidence);
        if (reason.empty()) {
            if (observation.buttons_down) {
                return decision("mouse_button_hold");
            }
            if (detail::contains_point_with_margin(
                    observation.pip->rect, observation.cursor,
                    detail::kInnerMargin)) {
                return decision("pointer_over_pip");
            }
            if (now < exit_grace_until) {
                return decision("pointer_exit_grace");
            }
            if (now < cooldown_until) {
                return decision("cooldown");
            }
            if (near_since.has_value()) {
                return decision("near_pip");
            }
            return decision("idle");
        }

        const auto chosen =
            choose_target(now, observation, reason == "cursor_near");
        if (!chosen.has_value()) {
            return decision(
                "no_safe_target", std::nullopt, std::nullopt,
                reason + ":no_materially_better_safe_target");
        }
        const auto& [target, features, action, clearance] = *chosen;
        const std::string chosen_reason =
            clearance >= detail::kClearanceGoal
                ? reason
                : reason + ":cramped_fallback";
        if (observe_only) {
            retry_after = now + kRetrySeconds;
            return decision("observe_only", std::nullopt, std::nullopt,
                            chosen_reason, target);
        }

        const auto path = detail::plan_safe_path(
            {observation.pip->rect.left, observation.pip->rect.top},
            {target.left, target.top}, observation.pip->rect.width(),
            observation.pip->rect.height(), observation.cursor,
            detail::kSafetyMargin, observation.work_area,
            reason == "cursor_near");
        if (!path.has_value()) {
            retry_after = now + kRetrySeconds;
            return decision("no_safe_target", std::nullopt,
                            std::nullopt, reason + ":unreachable");
        }
        detail::MotionTrajectory trajectory =
            detail::MotionTrajectory::from_path(*path);
        const Point first =
            trajectory.advance(std::min(elapsed, 1.0 / 60.0));
        motion = Motion{target, observation.pip->rect, chosen_reason, clearance,
                        reason == "cursor_near",
                        std::move(trajectory), first, std::nullopt,
                        std::nullopt};
        pending = PendingOutcome{
            context->key,
            context->profile_key,
            observation.pip->window_id,
            observation.foreground.has_value()
                ? std::optional<std::string>(
                      observation.foreground->window_id)
                : std::nullopt,
            features,
            action,
            target,
            now + kRewardDelaySeconds,
            0.0};
        return_since.reset();
        cooldown_until =
            std::max(cooldown_until, now + kCooldownSeconds);
        last_emitted_rect =
            observation.pip->rect.moved_to(first.x, first.y);
        return decision("moving", first, observation.pip->window_id,
                        chosen_reason, target);
    }

    void movement_failed() {
        ensure_open();
        pending.reset();
        motion.reset();
        last_emitted_rect.reset();
        near_since.reset();
        return_since.reset();
        const double now = current_now.value_or(0.0);
        cooldown_until = now + kCooldownSeconds;
        retry_after = now + kRetrySeconds;
        state = "movement_failed";
    }

    void close() {
        if (closed) {
            return;
        }
        save_profile(std::nullopt, true);
        store.flush();
        closed = true;
    }

    LearningStore& store;
    bool observe_only{};
    bool paused_value{};
    bool closed{};
    std::thread::id owner;
    std::string state{"idle"};
    std::optional<Context> context;
    std::optional<detail::ProfileRow> profile;
    detail::InteractionTracker activity;
    detail::VisualActivityTracker visual;
    bool visual_available{};
    std::optional<PendingOutcome> pending;
    std::optional<Motion> motion;
    std::optional<double> current_now;
    std::optional<Rect> last_observed_pip;
    std::optional<std::string> last_pip_id;
    std::optional<Rect> last_emitted_rect;
    std::optional<Observation> last_observation;
    bool manual_drag_seen{};
    double manual_until{-1.0};
    bool inside{};
    double exit_grace_until{-1.0};
    std::optional<double> near_since;
    double cooldown_until{-1.0};
    double retry_after{-1.0};
    std::optional<double> return_since;
    std::optional<double> last_maintenance_at;
    std::optional<detail::LinUCBModel> model;
    int learning_count{};
    int content_transition_count{};
};

Controller::Controller(LearningStore& store, bool observe_only)
    : impl_(std::make_unique<Impl>(store, observe_only)) {}

Controller::~Controller() {
    if (impl_ != nullptr) {
        try {
            impl_->close();
        } catch (...) {
        }
    }
}

bool Controller::paused() const {
    impl_->ensure_owner();
    return impl_->paused_value;
}

void Controller::set_paused(bool paused) {
    impl_->ensure_owner();
    if (paused == impl_->paused_value) {
        return;
    }
    impl_->paused_value = paused;
    impl_->invalidate_opportunity();
    impl_->near_since.reset();
    if (paused) {
        impl_->state = "paused";
        impl_->visual_available = false;
    } else {
        impl_->state = "idle";
    }
}

Decision Controller::tick(double monotonic_seconds,
                          const Observation& observation) {
    return impl_->tick(monotonic_seconds, observation);
}

void Controller::movement_failed() {
    impl_->movement_failed();
}

Json Controller::status() const {
    impl_->ensure_owner();
    Json status = {
        {"mode", "adaptive"},
        {"paused", impl_->paused_value},
        {"motion_active", impl_->motion.has_value()},
        {"learning_count", impl_->learning_count},
        {"context_key",
         impl_->context.has_value() ? Json(impl_->context->key)
                                    : Json(nullptr)},
        {"state", impl_->state},
        {"visual_available", impl_->visual_available}};
    if (impl_->activity.content_overlay_active()) {
        status["content_overlay_active"] = true;
    }
    if (impl_->content_transition_count > 0) {
        status["content_transition_count"] =
            impl_->content_transition_count;
    }
    if (impl_->visual.broad_active_at(
            impl_->current_now.value_or(0.0))) {
        status["visual_broad_update"] = true;
    }
    return status;
}

Json Controller::insights(
    std::optional<std::string> profile_key) const {
    impl_->ensure_open();
    impl_->ensure_owner();
    std::vector<detail::ProfileRow> profiles =
        detail::StoreAccess::list_profiles(impl_->store);
    const auto counts =
        detail::StoreAccess::decision_counts(impl_->store);
    const std::optional<std::string> active_key =
        impl_->context.has_value()
            ? std::optional<std::string>(impl_->context->profile_key)
            : std::nullopt;
    std::map<std::string, detail::ProfileRow> by_key;
    for (const detail::ProfileRow& profile : profiles) {
        by_key[profile.profile_key] = profile;
    }
    if (active_key.has_value() && !by_key.contains(*active_key) &&
        impl_->profile.has_value()) {
        by_key[*active_key] = *impl_->profile;
        profiles.clear();
        for (const auto& [key, profile] : by_key) {
            (void)key;
            profiles.push_back(profile);
        }
    }

    std::optional<std::string> selected_key;
    std::optional<std::string> selection_note;
    if (!profile_key.has_value()) {
        if (active_key.has_value() && by_key.contains(*active_key)) {
            selected_key = *active_key;
        } else if (!profiles.empty()) {
            selected_key = profiles.front().profile_key;
        }
    } else {
        selected_key = *profile_key;
        if (!by_key.contains(*selected_key)) {
            selected_key.reset();
            selection_note =
                "The requested profile is not learned; no profile was "
                "created.";
        }
    }
    std::optional<Rect> recommendation;
    if (selected_key.has_value() && active_key.has_value() &&
        *selected_key == *active_key) {
        if (impl_->motion.has_value()) {
            recommendation = impl_->motion->target;
        } else if (impl_->pending.has_value()) {
            recommendation = impl_->pending->target;
        }
    }
    const bool selected_is_active =
        selected_key.has_value() && active_key.has_value() &&
        *selected_key == *active_key;
    std::optional<detail::ProfileRow> selected_profile;
    if (selected_key.has_value()) {
        selected_profile = by_key.at(*selected_key);
    }
    const std::optional<detail::ActivityState> active_state =
        impl_->context.has_value()
            ? std::optional<detail::ActivityState>(
                  impl_->activity.effective_state())
            : std::nullopt;
    const double now = impl_->current_now.value_or(0.0);
    if (selected_is_active && impl_->visual_available &&
        !impl_->paused_value) {
        return detail::build_insights(
            *impl_->model, profiles, counts, selected_profile,
            selected_key, active_key, active_state,
            impl_->last_observation, impl_->visual, impl_->learning_count,
            recommendation, now, selection_note);
    }
    const detail::VisualActivityTracker empty_visual;
    return detail::build_insights(
        *impl_->model, profiles, counts, selected_profile, selected_key,
        active_key, active_state, impl_->last_observation, empty_visual,
        impl_->learning_count, recommendation, now, selection_note);
}

void Controller::close() {
    impl_->ensure_owner();
    impl_->close();
}

}  // namespace schiva
