#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace schiva::detail {

ActivityMap::ActivityMap(int columns, int rows, double half_life,
                         std::vector<double> values)
    : columns_(columns),
      rows_(rows),
      half_life_(half_life),
      values_(std::move(values)) {
    if (columns_ <= 0 || rows_ <= 0) {
        throw std::invalid_argument(
            "activity map dimensions must be positive");
    }
    if (!std::isfinite(half_life_) || half_life_ <= 0) {
        throw std::invalid_argument(
            "activity map half-life must be positive");
    }
    const std::size_t size =
        static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_);
    if (values_.empty()) {
        values_.assign(size, 0.0);
    }
    if (values_.size() != size) {
        throw std::invalid_argument("invalid activity map values");
    }
    for (double value : values_) {
        if (!std::isfinite(value) || value < 0) {
            throw std::invalid_argument("invalid activity map values");
        }
    }
}

void ActivityMap::decay(double elapsed) {
    if (elapsed <= 0) {
        return;
    }
    const double factor =
        std::exp(-std::log(2.0) * elapsed / half_life_);
    for (double& value : values_) {
        const double decayed = value * factor;
        value = decayed < 1e-7 ? 0.0 : decayed;
    }
}

std::optional<std::pair<int, int>> ActivityMap::cell(
    double x, double y, const Rect& reference) const {
    if (reference.width() <= 0 || reference.height() <= 0) {
        return std::nullopt;
    }
    if (!(reference.left <= x && x <= reference.right &&
          reference.top <= y && y <= reference.bottom)) {
        return std::nullopt;
    }
    int column = static_cast<int>(
        (x - reference.left) * columns_ / reference.width());
    int row = static_cast<int>(
        (y - reference.top) * rows_ / reference.height());
    column = std::clamp(column, 0, columns_ - 1);
    row = std::clamp(row, 0, rows_ - 1);
    return std::pair<int, int>{row, column};
}

void ActivityMap::add(double x, double y, const Rect& reference,
                      double weight) {
    if (!std::isfinite(weight) || weight <= 0) {
        return;
    }
    const auto location = cell(x, y, reference);
    if (!location.has_value()) {
        return;
    }
    const auto [row, column] = *location;
    double& value =
        values_[static_cast<std::size_t>(row) * columns_ + column];
    value = std::min(1000.0, value + weight);
}

double ActivityMap::density_for_rect(const Rect& target,
                                     const Rect& reference) const {
    if (reference.width() <= 0 || reference.height() <= 0 ||
        target.width() <= 0 || target.height() <= 0) {
        return 0.0;
    }
    double total = 0.0;
    double covered = 0.0;
    const double cell_width =
        static_cast<double>(reference.width()) / columns_;
    const double cell_height =
        static_cast<double>(reference.height()) / rows_;
    const int left = std::max(target.left, reference.left);
    const int right = std::min(target.right, reference.right);
    const int top = std::max(target.top, reference.top);
    const int bottom = std::min(target.bottom, reference.bottom);
    if (right <= left || bottom <= top) {
        return 0.0;
    }

    const int first_column = std::clamp(
        static_cast<int>(std::floor((left - reference.left) * columns_ /
                                    static_cast<double>(reference.width()))),
        0, columns_ - 1);
    const int last_column = std::clamp(
        static_cast<int>(std::ceil((right - reference.left) * columns_ /
                                   static_cast<double>(reference.width()))) -
            1,
        0, columns_ - 1);
    const int first_row = std::clamp(
        static_cast<int>(std::floor((top - reference.top) * rows_ /
                                    static_cast<double>(reference.height()))),
        0, rows_ - 1);
    const int last_row = std::clamp(
        static_cast<int>(std::ceil((bottom - reference.top) * rows_ /
                                   static_cast<double>(reference.height()))) -
            1,
        0, rows_ - 1);

    for (int row = first_row; row <= last_row; ++row) {
        const double cell_top = reference.top + row * cell_height;
        const double cell_bottom =
            reference.top + (row + 1) * cell_height;
        for (int column = first_column; column <= last_column; ++column) {
            const double cell_left = reference.left + column * cell_width;
            const double cell_right =
                reference.left + (column + 1) * cell_width;
            const double overlap_width = std::max(
                0.0, std::min(cell_right, static_cast<double>(target.right)) -
                         std::max(cell_left, static_cast<double>(target.left)));
            const double overlap_height = std::max(
                0.0,
                std::min(cell_bottom, static_cast<double>(target.bottom)) -
                    std::max(cell_top, static_cast<double>(target.top)));
            const double overlap = overlap_width * overlap_height;
            if (overlap <= 0) {
                continue;
            }
            covered += overlap;
            total += values_[static_cast<std::size_t>(row) * columns_ +
                             column] *
                     overlap;
        }
    }
    return covered <= 0 ? 0.0 : total / covered;
}

Json ActivityMap::as_sparse() const {
    Json result = Json::object();
    for (std::size_t index = 0; index < values_.size(); ++index) {
        const double value = values_[index];
        if (value > 1e-7) {
            result[std::to_string(index)] =
                static_cast<double>(python_round(value * 1e6)) / 1e6;
        }
    }
    return result;
}

ActivityMap ActivityMap::from_sparse(const Json& sparse, double half_life,
                                     int columns, int rows) {
    if (columns <= 0 || rows <= 0) {
        throw std::invalid_argument(
            "activity map dimensions must be positive");
    }
    std::vector<double> values(
        static_cast<std::size_t>(columns) * rows, 0.0);
    if (sparse.is_object()) {
        for (auto iterator = sparse.begin(); iterator != sparse.end();
             ++iterator) {
            try {
                std::size_t consumed = 0;
                const std::string key = iterator.key();
                const long long raw_index =
                    std::stoll(key, &consumed, 10);
                if (consumed != key.size()) {
                    continue;
                }
                const double value = iterator.value().get<double>();
                if (raw_index >= 0 &&
                    raw_index < static_cast<long long>(values.size()) &&
                    std::isfinite(value) && value >= 0 &&
                    value <= 1000) {
                    values[static_cast<std::size_t>(raw_index)] = value;
                }
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    return ActivityMap(columns, rows, half_life, std::move(values));
}

void InteractionTracker::set_state(ActivityState state) {
    state_ = std::move(state);
}

void InteractionTracker::reset_presence() {
    last_cursor_.reset();
    last_buttons_ = false;
    last_reference_.reset();
    had_activity_ = false;
}

void InteractionTracker::observe(double now,
                                 const Observation& observation) {
    had_activity_ = false;
    const double elapsed = last_now_.has_value()
                               ? std::max(0.0, now - *last_now_)
                               : 0.0;
    last_now_ = now;
    state_.short_map.decay(elapsed);
    state_.long_map.decay(elapsed);
    state_.manual_map.decay(elapsed);

    const Point& cursor = observation.cursor;
    const bool inside_pip =
        observation.pip.has_value() &&
        observation.pip->rect.left <= cursor.x &&
        cursor.x < observation.pip->rect.right &&
        observation.pip->rect.top <= cursor.y &&
        cursor.y < observation.pip->rect.bottom;
    bool inside_helper = false;
    for (const Rect& rect : observation.interaction_exclusions) {
        if (rect.left <= cursor.x && cursor.x < rect.right &&
            rect.top <= cursor.y && cursor.y < rect.bottom) {
            inside_helper = true;
            break;
        }
    }
    if (!observation.pip.has_value() || inside_pip || inside_helper) {
        last_cursor_ = cursor;
        last_buttons_ = observation.buttons_down;
        return;
    }

    const Rect reference = observation.foreground.has_value()
                               ? observation.foreground->rect
                               : observation.work_area;
    if (reference.width() <= 0 || reference.height() <= 0) {
        return;
    }
    if (last_reference_.has_value() && !(*last_reference_ == reference)) {
        last_cursor_ = cursor;
        last_buttons_ = observation.buttons_down;
        last_reference_ = reference;
        return;
    }
    last_reference_ = reference;

    const double dwell_weight = std::min(std::max(elapsed, 0.0), 1.0);
    if (dwell_weight > 0) {
        state_.short_map.add(cursor.x, cursor.y, reference, dwell_weight);
        state_.long_map.add(cursor.x, cursor.y, reference, dwell_weight);
        had_activity_ = true;
    }

    const bool moved =
        last_cursor_.has_value() && !(cursor == *last_cursor_);
    const bool clicked =
        observation.buttons_down && !last_buttons_;
    if (moved) {
        state_.short_map.add(cursor.x, cursor.y, reference, 0.25);
        state_.long_map.add(cursor.x, cursor.y, reference, 0.10);
        had_activity_ = true;
    }
    if (clicked) {
        state_.short_map.add(cursor.x, cursor.y, reference, 2.0);
        state_.long_map.add(cursor.x, cursor.y, reference, 0.75);
        had_activity_ = true;
    }
    if (observation.input_active && elapsed > 0) {
        const double input_weight = std::min(elapsed, 1.0) * 0.75;
        state_.short_map.add(cursor.x, cursor.y, reference, input_weight);
        state_.long_map.add(cursor.x, cursor.y, reference, input_weight);
        had_activity_ = true;
    }
    if (observation.caret.has_value()) {
        state_.short_map.add(
            observation.caret->left + observation.caret->width() / 2.0,
            observation.caret->top + observation.caret->height() / 2.0,
            reference, 0.75);
        state_.long_map.add(
            observation.caret->left + observation.caret->width() / 2.0,
            observation.caret->top + observation.caret->height() / 2.0,
            reference, 0.30);
    }

    last_cursor_ = cursor;
    last_buttons_ = observation.buttons_down;
}

void InteractionTracker::add_manual_preference(const Rect& target,
                                               const Rect& reference) {
    if (reference.width() <= 0 || reference.height() <= 0) {
        return;
    }
    state_.manual_map.add(
        target.left + target.width() / 2.0,
        target.top + target.height() / 2.0, reference, 1.0);
    had_activity_ = true;
}

double InteractionTracker::risk_for(const Rect& target,
                                    const Rect& reference) const {
    const double short_risk =
        state_.short_map.density_for_rect(target, reference);
    const double long_risk =
        state_.long_map.density_for_rect(target, reference);
    return std::min(
        1.0,
        1.0 - std::exp(-(0.45 * short_risk + 0.20 * long_risk)));
}

double InteractionTracker::manual_preference_for(
    const Rect& target, const Rect& reference) const {
    const double value =
        state_.manual_map.density_for_rect(target, reference);
    return std::min(1.0, 1.0 - std::exp(-value));
}

}  // namespace schiva::detail
