#pragma once

// Main-thread data bridge for the macOS UI and native backend. The UI is the
// sole writer of display geometry and picker window IDs; the backend only
// reads immutable snapshots while running on its owner thread.

#include <cstdint>
#include <cmath>
#include <utility>
#include <vector>

#include "schiva/types.hpp"

namespace schiva::detail {

inline Point cocoa_to_quartz_point(double x, double y, double primary_max_y) {
    return {static_cast<int>(std::lround(x)),
            static_cast<int>(std::lround(primary_max_y - y))};
}

inline Rect cocoa_to_quartz_rect(
    double left, double bottom, double right, double top,
    double primary_max_y) {
    return {static_cast<int>(std::lround(left)),
            static_cast<int>(std::lround(primary_max_y - top)),
            static_cast<int>(std::lround(right)),
            static_cast<int>(std::lround(primary_max_y - bottom))};
}

inline std::pair<double, double> quartz_to_cocoa_point(
    Point point, double primary_max_y) {
    return {static_cast<double>(point.x),
            primary_max_y - static_cast<double>(point.y)};
}

struct MacScreenGeometry {
    std::uint32_t display_id{};
    Rect frame;          // Quartz global, top-left-origin coordinates.
    Rect visible_frame;  // AppKit visibleFrame converted to Quartz.
};

struct MacUiSnapshot {
    bool screens_valid{};
    std::uint64_t generation{};
    std::vector<MacScreenGeometry> screens;
    std::vector<std::uint32_t> picker_window_ids;
};

void publish_macos_screens(std::vector<MacScreenGeometry> screens);
void clear_macos_screens();
void publish_macos_picker_windows(std::vector<std::uint32_t> window_ids);
MacUiSnapshot macos_ui_snapshot();

}  // namespace schiva::detail
