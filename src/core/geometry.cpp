#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace schiva::detail {
namespace {

constexpr std::array<double, 5> kAnchorFractions = {0.0, 0.25, 0.5, 0.75,
                                                    1.0};

bool within_work(const Point& point, int width, int height,
                 const std::optional<Rect>& work) {
    if (!work.has_value()) {
        return true;
    }
    return work->left <= point.x &&
           point.x <= work->right - width &&
           work->top <= point.y &&
           point.y <= work->bottom - height;
}

Rect forbidden_top_left(int width, int height, const Point& cursor,
                        int margin) {
    return {cursor.x - margin - width, cursor.y - margin - height,
            cursor.x + margin, cursor.y + margin};
}

bool segment_hits_forbidden(double start, double end, double low,
                            double high) {
    if (std::abs(end - start) <= 1e-12) {
        return low < start && start < high;
    }
    double entry = (low - start) / (end - start);
    double exit = (high - start) / (end - start);
    if (entry > exit) {
        std::swap(entry, exit);
    }
    return std::max(0.0, entry) < std::min(1.0, exit);
}

std::vector<Point> compact(const std::vector<Point>& points) {
    std::vector<Point> result;
    result.reserve(points.size());
    for (const Point& point : points) {
        if (result.empty() || !(result.back() == point)) {
            result.push_back(point);
        }
    }
    return result;
}

bool route_preserves_pointer_clearance(
    const Point& start, const std::vector<Point>& route, int width,
    int height, const Point& cursor, double tolerance = 1.0) {
    if (route.empty()) {
        return true;
    }
    double required = distance_to_rect(
        {start.x, start.y, start.x + width, start.y + height}, cursor);
    Point current = start;
    for (const Point& point : route) {
        if (point == current) {
            continue;
        }
        const double dx = point.x - current.x;
        const double dy = point.y - current.y;
        const int samples = std::max(
            1, static_cast<int>(
                   std::ceil(std::hypot(dx, dy) / 2.0)));
        for (int sample = 1; sample <= samples; ++sample) {
            const double fraction =
                static_cast<double>(sample) / samples;
            const int x = static_cast<int>(python_round(
                current.x + dx * fraction));
            const int y = static_cast<int>(python_round(
                current.y + dy * fraction));
            const double clearance =
                distance_to_rect({x, y, x + width, y + height}, cursor);
            if (clearance + tolerance < required) {
                return false;
            }
            required = std::max(required, clearance);
        }
        current = point;
    }
    return true;
}

}  // namespace

long long python_round(double value) {
    return static_cast<long long>(std::nearbyint(value));
}

Rect expanded(const Rect& rect, int margin) {
    return {rect.left - margin, rect.top - margin, rect.right + margin,
            rect.bottom + margin};
}

bool intersects(const Rect& first, const Rect& second) {
    return first.left < second.right && first.right > second.left &&
           first.top < second.bottom && first.bottom > second.top;
}

long long intersection_area(const Rect& first, const Rect& second) {
    const long long width =
        std::max(0, std::min(first.right, second.right) -
                        std::max(first.left, second.left));
    const long long height =
        std::max(0, std::min(first.bottom, second.bottom) -
                        std::max(first.top, second.top));
    return width * height;
}

double vacated_fraction(const Rect& original, const Rect& destination) {
    const long long area =
        static_cast<long long>(original.width()) * original.height();
    if (area <= 0) {
        return 0.0;
    }
    const double overlap =
        static_cast<double>(intersection_area(original, destination)) /
        static_cast<double>(area);
    return std::clamp(1.0 - overlap, 0.0, 1.0);
}

Rect mouse_work_region(const Point& cursor, int width, int height) {
    width = std::max(kSafetyMargin * 2, width);
    height = std::max(kSafetyMargin * 2, height);
    const int left = cursor.x - width / 2;
    const int top = cursor.y - height / 2;
    return {left, top, left + width, top + height};
}

bool contains_point_with_margin(const Rect& rect, const Point& point,
                                int margin) {
    return rect.left - margin <= point.x &&
           point.x <= rect.right + margin &&
           rect.top - margin <= point.y &&
           point.y <= rect.bottom + margin;
}

double distance_to_rect(const Rect& rect, const Point& point) {
    const double nearest_x =
        std::clamp(static_cast<double>(point.x),
                   static_cast<double>(rect.left),
                   static_cast<double>(rect.right));
    const double nearest_y =
        std::clamp(static_cast<double>(point.y),
                   static_cast<double>(rect.top),
                   static_cast<double>(rect.bottom));
    return std::hypot(point.x - nearest_x, point.y - nearest_y);
}

std::vector<Rect> candidate_rects(
    const Rect& work, int width, int height, const Point& cursor,
    const std::optional<Rect>& current, int safety_margin, int near_margin,
    const std::optional<Rect>& protected_area) {
    if (width <= 0 || height <= 0 || width > work.width() ||
        height > work.height()) {
        return {};
    }
    const int available_x = work.width() - width;
    const int available_y = work.height() - height;
    std::vector<Point> positions;
    positions.reserve(25);
    for (double fraction_y : kAnchorFractions) {
        for (double fraction_x : kAnchorFractions) {
            positions.push_back(
                {work.left +
                     static_cast<int>(python_round(available_x * fraction_x)),
                 work.top +
                     static_cast<int>(python_round(available_y * fraction_y))});
        }
    }

    const Rect cursor_rect{cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
    std::vector<Rect> unique;
    std::unordered_set<long long> seen;
    for (const Point& position : positions) {
        const long long key =
            (static_cast<long long>(position.x) << 32) ^
            static_cast<std::uint32_t>(position.y);
        if (!seen.insert(key).second) {
            continue;
        }
        const Rect target{position.x, position.y, position.x + width,
                          position.y + height};
        if (current.has_value() &&
            std::abs(position.x - current->left) <= 8 &&
            std::abs(position.y - current->top) <= 8) {
            continue;
        }
        if (intersects(expanded(target, safety_margin), cursor_rect)) {
            continue;
        }
        if (protected_area.has_value() &&
            intersects(target, *protected_area)) {
            continue;
        }
        unique.push_back(target);
    }

    std::vector<Rect> preferred;
    for (const Rect& target : unique) {
        if (!intersects(expanded(target, near_margin), cursor_rect)) {
            preferred.push_back(target);
        }
    }
    return preferred.empty() ? unique : preferred;
}

bool segment_is_safe(const Point& start, const Point& end, int width,
                     int height, const Point& cursor, int safety_margin) {
    const Rect forbidden =
        forbidden_top_left(width, height, cursor, safety_margin);
    const bool x_entry = segment_hits_forbidden(
        start.x, end.x, forbidden.left, forbidden.right);
    const bool y_entry = segment_hits_forbidden(
        start.y, end.y, forbidden.top, forbidden.bottom);
    return !(x_entry && y_entry);
}

std::optional<std::vector<Point>> plan_safe_path(
    const Point& start, const Point& target, int width, int height,
    const Point& cursor, int safety_margin,
    const std::optional<Rect>& work, bool nondecreasing_clearance) {
    std::vector<std::vector<Point>> routes;
    const bool direct_safe =
        segment_is_safe(start, target, width, height, cursor,
                        safety_margin);
    const bool direct_preserves_clearance =
        !nondecreasing_clearance ||
        route_preserves_pointer_clearance(
            start, {target}, width, height, cursor);
    if (!direct_safe || !direct_preserves_clearance) {
        const Rect forbidden =
            forbidden_top_left(width, height, cursor, safety_margin);
        for (int edge_x : {forbidden.left, forbidden.right}) {
            routes.push_back(
                {start, {edge_x, start.y}, {edge_x, target.y}, target});
        }
        for (int edge_y : {forbidden.top, forbidden.bottom}) {
            routes.push_back(
                {start, {start.x, edge_y}, {target.x, edge_y}, target});
        }
    } else {
        routes.push_back({start, target});
    }

    for (const std::vector<Point>& raw_route : routes) {
        const std::vector<Point> route = compact(raw_route);
        bool all_within = true;
        for (const Point& point : route) {
            if (!within_work(point, width, height, work)) {
                all_within = false;
                break;
            }
        }
        if (!all_within) {
            continue;
        }
        bool safe = true;
        for (std::size_t index = 1; index < route.size(); ++index) {
            if (!segment_is_safe(route[index - 1], route[index], width,
                                 height, cursor, safety_margin)) {
                safe = false;
                break;
            }
        }
        if (nondecreasing_clearance &&
            !route_preserves_pointer_clearance(
                start, route, width, height, cursor)) {
            safe = false;
        }
        if (safe) {
            return route;
        }
    }
    return std::nullopt;
}

bool route_is_safe(const Point& start,
                   const std::vector<Point>& waypoints, int width, int height,
                   const Point& cursor, int safety_margin,
                   const std::optional<Rect>& work,
                   bool nondecreasing_clearance) {
    Point current = start;
    if (!within_work(current, width, height, work)) {
        return false;
    }
    if (nondecreasing_clearance &&
        !route_preserves_pointer_clearance(
            start, waypoints, width, height, cursor)) {
        return false;
    }
    for (const Point& point : waypoints) {
        if (!within_work(point, width, height, work) ||
            !segment_is_safe(current, point, width, height, cursor,
                             safety_margin)) {
            return false;
        }
        current = point;
    }
    return true;
}

std::optional<Point> first_safe_step(const Point& start,
                                     const Point& target, int width,
                                     int height, const Point& cursor,
                                     int safety_margin,
                                     const std::optional<Rect>& work,
                                     int max_step) {
    const auto path = plan_safe_path(start, target, width, height, cursor,
                                     safety_margin, work);
    if (!path.has_value()) {
        return std::nullopt;
    }
    if (path->size() == 1) {
        return path->front();
    }
    const Point next_corner = (*path)[1];
    const double dx = next_corner.x - start.x;
    const double dy = next_corner.y - start.y;
    const double distance = std::hypot(dx, dy);
    if (distance <= max_step) {
        return next_corner;
    }
    const double factor = max_step / distance;
    const Point step{
        static_cast<int>(python_round(start.x + dx * factor)),
        static_cast<int>(python_round(start.y + dy * factor))};
    if (!segment_is_safe(start, step, width, height, cursor,
                         safety_margin)) {
        return std::nullopt;
    }
    return step;
}

std::pair<double, double> normalized_position(const Rect& rect,
                                              const Rect& reference) {
    if (reference.width() <= 0 || reference.height() <= 0) {
        return {0.5, 0.5};
    }
    return {
        std::clamp(static_cast<double>(rect.left - reference.left) /
                       reference.width(),
                   0.0, 1.0),
        std::clamp(static_cast<double>(rect.top - reference.top) /
                       reference.height(),
                   0.0, 1.0)};
}

MotionTrajectory MotionTrajectory::from_path(const std::vector<Point>& path) {
    if (path.empty()) {
        throw std::invalid_argument("motion path must contain a start");
    }
    MotionTrajectory trajectory;
    trajectory.waypoints_ = compact(path);
    if (trajectory.waypoints_.size() == 1) {
        trajectory.segment_lengths_.push_back(0.0);
        return trajectory;
    }

    long double total = 0.0L;
    for (std::size_t index = 1; index < trajectory.waypoints_.size();
         ++index) {
        const Point& first = trajectory.waypoints_[index - 1];
        const Point& second = trajectory.waypoints_[index];
        const double length =
            std::hypot(second.x - first.x, second.y - first.y);
        trajectory.segment_lengths_.push_back(length);
        total += static_cast<long double>(length);
    }
    trajectory.total_distance_ = static_cast<double>(total);
    trajectory.duration_ =
        std::clamp(0.16 + trajectory.total_distance_ / 7000.0, 0.18, 0.40);
    return trajectory;
}

const Point& MotionTrajectory::start() const {
    return waypoints_.front();
}

const Point& MotionTrajectory::target() const {
    return waypoints_.back();
}

bool MotionTrajectory::complete() const {
    return elapsed_ >= duration_;
}

std::vector<Point> MotionTrajectory::remaining_waypoints() const {
    double travelled = total_distance_ * progress();
    for (std::size_t index = 0; index < segment_lengths_.size(); ++index) {
        const double length = segment_lengths_[index];
        if (travelled < length ||
            index == segment_lengths_.size() - 1) {
            return std::vector<Point>(waypoints_.begin() +
                                          static_cast<std::ptrdiff_t>(index + 1),
                                      waypoints_.end());
        }
        travelled -= length;
    }
    return {};
}

double MotionTrajectory::progress() const {
    if (duration_ <= 0) {
        return 1.0;
    }
    const double fraction =
        std::clamp(elapsed_ / duration_, 0.0, 1.0);
    return fraction * fraction * (3.0 - 2.0 * fraction);
}

Point MotionTrajectory::advance(double elapsed) {
    if (!std::isfinite(elapsed) || elapsed <= 0 || complete()) {
        return position();
    }
    elapsed_ = std::min(duration_, elapsed_ + std::min(elapsed, 0.05));
    return position();
}

Point MotionTrajectory::position() const {
    if (duration_ <= 0 || elapsed_ >= duration_) {
        return target();
    }
    double remaining = total_distance_ * progress();
    if (remaining <= 0) {
        return start();
    }
    for (std::size_t index = 0; index < segment_lengths_.size();
         ++index) {
        const double length = segment_lengths_[index];
        if (remaining <= length ||
            index == segment_lengths_.size() - 1) {
            double fraction = length <= 0 ? 0.0 : remaining / length;
            fraction = std::clamp(fraction, 0.0, 1.0);
            const Point& first = waypoints_[index];
            const Point& second = waypoints_[index + 1];
            return {
                static_cast<int>(python_round(
                    first.x + (second.x - first.x) * fraction)),
                static_cast<int>(python_round(
                    first.y + (second.y - first.y) * fraction))};
        }
        remaining -= length;
    }
    return target();
}

}  // namespace schiva::detail
