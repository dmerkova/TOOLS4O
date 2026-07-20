#include "Config.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace debufr {
namespace {

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string upperCopy(const std::string& value) {
    std::string output = value;
    std::transform(output.begin(), output.end(), output.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return output;
}

std::string lowerCopy(const std::string& value) {
    std::string output = value;
    std::transform(output.begin(), output.end(), output.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return output;
}

VariableQuantity parseQuantity(const std::string& value) {
    const auto normalized = lowerCopy(trim(value));
    if (normalized.empty() || normalized == "generic") {
        return VariableQuantity::Generic;
    }
    if (normalized == "temperature") {
        return VariableQuantity::Temperature;
    }
    throw std::runtime_error("Unsupported variable quantity: " + value);
}

DuplicatePressureHandling parseDuplicatePressureHandling(const std::string& value) {
    const auto normalized = lowerCopy(trim(value));
    if (normalized == "preserve") {
        return DuplicatePressureHandling::Preserve;
    }
    if (normalized == "merge") {
        return DuplicatePressureHandling::Merge;
    }
    throw std::runtime_error("Unsupported duplicate_pressure_handling: " + value);
}

OutputOrganization parseOrganization(const std::string& value) {
    const auto normalized = lowerCopy(trim(value));
    if (normalized == "one_file") {
        return OutputOrganization::OneFile;
    }
    if (normalized == "one_file_per_station") {
        return OutputOrganization::OneFilePerStation;
    }
    if (normalized == "one_file_per_profile") {
        return OutputOrganization::OneFilePerProfile;
    }
    throw std::runtime_error("Unsupported output organization: " + value);
}

PressureUnits parsePressureUnits(const std::string& value) {
    const auto normalized = lowerCopy(trim(value));
    if (normalized == "pa") {
        return PressureUnits::Pa;
    }
    if (normalized == "hpa") {
        return PressureUnits::HPa;
    }
    throw std::runtime_error("Unsupported pressure_units: " + value);
}

TemperatureUnits parseTemperatureUnits(const std::string& value) {
    const auto normalized = lowerCopy(trim(value));
    if (normalized == "kelvin") {
        return TemperatureUnits::Kelvin;
    }
    if (normalized == "celsius") {
        return TemperatureUnits::Celsius;
    }
    throw std::runtime_error("Unsupported temperature_units: " + value);
}

std::vector<std::string> requireStringList(const YAML::Node& parent, const std::string& key) {
    if (!parent[key]) {
        return {};
    }
    if (!parent[key].IsSequence()) {
        throw std::runtime_error("Expected '" + key + "' to be a YAML sequence");
    }

    std::vector<std::string> values;
    for (const auto& item : parent[key]) {
        values.push_back(trim(item.as<std::string>()));
    }
    return values;
}

std::unordered_set<std::string> makeSetUpper(const std::vector<std::string>& values) {
    std::unordered_set<std::string> result;
    for (const auto& value : values) {
        result.insert(upperCopy(value));
    }
    return result;
}

std::unordered_set<std::string> makeSetExact(const std::vector<std::string>& values) {
    return std::unordered_set<std::string>(values.begin(), values.end());
}

}  // namespace

std::optional<std::int64_t> parseIso8601(const std::string& value) {
    if (trim(value).empty()) {
        return std::nullopt;
    }

    std::tm time{};
    std::istringstream input(value);
    input >> std::get_time(&time, "%Y-%m-%dT%H:%M:%S");
    if (input.fail()) {
        return std::nullopt;
    }

    time.tm_isdst = 0;
#if defined(_WIN32)
    const auto epochSeconds = _mkgmtime(&time);
#else
    const auto epochSeconds = timegm(&time);
#endif
    if (epochSeconds < 0) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(epochSeconds);
}

bool AppConfig::stationSelected(const std::string& station) const {
    return selection.stationSet.empty() || selection.stationSet.count(station) > 0;
}

bool AppConfig::messageTypeSelected(const std::string& messageType) const {
    return selection.messageTypeSet.empty() || selection.messageTypeSet.count(upperCopy(messageType)) > 0;
}

bool AppConfig::observationTimeSelected(const std::optional<std::int64_t>& observationEpochSeconds) const {
    if (!selection.startTime && !selection.endTime) {
        return true;
    }
    if (!observationEpochSeconds) {
        return false;
    }
    if (selection.startTime && *observationEpochSeconds < *selection.startTime) {
        return false;
    }
    if (selection.endTime && *observationEpochSeconds > *selection.endTime) {
        return false;
    }
    return true;
}

AppConfig loadConfig(const std::filesystem::path& path) {
    const auto yaml = YAML::LoadFile(path.string());
    AppConfig config;

    if (!yaml["input"] || !yaml["input"]["file"]) {
        throw std::runtime_error("Config is missing input.file");
    }
    config.inputFile = yaml["input"]["file"].as<std::string>();

    const auto selection = yaml["selection"];
    if (selection) {
        config.selection.stations = requireStringList(selection, "stations");
        config.selection.stationSet = makeSetExact(config.selection.stations);
        config.selection.messageTypes = requireStringList(selection, "message_types");
        config.selection.messageTypeSet = makeSetUpper(config.selection.messageTypes);
        if (selection["start_time"]) {
            config.selection.startTime = parseIso8601(selection["start_time"].as<std::string>());
            if (!config.selection.startTime) {
                throw std::runtime_error("selection.start_time must be ISO-8601 like 2025-10-27T09:00:00");
            }
        }
        if (selection["end_time"]) {
            config.selection.endTime = parseIso8601(selection["end_time"].as<std::string>());
            if (!config.selection.endTime) {
                throw std::runtime_error("selection.end_time must be ISO-8601 like 2025-10-27T09:00:00");
            }
        }
    }

    const auto profile = yaml["profile"];
    if (profile) {
        if (profile["pressure_mnemonic"]) {
            config.profile.pressureMnemonic = upperCopy(profile["pressure_mnemonic"].as<std::string>());
        }
        if (profile["variables"]) {
            if (!profile["variables"].IsSequence()) {
                throw std::runtime_error("profile.variables must be a YAML sequence");
            }
            for (const auto& variable : profile["variables"]) {
                VariableConfig entry;
                if (!variable["mnemonic"] || !variable["output_name"]) {
                    throw std::runtime_error("Each profile.variables entry must define mnemonic and output_name");
                }
                entry.mnemonic = upperCopy(variable["mnemonic"].as<std::string>());
                entry.outputName = trim(variable["output_name"].as<std::string>());
                if (variable["quantity"]) {
                    entry.quantity = parseQuantity(variable["quantity"].as<std::string>());
                } else if (entry.mnemonic == "TMDB" || entry.mnemonic == "TMDP") {
                    entry.quantity = VariableQuantity::Temperature;
                }
                config.profile.variables.push_back(entry);
            }
        }
        config.profile.metadata = requireStringList(profile, "metadata");
        for (auto& mnemonic : config.profile.metadata) {
            mnemonic = upperCopy(mnemonic);
        }
        if (profile["compute_wind_components"]) {
            config.profile.computeWindComponents = profile["compute_wind_components"].as<bool>();
        }
        if (profile["merge_levels_across_messages"]) {
            config.profile.mergeLevelsAcrossMessages = profile["merge_levels_across_messages"].as<bool>();
        }
        if (profile["duplicate_pressure_handling"]) {
            config.profile.duplicatePressureHandling = parseDuplicatePressureHandling(profile["duplicate_pressure_handling"].as<std::string>());
        }
    }

    const auto output = yaml["output"];
    if (output) {
        if (output["directory"]) {
            config.output.directory = output["directory"].as<std::string>();
        }
        if (output["format"]) {
            config.output.format = lowerCopy(output["format"].as<std::string>());
        }
        if (output["organization"]) {
            config.output.organization = parseOrganization(output["organization"].as<std::string>());
        }
        if (output["pressure_units"]) {
            config.output.pressureUnits = parsePressureUnits(output["pressure_units"].as<std::string>());
        }
        if (output["temperature_units"]) {
            config.output.temperatureUnits = parseTemperatureUnits(output["temperature_units"].as<std::string>());
        }
        if (output["missing_value"]) {
            config.output.missingValue = output["missing_value"].as<std::string>();
        }
        if (output["sort_pressure"]) {
            config.output.sortPressure = lowerCopy(output["sort_pressure"].as<std::string>());
        }
        if (output["include_columns"]) {
            config.output.includeColumns = requireStringList(output, "include_columns");
        }
    }

    validateConfig(config);
    return config;
}

void validateConfig(const AppConfig& config) {
    if (config.inputFile.empty()) {
        throw std::runtime_error("input.file must not be empty");
    }
    if (config.profile.pressureMnemonic.empty()) {
        throw std::runtime_error("profile.pressure_mnemonic must not be empty");
    }
    if (config.profile.variables.empty()) {
        throw std::runtime_error("profile.variables must contain at least one variable");
    }
    if (config.output.format != "csv") {
        throw std::runtime_error("Only output.format=csv is supported");
    }
    if (config.output.sortPressure != "ascending" && config.output.sortPressure != "descending" &&
        config.output.sortPressure != "none") {
        throw std::runtime_error("output.sort_pressure must be ascending, descending, or none");
    }
    if (config.selection.startTime && config.selection.endTime && *config.selection.startTime > *config.selection.endTime) {
        throw std::runtime_error("selection.start_time must be earlier than or equal to selection.end_time");
    }

    std::unordered_set<std::string> seenVariables;
    for (const auto& variable : config.profile.variables) {
        if (variable.mnemonic.empty()) {
            throw std::runtime_error("profile.variables mnemonic must not be empty");
        }
        if (variable.outputName.empty()) {
            throw std::runtime_error("profile.variables output_name must not be empty");
        }
        if (!seenVariables.insert(variable.mnemonic).second) {
            throw std::runtime_error("Duplicate profile.variables mnemonic: " + variable.mnemonic);
        }
    }
}

}  // namespace debufr
