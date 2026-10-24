#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "schiva/core.hpp"

namespace schiva::heatmap {

constexpr std::size_t kMaxGridDimension = 128;
constexpr std::size_t kMaxDetailChars = 260;
constexpr auto kUnknownColor = "#394452";

struct LayerSpec {
    std::string_view key;
    std::string_view label;
    std::string_view meaning;
    std::string_view high_color;
};

struct NormalizedRect {
    double left{};
    double top{};
    double right{};
    double bottom{};
};

const LayerSpec& layer_spec(std::string_view key);
std::vector<std::string> layer_keys();
std::optional<double> clamp_unit(const Json& value);
std::pair<int, int> grid_dimensions(const Json& snapshot);
std::optional<double> layer_value(
    const Json& snapshot,
    std::string_view layer_key,
    std::size_t index);
std::string layer_color(std::string_view layer_key, const Json& value);

std::optional<NormalizedRect> normalize_rect_json(const Json& value);
std::optional<std::pair<double, double>> normalize_pip_size(const Json& value);
std::optional<std::pair<double, double>> snapshot_pip_size(const Json& snapshot);
std::optional<NormalizedRect> candidate_footprint(
    int column,
    int row,
    int columns,
    int rows,
    const Json& pip_size);

std::optional<std::string> profile_key(const Json& profile);
struct ProfileChoice {
    std::string key;
    std::string label;
};
std::vector<ProfileChoice> profile_choices(const Json& snapshot);
std::optional<std::string> resolve_profile_key(
    const Json& snapshot,
    std::optional<std::string> current);
bool bridge_data_matches_selection(
    const Json& data,
    const std::optional<std::string>& selected_profile);

std::string learning_summary(const Json& snapshot);
std::optional<std::string> projection_notice(const Json& snapshot);
std::string profile_status_details(const Json& snapshot);
std::optional<std::string> cold_start_message(const Json& snapshot);
std::string bounded_detail(const Json& value);

}  // namespace schiva::heatmap
