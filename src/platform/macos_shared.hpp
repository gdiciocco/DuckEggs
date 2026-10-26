#pragma once

// Main-thread data bridge for the macOS UI and native backend. The UI is the
// sole writer of display geometry and picker window IDs; the backend only
// reads immutable snapshots while running on its owner thread.

#include <cstdint>
#include <vector>

#include "schiva/types.hpp"

namespace schiva::detail {

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
