#pragma once

#include "schiva/core.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace schiva::detail {

inline constexpr int kMapColumns = 24;
inline constexpr int kMapRows = 14;
inline constexpr int kVisualColumns = 16;
inline constexpr int kVisualRows = 9;
inline constexpr int kFeatureDimension = 12;
inline constexpr int kNearMargin = 100;
inline constexpr int kInnerMargin = 20;
inline constexpr int kSafetyMargin = kInnerMargin + 8;
inline constexpr int kMaxStepPixels = 96;
inline constexpr double kClearanceGoal = 0.65;
inline constexpr double kMinFallbackClearance = 0.15;

long long python_round(double value);
Rect expanded(const Rect& rect, int margin);
bool intersects(const Rect& first, const Rect& second);
long long intersection_area(const Rect& first, const Rect& second);
double vacated_fraction(const Rect& original, const Rect& destination);
Rect mouse_work_region(const Point& cursor, int width, int height);
bool contains_point_with_margin(const Rect& rect, const Point& point,
                                int margin = 0);
double distance_to_rect(const Rect& rect, const Point& point);
std::vector<Rect> candidate_rects(
    const Rect& work, int width, int height, const Point& cursor,
    const std::optional<Rect>& current = std::nullopt,
    int safety_margin = kSafetyMargin, int near_margin = kNearMargin,
    const std::optional<Rect>& protected_area = std::nullopt);
bool segment_is_safe(const Point& start, const Point& end, int width,
                     int height, const Point& cursor,
                     int safety_margin = kSafetyMargin);
std::optional<std::vector<Point>> plan_safe_path(
    const Point& start, const Point& target, int width, int height,
    const Point& cursor, int safety_margin = kSafetyMargin,
    const std::optional<Rect>& work = std::nullopt);
bool route_is_safe(const Point& start,
                   const std::vector<Point>& waypoints, int width, int height,
                   const Point& cursor,
                   int safety_margin = kSafetyMargin,
                   const std::optional<Rect>& work = std::nullopt);
std::optional<Point> first_safe_step(const Point& start,
                                     const Point& target, int width,
                                     int height, const Point& cursor,
                                     int safety_margin = kSafetyMargin,
                                     const std::optional<Rect>& work = std::nullopt,
                                     int max_step = kMaxStepPixels);
std::pair<double, double> normalized_position(const Rect& rect,
                                              const Rect& reference);

class MotionTrajectory {
public:
    static MotionTrajectory from_path(const std::vector<Point>& path);

    [[nodiscard]] const Point& start() const;
    [[nodiscard]] const Point& target() const;
    [[nodiscard]] bool complete() const;
    [[nodiscard]] std::vector<Point> remaining_waypoints() const;
    [[nodiscard]] Point advance(double elapsed);
    [[nodiscard]] Point position() const;

    [[nodiscard]] const std::vector<Point>& waypoints() const {
        return waypoints_;
    }
    [[nodiscard]] double elapsed() const { return elapsed_; }
    [[nodiscard]] double duration() const { return duration_; }

private:
    std::vector<Point> waypoints_;
    std::vector<double> segment_lengths_;
    double total_distance_{};
    double duration_{};
    double elapsed_{};

    [[nodiscard]] double progress() const;
};

class LinUCBModel {
public:
    explicit LinUCBModel(int dimension = kFeatureDimension,
                         double alpha = 0.35);
    LinUCBModel(int dimension, double alpha,
                const std::vector<std::vector<double>>& inverse,
                const std::vector<double>& b, int updates);

    [[nodiscard]] int dimension() const { return dimension_; }
    [[nodiscard]] int updates() const { return updates_; }
    [[nodiscard]] int& mutable_updates() { return updates_; }
    [[nodiscard]] double alpha() const { return alpha_; }
    void set_alpha(double alpha);
    [[nodiscard]] std::vector<double> theta() const;
    [[nodiscard]] double predict(const std::vector<double>& features) const;
    [[nodiscard]] double uncertainty(
        const std::vector<double>& features) const;
    [[nodiscard]] double score(const std::vector<double>& features) const;
    void update(const std::vector<double>& features, double reward);
    [[nodiscard]] Json state() const;
    static LinUCBModel from_state(const Json& state, double alpha = 0.35);

private:
    int dimension_;
    double alpha_;
    std::vector<std::vector<double>> inverse_;
    std::vector<double> b_;
    int updates_{};

    void validate_state() const;
    std::vector<double> validate_vector(
        const std::vector<double>& features) const;
    [[nodiscard]] std::vector<double> matvec(
        const std::vector<std::vector<double>>& matrix,
        const std::vector<double>& vector) const;
};

class ActivityMap {
public:
    ActivityMap(int columns = kMapColumns, int rows = kMapRows,
                double half_life = 8.0,
                std::vector<double> values = {});

    void decay(double elapsed);
    void add(double x, double y, const Rect& reference, double weight);
    [[nodiscard]] double density_for_rect(const Rect& target,
                                          const Rect& reference) const;
    [[nodiscard]] Json as_sparse() const;
    static ActivityMap from_sparse(const Json& sparse, double half_life,
                                   int columns = kMapColumns,
                                   int rows = kMapRows);

    [[nodiscard]] int columns() const { return columns_; }
    [[nodiscard]] int rows() const { return rows_; }
    [[nodiscard]] double half_life() const { return half_life_; }
    [[nodiscard]] const std::vector<double>& values() const { return values_; }
    [[nodiscard]] std::vector<double>& values() { return values_; }

private:
    int columns_;
    int rows_;
    double half_life_;
    std::vector<double> values_;

    [[nodiscard]] std::optional<std::pair<int, int>> cell(
        double x, double y, const Rect& reference) const;
};

struct ActivityState {
    ActivityMap short_map{ kMapColumns, kMapRows, 5.0 };
    ActivityMap long_map{ kMapColumns, kMapRows, 180.0 };
    ActivityMap manual_map{ kMapColumns, kMapRows, 600.0 };
};

class InteractionTracker {
public:
    InteractionTracker() = default;

    void set_state(ActivityState state);
    void reset_presence();
    void observe(double now, const Observation& observation);
    void add_manual_preference(const Rect& target, const Rect& reference);
    [[nodiscard]] double risk_for(const Rect& target,
                                  const Rect& reference) const;
    [[nodiscard]] double manual_preference_for(
        const Rect& target, const Rect& reference) const;

    [[nodiscard]] const ActivityState& state() const { return state_; }
    [[nodiscard]] ActivityState& state() { return state_; }
    [[nodiscard]] bool had_activity() const { return had_activity_; }

private:
    ActivityState state_;
    std::optional<double> last_now_;
    std::optional<Point> last_cursor_;
    bool last_buttons_{};
    std::optional<Rect> last_reference_;
    bool had_activity_{};
};

struct VisualEvidence {
    bool valid{};
    bool burst{};
    bool sustained{};
    bool conflict{};
    bool broad{};
    double change_ratio{};
};

class VisualActivityTracker {
public:
    explicit VisualActivityTracker(int columns = kVisualColumns,
                                   int rows = kVisualRows);

    void reset();
    [[nodiscard]] bool availability_at(
        double now, double max_age = 2.5) const;
    VisualEvidence observe(double now, const VisualFrame& frame,
                           const std::optional<Rect>& pip_rect,
                           const std::optional<std::string>& context_key);
    [[nodiscard]] bool broad_active_at(double now) const;
    [[nodiscard]] bool grace_active_at(double now) const;
    [[nodiscard]] bool conflict_for(const Rect& pip_rect) const;
    [[nodiscard]] double risk_for(
        const Rect& target, const std::optional<VisualFrame>& frame,
        std::optional<double> now = std::nullopt) const;

    [[nodiscard]] const std::vector<double>& burst_mask() const {
        return burst_mask_;
    }
    [[nodiscard]] const std::vector<double>& sustained_mask() const {
        return sustained_mask_;
    }
    [[nodiscard]] bool valid_seen() const { return valid_seen_; }

private:
    struct Geometry {
        int width{};
        int height{};
        Rect bounds;
        std::optional<std::string> context_key;

        bool operator==(const Geometry&) const = default;
    };

    int columns_;
    int rows_;
    std::vector<double> burst_mask_;
    std::vector<double> sustained_mask_;
    std::vector<int> active_streak_;
    std::optional<std::vector<double>> previous_;
    std::optional<std::vector<std::uint8_t>> previous_pixels_;
    std::optional<std::vector<bool>> previous_valid_;
    std::optional<std::vector<bool>> last_valid_;
    std::optional<Geometry> geometry_;
    std::optional<double> last_now_;
    std::optional<double> last_sustained_at_;
    double broad_until_{-1.0};
    double grace_until_{-1.0};
    bool previous_broad_candidate_{};
    bool valid_seen_{};

    [[nodiscard]] std::vector<double> cell_means(
        const VisualFrame& frame) const;
    [[nodiscard]] Rect screen_cell(const Rect& bounds, int index) const;
    [[nodiscard]] std::vector<bool> valid_cells(
        const VisualFrame& frame, const std::optional<Rect>& pip_rect) const;
    void clear_spatial_activity();
};

struct ProfileRow {
    std::string profile_key;
    std::string app_id;
    std::string role;
    std::string layout_bin;
    Json short_map = Json::object();
    Json long_map = Json::object();
    Json manual_map = Json::object();
};

class StoreAccess {
public:
    static std::optional<ProfileRow> load_profile(LearningStore& store,
                                                  const std::string& key,
                                                  const std::string& app_id,
                                                  const std::string& role,
                                                  const std::string& layout_bin);
    static bool save_profile(LearningStore& store, const ProfileRow& profile);
    static std::vector<ProfileRow> list_profiles(const LearningStore& store);
    static std::optional<ProfileRow> peek_profile(const LearningStore& store,
                                                  const std::string& key);
    static std::optional<Json> load_model(LearningStore& store);
    static std::optional<Json> peek_model(const LearningStore& store);
    static void save_model(LearningStore& store, const Json& state);
    static bool add_decision(LearningStore& store,
                             const std::string& profile_key,
                             const std::vector<double>& features,
                             const std::string& action, double reward);
    static std::unordered_map<std::string, int> decision_counts(
        const LearningStore& store);
    static int profile_count(const LearningStore& store);
    static int decision_count(const LearningStore& store);
    static int dropped_records(const LearningStore& store);
    static int dirty_profiles(const LearningStore& store);
    static int dirty_decisions(const LearningStore& store);
};

struct PlacementScore {
    double total{};
    double learned{};
    double static_utility{};
    std::vector<double> features;
    double activity_risk{};
    double visual_risk{};
    double manual_preference{};
    std::optional<double> clearance;
};

std::vector<Rect> eligible_placement_targets(
    const std::vector<Rect>& candidates, const Observation& observation,
    double clearance_goal = kClearanceGoal,
    double minimum_fallback = kMinFallbackClearance);

class PlacementScorer {
public:
    PlacementScorer(const LinUCBModel& model,
                    const InteractionTracker& activity,
                    const VisualActivityTracker& visual);

    [[nodiscard]] std::vector<double> features(
        double now, const Observation& observation, const Rect& target,
        bool include_visual = true) const;
    [[nodiscard]] PlacementScore score(
        double now, const Observation& observation, const Rect& target,
        const std::optional<Rect>& current = std::nullopt,
        bool include_visual = true) const;

private:
    const LinUCBModel& model_;
    const InteractionTracker& activity_;
    const VisualActivityTracker& visual_;

    struct Risks {
        double activity{};
        double visual{};
        double manual{};
    };

    [[nodiscard]] Risks risks(double now, const Observation& observation,
                              const Rect& target,
                              bool include_visual) const;
    static std::vector<double> feature_vector(
        const Observation& observation, const Rect& target,
        const Risks& risks);
    static double static_utility(const Rect& target,
                                 const Observation& observation,
                                 const Risks& risks,
                                 std::optional<double> clearance);
};

std::vector<Rect> workspace_centered_candidates(const Rect& work, int width,
                                                int height);

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
    const std::optional<std::string>& selection_note);

}  // namespace schiva::detail
