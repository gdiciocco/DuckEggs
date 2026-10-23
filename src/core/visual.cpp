#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace schiva::detail {

VisualActivityTracker::VisualActivityTracker(int columns, int rows)
    : columns_(columns), rows_(rows) {
    if (columns_ <= 0 || rows_ <= 0) {
        throw std::invalid_argument(
            "visual tracker dimensions must be positive");
    }
    reset();
}

void VisualActivityTracker::reset() {
    const std::size_t size =
        static_cast<std::size_t>(columns_) * rows_;
    burst_mask_.assign(size, 0.0);
    sustained_mask_.assign(size, 0.0);
    active_streak_.assign(size, 0);
    previous_.reset();
    previous_valid_.reset();
    last_valid_.reset();
    geometry_.reset();
    last_now_.reset();
    last_sustained_at_.reset();
    grace_until_ = -1.0;
    valid_seen_ = false;
}

std::vector<double> VisualActivityTracker::cell_means(
    const VisualFrame& frame) const {
    std::vector<double> means;
    means.reserve(static_cast<std::size_t>(columns_) * rows_);
    for (int row = 0; row < rows_; ++row) {
        const int top = row * frame.height / rows_;
        const int bottom =
            std::max(top + 1, (row + 1) * frame.height / rows_);
        for (int column = 0; column < columns_; ++column) {
            const int left = column * frame.width / columns_;
            const int right =
                std::max(left + 1,
                         (column + 1) * frame.width / columns_);
            long long total = 0;
            int count = 0;
            for (int y = top; y < std::min(bottom, frame.height); ++y) {
                const std::size_t offset =
                    static_cast<std::size_t>(y) * frame.width;
                for (int x = left; x < std::min(right, frame.width); ++x) {
                    total += frame.pixels[offset + x];
                    ++count;
                }
            }
            means.push_back(count == 0
                                ? 0.0
                                : static_cast<double>(total) / count);
        }
    }
    return means;
}

Rect VisualActivityTracker::screen_cell(const Rect& bounds,
                                        int index) const {
    const int row = index / columns_;
    const int column = index % columns_;
    const int left =
        bounds.left + static_cast<int>(python_round(
                          static_cast<double>(bounds.width()) * column /
                          columns_));
    const int right =
        bounds.left + static_cast<int>(python_round(
                          static_cast<double>(bounds.width()) * (column + 1) /
                          columns_));
    const int top =
        bounds.top + static_cast<int>(python_round(
                         static_cast<double>(bounds.height()) * row / rows_));
    const int bottom =
        bounds.top +
        static_cast<int>(python_round(static_cast<double>(bounds.height()) *
                                      (row + 1) / rows_));
    return {left, top, std::max(left + 1, right),
            std::max(top + 1, bottom)};
}

std::vector<bool> VisualActivityTracker::valid_cells(
    const VisualFrame& frame,
    const std::optional<Rect>& pip_rect) const {
    std::vector<bool> valid;
    valid.reserve(static_cast<std::size_t>(columns_) * rows_);
    for (int index = 0; index < columns_ * rows_; ++index) {
        const Rect cell = screen_cell(frame.bounds, index);
        bool excluded = false;
        for (const Rect& rect : frame.excluded) {
            if (intersects(cell, rect)) {
                excluded = true;
                break;
            }
        }
        if (pip_rect.has_value() && intersects(cell, *pip_rect)) {
            excluded = true;
        }
        valid.push_back(!excluded);
    }
    return valid;
}

bool VisualActivityTracker::availability_at(double now,
                                            double max_age) const {
    return valid_seen_ && last_now_.has_value() &&
           now - *last_now_ <= max_age;
}

VisualEvidence VisualActivityTracker::observe(
    double now, const VisualFrame& frame,
    const std::optional<Rect>& pip_rect,
    const std::optional<std::string>& context_key) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.pixels.size() !=
            static_cast<std::size_t>(frame.width) * frame.height) {
        throw std::invalid_argument("invalid visual frame");
    }
    const Geometry geometry{frame.width, frame.height, frame.bounds,
                            context_key};
    if (geometry_.has_value() && !(*geometry_ == geometry)) {
        reset();
    }
    const double elapsed = last_now_.has_value()
                               ? std::max(0.0, now - *last_now_)
                               : 0.0;
    last_now_ = now;
    geometry_ = geometry;
    valid_seen_ = true;

    const double burst_decay =
        elapsed == 0.0 ? 1.0 : std::exp(-elapsed / 1.2);
    const double sustained_decay =
        elapsed == 0.0 ? 1.0 : std::exp(-elapsed / 3.0);
    for (double& value : burst_mask_) {
        value *= burst_decay;
        if (value < 1e-4) {
            value = 0.0;
        }
    }
    for (double& value : sustained_mask_) {
        value *= sustained_decay;
        if (value < 1e-4) {
            value = 0.0;
        }
    }

    const std::vector<double> current = cell_means(frame);
    const std::vector<bool> valid_cells_now = valid_cells(frame, pip_rect);
    last_valid_ = valid_cells_now;
    const int valid_count = static_cast<int>(
        std::count(valid_cells_now.begin(), valid_cells_now.end(), true));
    if (!previous_.has_value() || !previous_valid_.has_value() ||
        valid_count == 0) {
        previous_ = current;
        previous_valid_ = valid_cells_now;
        return {true, false, false, false, 0.0};
    }

    int changed_count = 0;
    for (int index = 0; index < columns_ * rows_; ++index) {
        const bool comparable =
            valid_cells_now[index] && (*previous_valid_)[index];
        const bool changed =
            comparable &&
            std::abs(current[index] - (*previous_)[index]) >= 12.0;
        if (changed) {
            ++changed_count;
            burst_mask_[index] = 1.0;
            ++active_streak_[index];
            if (active_streak_[index] >= 2) {
                sustained_mask_[index] = 1.0;
            }
        } else if (comparable) {
            active_streak_[index] = 0;
        } else {
            active_streak_[index] = 0;
        }
    }
    previous_ = current;
    previous_valid_ = valid_cells_now;

    const bool burst = changed_count > 0;
    int sustained_cells = 0;
    for (int index = 0; index < columns_ * rows_; ++index) {
        if (sustained_mask_[index] > 0.5 &&
            (*last_valid_)[index]) {
            ++sustained_cells;
        }
    }
    const bool sustained = sustained_cells >= 2;
    if (sustained) {
        last_sustained_at_ = now;
        grace_until_ = std::max(grace_until_, now + 2.5);
    }

    bool conflict = false;
    if (pip_rect.has_value()) {
        const Rect near_pip =
            expanded(*pip_rect,
                     std::max(8, frame.bounds.width() / columns_));
        for (int index = 0; index < columns_ * rows_; ++index) {
            if (sustained_mask_[index] > 0.5 &&
                (*last_valid_)[index] &&
                intersects(screen_cell(frame.bounds, index), near_pip)) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            grace_until_ = std::max(grace_until_, now + 2.5);
        }
    }
    const double change_ratio =
        valid_count == 0
            ? 0.0
            : static_cast<double>(changed_count) / valid_count;
    return {true, burst, sustained, conflict, change_ratio};
}

bool VisualActivityTracker::grace_active_at(double now) const {
    return availability_at(now) && last_sustained_at_.has_value() &&
           now < grace_until_;
}

bool VisualActivityTracker::conflict_for(const Rect& pip_rect) const {
    if (!geometry_.has_value()) {
        return false;
    }
    const Rect& frame_bounds = geometry_->bounds;
    const Rect near_pip =
        expanded(pip_rect,
                 std::max(8, frame_bounds.width() / columns_));
    for (int index = 0; index < columns_ * rows_; ++index) {
        if (sustained_mask_[index] > 0.5 &&
            (!last_valid_.has_value() || (*last_valid_)[index]) &&
            intersects(screen_cell(frame_bounds, index), near_pip)) {
            return true;
        }
    }
    return false;
}

double VisualActivityTracker::risk_for(
    const Rect& target, const std::optional<VisualFrame>& frame,
    std::optional<double> now) const {
    if (now.has_value() && !availability_at(*now)) {
        return 0.0;
    }
    Rect bounds;
    if (frame.has_value()) {
        bounds = frame->bounds;
    } else if (geometry_.has_value()) {
        bounds = geometry_->bounds;
    } else {
        return 0.0;
    }
    if (bounds.width() <= 0 || bounds.height() <= 0) {
        return 0.0;
    }
    const int left = std::max(target.left, bounds.left);
    const int right = std::min(target.right, bounds.right);
    const int top = std::max(target.top, bounds.top);
    const int bottom = std::min(target.bottom, bounds.bottom);
    if (right <= left || bottom <= top) {
        return 0.0;
    }
    const int first_column = std::clamp(
        static_cast<int>(std::floor(
            (left - bounds.left) * columns_ /
            static_cast<double>(bounds.width()))),
        0, columns_ - 1);
    const int last_column = std::clamp(
        static_cast<int>(std::ceil(
            (right - bounds.left) * columns_ /
            static_cast<double>(bounds.width()))) -
            1,
        0, columns_ - 1);
    const int first_row = std::clamp(
        static_cast<int>(std::floor(
            (top - bounds.top) * rows_ /
            static_cast<double>(bounds.height()))),
        0, rows_ - 1);
    const int last_row = std::clamp(
        static_cast<int>(std::ceil(
            (bottom - bounds.top) * rows_ /
            static_cast<double>(bounds.height()))) -
            1,
        0, rows_ - 1);

    double total = 0.0;
    double area = 0.0;
    for (int row = first_row; row <= last_row; ++row) {
        for (int column = first_column; column <= last_column; ++column) {
            const int index = row * columns_ + column;
            if (last_valid_.has_value() && !(*last_valid_)[index]) {
                continue;
            }
            const Rect cell = screen_cell(bounds, index);
            const int overlap_width =
                std::max(0, std::min(cell.right, target.right) -
                                std::max(cell.left, target.left));
            const int overlap_height =
                std::max(0, std::min(cell.bottom, target.bottom) -
                                std::max(cell.top, target.top));
            const int overlap = overlap_width * overlap_height;
            if (overlap > 0) {
                total += overlap *
                         (0.25 * burst_mask_[index] +
                          0.75 * sustained_mask_[index]);
                area += overlap;
            }
        }
    }
    if (area == 0.0) {
        return 0.0;
    }
    double raw_risk = total / area;
    if (now.has_value() && last_now_.has_value()) {
        raw_risk *= std::exp(-std::max(0.0, *now - *last_now_) / 3.0);
    }
    return std::clamp(raw_risk, 0.0, 1.0);
}

}  // namespace schiva::detail
