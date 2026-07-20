#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace debufr {

struct ProfileLevel {
    std::size_t levelNumber{};
    std::optional<double> pressurePa;
    std::unordered_map<std::string, std::optional<double>> numericValues;
    std::vector<std::size_t> sourceLevels;
};

struct Profile {
    std::vector<std::size_t> sourceMessageNumbers;
    std::string messageType;
    std::string stationId;
    std::string observationTime;
    std::optional<std::int64_t> observationEpochSeconds;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::vector<ProfileLevel> levels;
};

inline bool almostEqual(const std::optional<double>& left, const std::optional<double>& right, double tolerance = 1e-9) {
    if (!left.has_value() || !right.has_value()) {
        return !left.has_value() && !right.has_value();
    }
    return std::fabs(*left - *right) <= tolerance;
}

}  // namespace debufr
