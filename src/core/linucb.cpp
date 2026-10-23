#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace schiva::detail {
namespace {

std::vector<std::vector<double>> identity(int dimension) {
    if (dimension <= 0) {
        return {};
    }
    std::vector<std::vector<double>> result(
        dimension, std::vector<double>(dimension, 0.0));
    for (int index = 0; index < dimension; ++index) {
        result[index][index] = 1.0;
    }
    return result;
}

}  // namespace

LinUCBModel::LinUCBModel(int dimension, double alpha)
    : dimension_(dimension),
      alpha_(alpha),
      inverse_(identity(dimension)),
      b_(static_cast<std::size_t>(std::max(dimension, 0)), 0.0) {
    if (dimension < 1 || dimension > 16) {
        throw std::invalid_argument(
            "LinUCB dimension must be between 1 and 16");
    }
    set_alpha(alpha);
}

LinUCBModel::LinUCBModel(
    int dimension, double alpha,
    const std::vector<std::vector<double>>& inverse,
    const std::vector<double>& b, int updates)
    : dimension_(dimension),
      alpha_(alpha),
      inverse_(inverse),
      b_(b),
      updates_(updates) {
    if (dimension < 1 || dimension > 16) {
        throw std::invalid_argument(
            "LinUCB dimension must be between 1 and 16");
    }
    set_alpha(alpha);
    validate_state();
}

void LinUCBModel::set_alpha(double alpha) {
    if (!std::isfinite(alpha) || alpha < 0) {
        throw std::invalid_argument(
            "LinUCB alpha must be finite and non-negative");
    }
    alpha_ = alpha;
}

void LinUCBModel::validate_state() const {
    if (static_cast<int>(inverse_.size()) != dimension_) {
        throw std::invalid_argument(
            "LinUCB inverse matrix has the wrong shape");
    }
    for (const auto& row : inverse_) {
        if (static_cast<int>(row.size()) != dimension_) {
            throw std::invalid_argument(
                "LinUCB inverse matrix has the wrong shape");
        }
    }
    if (static_cast<int>(b_.size()) != dimension_) {
        throw std::invalid_argument("LinUCB b vector has the wrong shape");
    }
    for (const auto& row : inverse_) {
        for (double value : row) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument(
                    "LinUCB state must contain only finite numbers");
            }
        }
    }
    for (double value : b_) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "LinUCB state must contain only finite numbers");
        }
    }
    if (updates_ < 0) {
        throw std::invalid_argument(
            "LinUCB update count must be non-negative");
    }

    constexpr double tolerance = 1e-8;
    for (int row = 0; row < dimension_; ++row) {
        for (int column = 0; column < dimension_; ++column) {
            if (std::abs(inverse_[row][column] -
                         inverse_[column][row]) > tolerance) {
                throw std::invalid_argument(
                    "LinUCB inverse matrix must be symmetric");
            }
        }
    }

    std::vector<std::vector<double>> lower(
        dimension_, std::vector<double>(dimension_, 0.0));
    for (int row = 0; row < dimension_; ++row) {
        for (int column = 0; column <= row; ++column) {
            double residual = inverse_[row][column];
            for (int index = 0; index < column; ++index) {
                residual -= lower[row][index] * lower[column][index];
            }
            if (row == column) {
                if (residual <= 1e-12) {
                    throw std::invalid_argument(
                        "LinUCB inverse matrix must be positive definite");
                }
                lower[row][column] = std::sqrt(residual);
            } else {
                lower[row][column] = residual / lower[column][column];
            }
        }
    }
}

std::vector<double> LinUCBModel::validate_vector(
    const std::vector<double>& features) const {
    if (static_cast<int>(features.size()) != dimension_) {
        throw std::invalid_argument("feature vector has the wrong dimension");
    }
    double maximum = 0.0;
    for (double value : features) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "feature vector must contain only finite numbers");
        }
        maximum = std::max(maximum, std::abs(value));
    }
    if (maximum > 20.0) {
        throw std::invalid_argument("feature vector is not normalized");
    }
    return features;
}

std::vector<double> LinUCBModel::matvec(
    const std::vector<std::vector<double>>& matrix,
    const std::vector<double>& vector) const {
    std::vector<double> result(dimension_, 0.0);
    for (int row = 0; row < dimension_; ++row) {
        for (int column = 0; column < dimension_; ++column) {
            result[row] += matrix[row][column] * vector[column];
        }
    }
    return result;
}

std::vector<double> LinUCBModel::theta() const {
    return matvec(inverse_, b_);
}

double LinUCBModel::predict(const std::vector<double>& features) const {
    const std::vector<double> vector = validate_vector(features);
    const std::vector<double> weights = theta();
    double result = 0.0;
    for (int index = 0; index < dimension_; ++index) {
        result += weights[index] * vector[index];
    }
    return result;
}

double LinUCBModel::uncertainty(
    const std::vector<double>& features) const {
    const std::vector<double> vector = validate_vector(features);
    const std::vector<double> projected = matvec(inverse_, vector);
    double variance = 0.0;
    for (int index = 0; index < dimension_; ++index) {
        variance += vector[index] * projected[index];
    }
    return std::sqrt(std::max(0.0, variance));
}

double LinUCBModel::score(const std::vector<double>& features) const {
    return predict(features) + alpha_ * uncertainty(features);
}

void LinUCBModel::update(const std::vector<double>& features,
                         double reward) {
    const std::vector<double> vector = validate_vector(features);
    if (!std::isfinite(reward) || reward < -1.0 || reward > 1.0) {
        throw std::invalid_argument(
            "reward must be finite and within [-1, 1]");
    }
    const std::vector<double> projected = matvec(inverse_, vector);
    double denominator = 1.0;
    for (int index = 0; index < dimension_; ++index) {
        denominator += vector[index] * projected[index];
    }
    if (!std::isfinite(denominator) || denominator <= 1e-10) {
        throw std::invalid_argument(
            "LinUCB update is numerically singular");
    }

    std::vector<std::vector<double>> updated(
        dimension_, std::vector<double>(dimension_, 0.0));
    for (int row = 0; row < dimension_; ++row) {
        for (int column = 0; column < dimension_; ++column) {
            updated[row][column] =
                inverse_[row][column] -
                projected[row] * projected[column] / denominator;
        }
    }
    inverse_ = std::move(updated);
    for (int index = 0; index < dimension_; ++index) {
        b_[index] += reward * vector[index];
    }
    ++updates_;
    validate_state();
}

Json LinUCBModel::state() const {
    return Json{{"dimension", dimension_},
                {"inverse", inverse_},
                {"b", b_},
                {"updates", updates_}};
}

LinUCBModel LinUCBModel::from_state(const Json& state, double alpha) {
    if (!state.is_object() || !state.contains("dimension") ||
        !state.contains("inverse") || !state.contains("b")) {
        throw std::invalid_argument("invalid persisted LinUCB state");
    }
    const int dimension = state.at("dimension").get<int>();
    const auto& inverse_json = state.at("inverse");
    const auto& b_json = state.at("b");
    if (!inverse_json.is_array() || !b_json.is_array()) {
        throw std::invalid_argument("invalid persisted LinUCB state");
    }
    const auto inverse =
        inverse_json.get<std::vector<std::vector<double>>>();
    const auto b = b_json.get<std::vector<double>>();
    const int updates =
        state.contains("updates") ? state.at("updates").get<int>() : 0;
    return LinUCBModel(dimension, alpha, inverse, b, updates);
}

}  // namespace schiva::detail
