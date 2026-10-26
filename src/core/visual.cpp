#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace schiva::detail {
namespace {

constexpr int kMeaningfulPixelDelta = 12;
constexpr double kMeaningfulTileFraction = 0.25;
constexpr double kMinimumComparableFraction = 0.60;
constexpr double kBroadImmediateRatio = 0.50;
constexpr double kBroadSuccessiveRatio = 0.35;
constexpr double kBroadSuccessiveMaxGapSeconds = 1.5;
constexpr double kBroadLatchSeconds = 1.0;
constexpr int kContentSignificantCellDelta = 2;
constexpr int kContentStableCellDelta = 1;
constexpr double kContentChangedFraction = 0.40;
constexpr double kContentStableFraction = 0.90;
constexpr double kContentMinimumComparableFraction = 0.60;
constexpr int kContentMinimumObservations = 3;
constexpr double kContentMinimumSettleSeconds = 0.50;
constexpr double kContentMaximumFreshGapSeconds = 1.0;
constexpr double kContentMaximumCaptureGapSeconds = 1.5;
constexpr double kContentCooldownSeconds = 0.75;

struct TilePixels {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

TilePixels tile_pixels(int columns, int rows, int width, int height,
                       int index) {
    const int row = index / columns;
    const int column = index % columns;
    const int left = column * width / columns;
    const int right =
        std::max(left + 1, (column + 1) * width / columns);
    const int top = row * height / rows;
    const int bottom =
        std::max(top + 1, (row + 1) * height / rows);
    return {left, top, std::min(right, width),
            std::min(bottom, height)};
}

}  // namespace

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
    previous_pixels_.reset();
    previous_valid_.reset();
    last_valid_.reset();
    geometry_.reset();
    stable_content_.reset();
    candidate_content_.reset();
    last_content_valid_at_.reset();
    last_now_.reset();
    last_sustained_at_.reset();
    content_candidate_since_.reset();
    content_candidate_last_at_.reset();
    content_cooldown_until_ = -1.0;
    content_candidate_observations_ = 0;
    content_candidate_armed_ = false;
    broad_until_ = -1.0;
    grace_until_ = -1.0;
    previous_broad_candidate_ = false;
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

bool VisualActivityTracker::broad_active_at(double now) const {
    return valid_seen_ && last_now_.has_value() && now < broad_until_;
}

void VisualActivityTracker::clear_spatial_activity() {
    std::fill(burst_mask_.begin(), burst_mask_.end(), 0.0);
    std::fill(sustained_mask_.begin(), sustained_mask_.end(), 0.0);
    std::fill(active_streak_.begin(), active_streak_.end(), 0);
    last_sustained_at_.reset();
    grace_until_ = -1.0;
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
    const std::optional<double> previous_time = last_now_;
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
    if (!previous_.has_value() || !previous_pixels_.has_value() ||
        !previous_valid_.has_value() || valid_count == 0) {
        if (valid_count > 0) {
            stable_content_ =
                content_signature(current, valid_cells_now);
            last_content_valid_at_ = now;
        }
        candidate_content_.reset();
        content_candidate_since_.reset();
        content_candidate_last_at_.reset();
        content_candidate_observations_ = 0;
        content_candidate_armed_ = false;
        previous_ = current;
        previous_pixels_ = frame.pixels;
        previous_valid_ = valid_cells_now;
        previous_broad_candidate_ = false;
        VisualEvidence evidence;
        evidence.valid = true;
        return evidence;
    }

    const int total_cells = columns_ * rows_;
    const int minimum_comparable = std::max(
        8, static_cast<int>(std::ceil(total_cells *
                                     kMinimumComparableFraction)));
    int comparable_count = 0;
    int broad_changed_count = 0;
    std::vector<bool> changed_columns(static_cast<std::size_t>(columns_),
                                      false);
    std::vector<bool> changed_rows(static_cast<std::size_t>(rows_), false);
    for (int index = 0; index < total_cells; ++index) {
        const bool comparable =
            valid_cells_now[index] && (*previous_valid_)[index];
        if (!comparable) {
            continue;
        }
        ++comparable_count;
        const TilePixels tile =
            tile_pixels(columns_, rows_, frame.width, frame.height, index);
        int pixel_count = 0;
        int meaningful_count = 0;
        for (int y = tile.top; y < tile.bottom; ++y) {
            const std::size_t offset =
                static_cast<std::size_t>(y) * frame.width;
            for (int x = tile.left; x < tile.right; ++x) {
                ++pixel_count;
                const int previous = (*previous_pixels_)[offset + x];
                const int present = frame.pixels[offset + x];
                if (std::abs(present - previous) >=
                    kMeaningfulPixelDelta) {
                    ++meaningful_count;
                }
            }
        }
        if (pixel_count > 0 &&
            static_cast<double>(meaningful_count) / pixel_count >=
                kMeaningfulTileFraction) {
            ++broad_changed_count;
            changed_columns[static_cast<std::size_t>(index % columns_)] =
                true;
            changed_rows[static_cast<std::size_t>(index / columns_)] = true;
        }
    }

    int changed_column_count = 0;
    int changed_row_count = 0;
    if (broad_changed_count > 0) {
        changed_column_count = static_cast<int>(std::count(
            changed_columns.begin(), changed_columns.end(), true));
        changed_row_count = static_cast<int>(
            std::count(changed_rows.begin(), changed_rows.end(), true));
    }
    const bool spatial_extent =
        changed_column_count * 2 >= columns_ &&
        changed_row_count * 2 >= rows_;
    const bool adequate_coverage =
        comparable_count >= minimum_comparable;
    const bool immediate_broad =
        adequate_coverage && spatial_extent &&
        static_cast<double>(broad_changed_count) >=
            kBroadImmediateRatio * comparable_count;
    const bool successive_candidate =
        adequate_coverage && spatial_extent &&
        static_cast<double>(broad_changed_count) >=
            kBroadSuccessiveRatio * comparable_count;
    const bool successive_fresh =
        previous_broad_candidate_ && previous_time.has_value() &&
        now - *previous_time <= kBroadSuccessiveMaxGapSeconds;
    const bool successive_broad =
        successive_fresh && successive_candidate;
    const bool broad_current = immediate_broad || successive_broad;
    const bool content_transition = update_content_transition(
        now, current, valid_cells_now, broad_current);
    previous_broad_candidate_ = successive_candidate;
    if (broad_current) {
        clear_spatial_activity();
        broad_until_ = now + kBroadLatchSeconds;
    }
    const bool broad_suppression = broad_active_at(now);

    int changed_count = 0;
    if (!broad_suppression) {
        for (int index = 0; index < total_cells; ++index) {
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
    } else {
        clear_spatial_activity();
    }
    previous_ = current;
    previous_pixels_ = frame.pixels;
    previous_valid_ = valid_cells_now;

    if (broad_suppression) {
        VisualEvidence evidence;
        evidence.valid = true;
        evidence.broad = true;
        evidence.content_transition = content_transition;
        return evidence;
    }

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
    VisualEvidence evidence;
    evidence.valid = true;
    evidence.burst = burst;
    evidence.sustained = sustained;
    evidence.conflict = conflict;
    evidence.content_transition = content_transition;
    evidence.change_ratio = change_ratio;
    return evidence;
}

VisualActivityTracker::ContentSignature
VisualActivityTracker::content_signature(
    const std::vector<double>& cells,
    const std::vector<bool>& valid) const {
    ContentSignature signature;
    signature.cells.reserve(cells.size());
    signature.valid = valid;
    for (double value : cells) {
        const int level = std::clamp(
            static_cast<int>(python_round(value / 16.0)), 0, 15);
        signature.cells.push_back(static_cast<std::uint8_t>(level));
    }
    return signature;
}

bool VisualActivityTracker::update_content_transition(
    double now, const std::vector<double>& cells,
    const std::vector<bool>& valid, bool broad_current) {
    const ContentSignature current = content_signature(cells, valid);
    if (!stable_content_.has_value()) {
        stable_content_ = current;
        last_content_valid_at_ = now;
        return false;
    }
    const std::optional<double> previous_valid_at = last_content_valid_at_;
    if (previous_valid_at.has_value() &&
        now - *previous_valid_at >
            kContentMaximumCaptureGapSeconds) {
        stable_content_ = current;
        last_content_valid_at_ = now;
        candidate_content_.reset();
        content_candidate_since_.reset();
        content_candidate_last_at_.reset();
        content_candidate_observations_ = 0;
        content_candidate_armed_ = false;
        return false;
    }

    const int total_cells = columns_ * rows_;
    const int minimum_comparable = std::max(
        8, static_cast<int>(std::ceil(
               total_cells * kContentMinimumComparableFraction)));
    int stable_comparable = 0;
    int stable_changed = 0;
    std::vector<bool> changed_columns(
        static_cast<std::size_t>(columns_), false);
    std::vector<bool> changed_rows(
        static_cast<std::size_t>(rows_), false);
    for (int index = 0; index < total_cells; ++index) {
        if (!valid[index] || !stable_content_->valid[index]) {
            continue;
        }
        ++stable_comparable;
        if (std::abs(
                static_cast<int>(current.cells[index]) -
                static_cast<int>(stable_content_->cells[index])) >=
            kContentSignificantCellDelta) {
            ++stable_changed;
            changed_columns[static_cast<std::size_t>(index % columns_)] =
                true;
            changed_rows[static_cast<std::size_t>(index / columns_)] = true;
        }
    }
    const int changed_column_count = static_cast<int>(std::count(
        changed_columns.begin(), changed_columns.end(), true));
    const int changed_row_count = static_cast<int>(
        std::count(changed_rows.begin(), changed_rows.end(), true));
    const bool adequate_overlap =
        stable_comparable >= minimum_comparable;
    if (adequate_overlap) {
        last_content_valid_at_ = now;
    }
    const bool broad_extent =
        changed_column_count * 2 >= columns_ &&
        changed_row_count * 2 >= rows_;
    const bool differs_from_stable =
        adequate_overlap && broad_extent &&
        static_cast<double>(stable_changed) >=
            kContentChangedFraction * stable_comparable;

    if (broad_current) {
        candidate_content_.reset();
        content_candidate_since_.reset();
        content_candidate_last_at_.reset();
        content_candidate_observations_ = 0;
        content_candidate_armed_ = true;
        return false;
    }

    if (!differs_from_stable) {
        candidate_content_.reset();
        content_candidate_since_.reset();
        content_candidate_last_at_.reset();
        content_candidate_observations_ = 0;
        content_candidate_armed_ = false;
        return false;
    }

    if (!candidate_content_.has_value()) {
        if (!content_candidate_armed_ || now < content_cooldown_until_) {
            return false;
        }
        candidate_content_ = current;
        content_candidate_since_ = now;
        content_candidate_last_at_ = now;
        content_candidate_observations_ = 1;
        content_candidate_armed_ = false;
        return false;
    }

    const bool fresh =
        content_candidate_last_at_.has_value() &&
        now >= *content_candidate_last_at_ &&
        now - *content_candidate_last_at_ <=
            kContentMaximumFreshGapSeconds;
    int candidate_comparable = 0;
    int candidate_stable = 0;
    for (int index = 0; index < total_cells; ++index) {
        if (!valid[index] || !candidate_content_->valid[index]) {
            continue;
        }
        ++candidate_comparable;
        if (std::abs(
                static_cast<int>(current.cells[index]) -
                static_cast<int>(candidate_content_->cells[index])) <=
            kContentStableCellDelta) {
            ++candidate_stable;
        }
    }
    const bool candidate_matches =
        fresh && candidate_comparable >= minimum_comparable &&
        static_cast<double>(candidate_stable) >=
            kContentStableFraction * candidate_comparable;
    if (!candidate_matches) {
        if (now < content_cooldown_until_) {
            candidate_content_.reset();
            content_candidate_since_.reset();
            content_candidate_last_at_.reset();
            content_candidate_observations_ = 0;
            content_candidate_armed_ = true;
            return false;
        }
        candidate_content_ = current;
        content_candidate_since_ = now;
        content_candidate_last_at_ = now;
        content_candidate_observations_ = 1;
        content_candidate_armed_ = false;
        return false;
    }

    ++content_candidate_observations_;
    content_candidate_last_at_ = now;
    const bool settled =
        content_candidate_observations_ >= kContentMinimumObservations &&
        content_candidate_since_.has_value() &&
        now - *content_candidate_since_ >=
            kContentMinimumSettleSeconds;
    if (!settled) {
        return false;
    }

    stable_content_ = current;
    candidate_content_.reset();
    content_candidate_since_.reset();
    content_candidate_last_at_.reset();
    content_candidate_observations_ = 0;
    content_candidate_armed_ = false;
    content_cooldown_until_ = now + kContentCooldownSeconds;
    return true;
}

bool VisualActivityTracker::grace_active_at(double now) const {
    return !broad_active_at(now) && availability_at(now) &&
           last_sustained_at_.has_value() &&
           now < grace_until_;
}

bool VisualActivityTracker::conflict_for(const Rect& pip_rect) const {
    if (!geometry_.has_value() || !last_now_.has_value() ||
        broad_active_at(*last_now_)) {
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
    if (broad_active_at(now.value_or(last_now_.value_or(0.0)))) {
        return 0.0;
    }
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
