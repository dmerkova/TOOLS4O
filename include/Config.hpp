#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace debufr {

enum class VariableQuantity {
    Generic,
    Temperature,
};

enum class DuplicatePressureHandling {
    Preserve,
    Merge,
};

enum class OutputOrganization {
    OneFile,
    OneFilePerStation,
    OneFilePerProfile,
};

enum class PressureUnits {
    Pa,
    HPa,
};

enum class TemperatureUnits {
    Kelvin,
    Celsius,
};

struct VariableConfig {
    std::string mnemonic;
    std::string outputName;
    VariableQuantity quantity{VariableQuantity::Generic};
};

struct SelectionConfig {
    std::vector<std::string> stations;
    std::unordered_set<std::string> stationSet;
    std::vector<std::string> messageTypes;
    std::unordered_set<std::string> messageTypeSet;
    std::optional<std::int64_t> startTime;
    std::optional<std::int64_t> endTime;
};

struct ProfileConfig {
    std::string pressureMnemonic{"PRLC"};
    std::vector<VariableConfig> variables;
    std::vector<std::string> metadata;
    bool computeWindComponents{false};
    bool mergeLevelsAcrossMessages{false};
    DuplicatePressureHandling duplicatePressureHandling{DuplicatePressureHandling::Preserve};
};

struct OutputConfig {
    std::filesystem::path directory{"output"};
    std::string format{"csv"};
    OutputOrganization organization{OutputOrganization::OneFilePerStation};
    PressureUnits pressureUnits{PressureUnits::HPa};
    TemperatureUnits temperatureUnits{TemperatureUnits::Celsius};
    std::string missingValue;
    std::string sortPressure{"descending"};
    std::vector<std::string> includeColumns{
        "message_number",
        "message_type",
        "station",
        "observation_time",
        "latitude",
        "longitude",
        "level_number",
        "pressure",
    };
};

struct AppConfig {
    std::filesystem::path inputFile;
    SelectionConfig selection;
    ProfileConfig profile;
    OutputConfig output;

    [[nodiscard]] bool stationSelected(const std::string& station) const;
    [[nodiscard]] bool messageTypeSelected(const std::string& messageType) const;
    [[nodiscard]] bool observationTimeSelected(const std::optional<std::int64_t>& observationEpochSeconds) const;
};

[[nodiscard]] AppConfig loadConfig(const std::filesystem::path& path);
void validateConfig(const AppConfig& config);
[[nodiscard]] std::optional<std::int64_t> parseIso8601(const std::string& value);

}  // namespace debufr
