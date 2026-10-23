#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace schiva {

using Json = nlohmann::json;

struct Point {
    int x{};
    int y{};

    bool operator==(const Point&) const = default;
};

struct Rect {
    int left{};
    int top{};
    int right{};
    int bottom{};

    [[nodiscard]] int width() const { return right - left; }
    [[nodiscard]] int height() const { return bottom - top; }
    [[nodiscard]] Rect moved_to(int new_left, int new_top) const {
        return {new_left, new_top, new_left + width(), new_top + height()};
    }

    bool operator==(const Rect&) const = default;
};

struct WindowInfo {
    std::string window_id;
    std::string app_id;
    std::string role;
    Rect rect;
};

struct VisualFrame {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
    Rect bounds;
    std::vector<Rect> excluded;
};

struct Observation {
    Point cursor;
    bool buttons_down{};
    std::optional<WindowInfo> pip;
    std::optional<WindowInfo> foreground;
    Rect work_area;
    std::vector<WindowInfo> windows;
    std::optional<VisualFrame> visual_frame;
    std::optional<Rect> caret;
    bool input_active{};
    double input_age{};
    std::vector<Rect> interaction_exclusions;
};

struct Decision {
    std::string state;
    std::optional<Point> move_to;
    std::optional<std::string> pip_id;
    std::string reason;
    std::optional<Rect> recommendation;
    std::optional<std::string> context_key;
    int learning_count{};
};

}  // namespace schiva
