#include "schiva/heatmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace schiva::heatmap {
namespace {

constexpr std::array<LayerSpec, 3> kLayers{{
    {
        "activity",
        "Activity",
        "Warmer cells are used more often, so they are less suitable "
        "for Picture-in-Picture. This is observed activity, not an image.",
        "#f08a65",
    },
    {
        "manual",
        "Manual preference",
        "Warmer cells reflect placements you chose or corrected directly. "
        "This is separate from the shared model reward.",
        "#73a7ff",
    },
    {
        "placement",
        "Placement",
        "Warmer cells are relatively better destinations for the complete "
        "PiP footprint. This is an estimate, not a probability.",
        "#73dcc5",
    },
}};

std::optional<double> finite_number(const Json& value) {
    if (!value.is_number() || value.is_boolean()) {
        return std::nullopt;
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

std::string string_value(const Json& value, std::string fallback = {}) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return fallback;
}

std::optional<std::int64_t> integer_value(const Json& value) {
    const auto number = finite_number(value);
    if (!number.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*number);
}

std::array<int, 3> rgb(std::string_view color) {
    auto parse = [](std::string_view part) {
        int value = 0;
        for (const char character : part) {
            value *= 16;
            if (character >= '0' && character <= '9') {
                value += character - '0';
            } else if (character >= 'a' && character <= 'f') {
                value += character - 'a' + 10;
            } else if (character >= 'A' && character <= 'F') {
                value += character - 'A' + 10;
            }
        }
        return value;
    };
    if (color.size() == 7 && color.front() == '#') {
        return {
            parse(color.substr(1, 2)),
            parse(color.substr(3, 2)),
            parse(color.substr(5, 2))};
    }
    return {0, 0, 0};
}

std::string hex_color(const std::array<int, 3>& channels) {
    std::ostringstream stream;
    stream << '#';
    stream << std::hex << std::setfill('0');
    for (const int channel : channels) {
        stream << std::setw(2) << std::clamp(channel, 0, 255);
    }
    return stream.str();
}

std::string display_count(const Json& value) {
    const auto number = finite_number(value);
    if (!number.has_value() || *number < 0.0) {
        return "n/d";
    }
    return std::to_string(static_cast<std::int64_t>(*number));
}

}  // namespace

const LayerSpec& layer_spec(std::string_view key) {
    for (const auto& spec : kLayers) {
        if (spec.key == key) {
            return spec;
        }
    }
    return kLayers[0];
}

std::vector<std::string> layer_keys() {
    return {"activity", "manual", "placement"};
}

std::optional<double> clamp_unit(const Json& value) {
    const auto number = finite_number(value);
    if (!number.has_value()) {
        return std::nullopt;
    }
    return std::clamp(*number, 0.0, 1.0);
}

std::pair<int, int> grid_dimensions(const Json& snapshot) {
    const auto columns = snapshot.contains("columns")
        ? finite_number(snapshot["columns"])
        : std::nullopt;
    const auto rows = snapshot.contains("rows")
        ? finite_number(snapshot["rows"])
        : std::nullopt;
    if (!columns.has_value() || !rows.has_value()) {
        return {24, 14};
    }
    return {
        std::clamp(static_cast<int>(*columns), 1,
                   static_cast<int>(kMaxGridDimension)),
        std::clamp(static_cast<int>(*rows), 1,
                   static_cast<int>(kMaxGridDimension))};
}

std::optional<double> layer_value(
    const Json& snapshot,
    std::string_view layer_key,
    std::size_t index) {
    if (!snapshot.contains("layers") ||
        !snapshot["layers"].is_object()) {
        return std::nullopt;
    }
    const Json& layers = snapshot["layers"];
    const std::string key(layer_key);
    if (!layers.contains(key) || !layers[key].is_array() ||
        index >= layers[key].size()) {
        return std::nullopt;
    }
    return clamp_unit(layers[key][index]);
}

std::string layer_color(
    std::string_view layer_key,
    const Json& value) {
    const auto normalized = clamp_unit(value);
    bool known = false;
    const LayerSpec* spec = nullptr;
    for (const auto& candidate : kLayers) {
        if (candidate.key == layer_key) {
            spec = &candidate;
            known = true;
            break;
        }
    }
    if (!normalized.has_value() || !known || spec == nullptr) {
        return std::string(kUnknownColor);
    }
    const auto low = rgb("#26313d");
    const auto high = rgb(spec->high_color);
    std::array<int, 3> channels{};
    for (std::size_t index = 0; index < channels.size(); ++index) {
        channels[index] = static_cast<int>(std::lround(
            low[index] + (high[index] - low[index]) * *normalized));
    }
    return hex_color(channels);
}

std::optional<NormalizedRect> normalize_rect_json(const Json& value) {
    if (!value.is_array() || value.size() != 4U) {
        return std::nullopt;
    }
    std::array<double, 4> values{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto number = finite_number(value[index]);
        if (!number.has_value()) {
            return std::nullopt;
        }
        values[index] = std::clamp(*number, 0.0, 1.0);
    }
    if (values[0] > values[2]) {
        std::swap(values[0], values[2]);
    }
    if (values[1] > values[3]) {
        std::swap(values[1], values[3]);
    }
    if (values[2] <= values[0] || values[3] <= values[1]) {
        return std::nullopt;
    }
    return NormalizedRect{
        values[0], values[1], values[2], values[3]};
}

std::optional<std::pair<double, double>> normalize_pip_size(
    const Json& value) {
    if (!value.is_array() || value.size() != 2U) {
        return std::nullopt;
    }
    const auto width = clamp_unit(value[0]);
    const auto height = clamp_unit(value[1]);
    if (!width.has_value() || !height.has_value() ||
        *width <= 0.0 || *height <= 0.0) {
        return std::nullopt;
    }
    return std::pair<double, double>{*width, *height};
}

std::optional<std::pair<double, double>> snapshot_pip_size(
    const Json& snapshot) {
    if (snapshot.contains("pip_size") &&
        snapshot["pip_size"].is_array() &&
        snapshot["pip_size"].size() == 2U) {
        const auto width = finite_number(snapshot["pip_size"][0]);
        const auto height = finite_number(snapshot["pip_size"][1]);
        if (width.has_value() && height.has_value() &&
            *width > 0.0 && *width <= 1.0 &&
            *height > 0.0 && *height <= 1.0) {
            return std::pair<double, double>{*width, *height};
        }
    }
    if (!snapshot.contains("pip_rect")) {
        return std::nullopt;
    }
    const auto rect = normalize_rect_json(snapshot["pip_rect"]);
    if (!rect.has_value()) {
        return std::nullopt;
    }
    return std::pair<double, double>{
        rect->right - rect->left,
        rect->bottom - rect->top};
}

std::optional<NormalizedRect> candidate_footprint(
    int column,
    int row,
    int columns,
    int rows,
    const Json& pip_size) {
    const auto size = normalize_pip_size(pip_size);
    if (!size.has_value() || columns <= 0 || rows <= 0) {
        return std::nullopt;
    }
    column = std::clamp(column, 0, columns - 1);
    row = std::clamp(row, 0, rows - 1);
    const double center_x =
        (static_cast<double>(column) + 0.5) / columns;
    const double center_y =
        (static_cast<double>(row) + 0.5) / rows;
    return NormalizedRect{
        center_x - size->first / 2.0,
        center_y - size->second / 2.0,
        center_x + size->first / 2.0,
        center_y + size->second / 2.0};
}

std::optional<std::string> profile_key(const Json& profile) {
    if (!profile.is_object() || !profile.contains("profile_key") ||
        !profile["profile_key"].is_string()) {
        return std::nullopt;
    }
    std::string value = profile["profile_key"].get<std::string>();
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::vector<ProfileChoice> profile_choices(const Json& snapshot) {
    if (!snapshot.contains("profiles") ||
        !snapshot["profiles"].is_array()) {
        return {};
    }
    const std::string active =
        snapshot.contains("active_profile") &&
                snapshot["active_profile"].is_string()
            ? snapshot["active_profile"].get<std::string>()
            : "";
    std::vector<ProfileChoice> choices;
    for (const Json& profile : snapshot["profiles"]) {
        const auto key = profile_key(profile);
        if (!key.has_value()) {
            continue;
        }
        const std::string app = profile.contains("app_id")
            ? string_value(profile["app_id"], "Unknown app")
            : "Unknown app";
        const std::string layout = profile.contains("layout_bin")
            ? string_value(profile["layout_bin"], "Unknown layout")
            : "Unknown layout";
        choices.push_back(ProfileChoice{
            *key,
            app + " \u00b7 " + layout + " \u00b7 " +
                (*key == active ? "Active" : "Learned")});
    }
    return choices;
}

std::optional<std::string> resolve_profile_key(
    const Json& snapshot,
    std::optional<std::string> current) {
    if (current.has_value()) {
        return current;
    }
    const auto choices = profile_choices(snapshot);
    auto exists = [&choices](const std::string& key) {
        return std::any_of(
            choices.begin(),
            choices.end(),
            [&key](const ProfileChoice& choice) {
                return choice.key == key;
            });
    };
    if (snapshot.contains("selected_profile") &&
        snapshot["selected_profile"].is_string()) {
        const std::string selected =
            snapshot["selected_profile"].get<std::string>();
        if (exists(selected)) {
            return selected;
        }
    }
    if (snapshot.contains("active_profile") &&
        snapshot["active_profile"].is_string()) {
        const std::string active =
            snapshot["active_profile"].get<std::string>();
        if (exists(active)) {
            return active;
        }
    }
    if (!choices.empty()) {
        return choices.front().key;
    }
    return std::nullopt;
}

bool bridge_data_matches_selection(
    const Json& data,
    const std::optional<std::string>& selected_profile) {
    if (!selected_profile.has_value()) {
        return true;
    }
    return data.contains("selected_profile") &&
           data["selected_profile"].is_string() &&
           data["selected_profile"].get<std::string>() ==
               *selected_profile;
}

std::string learning_summary(const Json& snapshot) {
    std::optional<Json> decision_count;
    const std::optional<std::string> selected =
        snapshot.contains("selected_profile") &&
                snapshot["selected_profile"].is_string()
            ? std::optional<std::string>(
                  snapshot["selected_profile"].get<std::string>())
            : std::nullopt;
    if (selected.has_value() && snapshot.contains("profiles") &&
        snapshot["profiles"].is_array()) {
        for (const Json& profile : snapshot["profiles"]) {
            const auto key = profile_key(profile);
            if (key.has_value() && *key == *selected &&
                profile.contains("decision_count")) {
                decision_count = profile["decision_count"];
                break;
            }
        }
    }
    if (!decision_count.has_value() && snapshot.contains("decision_count")) {
        decision_count = snapshot["decision_count"];
    }
    // Kept for compatibility with the frozen platform test surface. DuckEggs
    // renderers format this summary in English themselves.
    return "Decisioni profilo: " +
           display_count(decision_count.value_or(Json(nullptr))) +
           " \u00b7 apprendimenti globali: " +
           display_count(
               snapshot.contains("learning_count")
                   ? snapshot["learning_count"]
                   : Json(nullptr));
}

std::optional<std::string> projection_notice(const Json& snapshot) {
    if (!snapshot.contains("projection") ||
        !snapshot["projection"].is_boolean() ||
        !snapshot["projection"].get<bool>()) {
        return std::nullopt;
    }
    return "Projected maps \u2014 the application is inactive, so its activity "
           "is extended to the whole workspace. The shared model and current "
           "geometry are used, but this is not live data.";
}

std::string profile_status_details(const Json& snapshot) {
    const std::optional<std::string> selected =
        snapshot.contains("selected_profile") &&
                snapshot["selected_profile"].is_string()
            ? std::optional<std::string>(
                  snapshot["selected_profile"].get<std::string>())
            : std::nullopt;
    if (selected.has_value() && snapshot.contains("profiles") &&
        snapshot["profiles"].is_array()) {
        for (const Json& profile : snapshot["profiles"]) {
            const auto key = profile_key(profile);
            if (!key.has_value() || *key != *selected) {
                continue;
            }
            return "Profile: " +
                   string_value(
                       profile.contains("app_id")
                           ? profile["app_id"]
                           : Json(nullptr),
                       "Unknown app") +
                   " \u00b7 " +
                   string_value(
                       profile.contains("role")
                           ? profile["role"]
                           : Json(nullptr),
                       "Unknown window") +
                   " \u00b7 " +
                   string_value(
                       profile.contains("layout_bin")
                           ? profile["layout_bin"]
                           : Json(nullptr),
                       "Unknown layout") +
                   " \u00b7 key: " + *selected + ".";
        }
    }
    if (selected.has_value() && !selected->empty()) {
        return "Profile: key " + *selected +
               ", details are not available.";
    }
    return "The selected profile is not available.";
}

std::optional<std::string> cold_start_message(const Json& snapshot) {
    if (profile_choices(snapshot).empty()) {
        return "No learning maps are available yet. Keep DuckEggs running "
               "while you use Picture-in-Picture, then reopen this view.";
    }
    std::int64_t learning_count = 0;
    if (snapshot.contains("learning_count")) {
        learning_count =
            integer_value(snapshot["learning_count"]).value_or(0);
    }
    if (learning_count <= 0) {
        return "Cold start: the model has no examples yet. Gray cells have "
               "not been learned and do not indicate areas to avoid.";
    }
    if (snapshot.contains("layers") && snapshot["layers"].is_object()) {
        for (const auto& key : layer_keys()) {
            if (!snapshot["layers"].contains(key) ||
                !snapshot["layers"][key].is_array()) {
                continue;
            }
            for (const Json& value : snapshot["layers"][key]) {
                if (clamp_unit(value).has_value()) {
                    return std::nullopt;
                }
            }
        }
    }
    return "Not enough data yet: gray areas were not observed reliably.";
}

std::string bounded_detail(const Json& value) {
    std::string clean;
    if (value.is_string()) {
        const std::string source = value.get<std::string>();
        bool space = false;
        for (const char character : source) {
            const bool whitespace =
                character == ' ' || character == '\n' ||
                character == '\r' || character == '\t';
            if (whitespace) {
                if (!clean.empty()) {
                    space = true;
                }
            } else {
                if (space) {
                    clean.push_back(' ');
                    space = false;
                }
                clean.push_back(character);
            }
        }
    }
    if (clean.size() <= kMaxDetailChars) {
        return clean;
    }
    clean.resize(kMaxDetailChars - 3);
    while (!clean.empty() && (clean.back() & 0xC0) == 0x80) {
        clean.pop_back();
    }
    clean += "\xe2\x80\xa6";
    return clean;
}

}  // namespace schiva::heatmap
