#include "CsvWriter.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace debufr {
namespace {

std::string trimTrailingZeros(std::string value) {
    if (value.find('.') == std::string::npos) {
        return value;
    }
    while (!value.empty() && value.back() == '0') {
        value.pop_back();
    }
    if (!value.empty() && value.back() == '.') {
        value.pop_back();
    }
    if (value.empty()) {
        return "0";
    }
    return value;
}

std::string formatDouble(double value) {
    if (std::fabs(value) < 1e-9) {
        value = 0.0;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << value;
    return trimTrailingZeros(output.str());
}

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\n") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') {
            escaped += '"';
        }
        escaped += character;
    }
    escaped += '"';
    return escaped;
}

std::string joinMessageNumbers(const std::vector<std::size_t>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output << ';';
        }
        output << values[index];
    }
    return output.str();
}

std::string joinLevelNumbers(const std::vector<std::size_t>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output << ';';
        }
        output << values[index];
    }
    return output.str();
}

std::string pressureColumnName(const OutputConfig& output) {
    return output.pressureUnits == PressureUnits::HPa ? "pressure_hPa" : "pressure_Pa";
}

std::string variableColumnName(const VariableConfig& variable, const OutputConfig& output) {
    if (variable.quantity == VariableQuantity::Temperature) {
        return variable.outputName + (output.temperatureUnits == TemperatureUnits::Celsius ? "_C" : "_K");
    }
    return variable.outputName;
}

std::optional<double> convertPressure(const std::optional<double>& pressurePa, PressureUnits units) {
    if (!pressurePa) {
        return std::nullopt;
    }
    if (units == PressureUnits::HPa) {
        return *pressurePa / 100.0;
    }
    return pressurePa;
}

std::optional<double> convertTemperature(const std::optional<double>& value, TemperatureUnits units) {
    if (!value) {
        return std::nullopt;
    }
    if (units == TemperatureUnits::Celsius) {
        return *value - 273.15;
    }
    return value;
}

std::optional<double> findValue(const ProfileLevel& level, const std::string& mnemonic) {
    const auto iterator = level.numericValues.find(mnemonic);
    if (iterator == level.numericValues.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<double> computeU(const ProfileLevel& level) {
    const auto original = findValue(level, "UWND");
    if (original) {
        return original;
    }
    const auto direction = findValue(level, "WDIR");
    const auto speed = findValue(level, "WSPD");
    if (!direction || !speed) {
        return std::nullopt;
    }
    constexpr double kPi = 3.14159265358979323846;
    constexpr double degreesToRadians = kPi / 180.0;
    return -*speed * std::sin(*direction * degreesToRadians);
}

std::optional<double> computeV(const ProfileLevel& level) {
    const auto original = findValue(level, "VWND");
    if (original) {
        return original;
    }
    const auto direction = findValue(level, "WDIR");
    const auto speed = findValue(level, "WSPD");
    if (!direction || !speed) {
        return std::nullopt;
    }
    constexpr double kPi = 3.14159265358979323846;
    constexpr double degreesToRadians = kPi / 180.0;
    return -*speed * std::cos(*direction * degreesToRadians);
}

std::string sanitized(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            output.push_back(character);
        } else {
            output.push_back('_');
        }
    }
    return output;
}

std::vector<ProfileLevel> sortedLevels(const Profile& profile, const OutputConfig& output) {
    auto levels = profile.levels;
    if (output.sortPressure == "none") {
        return levels;
    }

    std::stable_sort(levels.begin(), levels.end(), [&](const ProfileLevel& left, const ProfileLevel& right) {
        if (!left.pressurePa && !right.pressurePa) {
            return left.levelNumber < right.levelNumber;
        }
        if (!left.pressurePa) {
            return false;
        }
        if (!right.pressurePa) {
            return true;
        }
        if (output.sortPressure == "ascending") {
            if (*left.pressurePa == *right.pressurePa) {
                return left.levelNumber < right.levelNumber;
            }
            return *left.pressurePa < *right.pressurePa;
        }
        if (*left.pressurePa == *right.pressurePa) {
            return left.levelNumber < right.levelNumber;
        }
        return *left.pressurePa > *right.pressurePa;
    });
    return levels;
}

struct RenderedTable {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};

RenderedTable renderTable(const std::vector<Profile>& profiles, const AppConfig& config) {
    RenderedTable table;
    for (const auto& column : config.output.includeColumns) {
        if (column == "pressure") {
            table.header.push_back(pressureColumnName(config.output));
        } else {
            table.header.push_back(column);
        }
    }
    for (const auto& variable : config.profile.variables) {
        table.header.push_back(variableColumnName(variable, config.output));
    }
    for (const auto& metadataMnemonic : config.profile.metadata) {
        table.header.push_back(metadataMnemonic);
    }
    if (config.profile.computeWindComponents) {
        table.header.push_back("u_wind");
        table.header.push_back("v_wind");
    }
    if (config.profile.duplicatePressureHandling == DuplicatePressureHandling::Merge) {
        table.header.push_back("source_levels");
    }

    for (const auto& profile : profiles) {
        for (const auto& level : sortedLevels(profile, config.output)) {
            std::vector<std::string> row;
            for (const auto& column : config.output.includeColumns) {
                if (column == "message_number") {
                    row.push_back(joinMessageNumbers(profile.sourceMessageNumbers));
                } else if (column == "message_type") {
                    row.push_back(profile.messageType);
                } else if (column == "station") {
                    row.push_back(profile.stationId);
                } else if (column == "observation_time") {
                    row.push_back(profile.observationTime);
                } else if (column == "latitude") {
                    row.push_back(profile.latitude ? formatDouble(*profile.latitude) : config.output.missingValue);
                } else if (column == "longitude") {
                    row.push_back(profile.longitude ? formatDouble(*profile.longitude) : config.output.missingValue);
                } else if (column == "level_number") {
                    row.push_back(std::to_string(level.levelNumber));
                } else if (column == "pressure") {
                    const auto pressure = convertPressure(level.pressurePa, config.output.pressureUnits);
                    row.push_back(pressure ? formatDouble(*pressure) : config.output.missingValue);
                } else {
                    row.push_back(config.output.missingValue);
                }
            }
            for (const auto& variable : config.profile.variables) {
                auto value = findValue(level, variable.mnemonic);
                if (variable.quantity == VariableQuantity::Temperature) {
                    value = convertTemperature(value, config.output.temperatureUnits);
                }
                row.push_back(value ? formatDouble(*value) : config.output.missingValue);
            }
            for (const auto& metadataMnemonic : config.profile.metadata) {
                const auto value = findValue(level, metadataMnemonic);
                row.push_back(value ? formatDouble(*value) : config.output.missingValue);
            }
            if (config.profile.computeWindComponents) {
                const auto u = computeU(level);
                const auto v = computeV(level);
                row.push_back(u ? formatDouble(*u) : config.output.missingValue);
                row.push_back(v ? formatDouble(*v) : config.output.missingValue);
            }
            if (config.profile.duplicatePressureHandling == DuplicatePressureHandling::Merge) {
                row.push_back(joinLevelNumbers(level.sourceLevels));
            }
            table.rows.push_back(std::move(row));
        }
    }

    return table;
}

std::string toCsv(const RenderedTable& table) {
    std::ostringstream output;
    for (std::size_t index = 0; index < table.header.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << csvEscape(table.header[index]);
    }
    output << '\n';

    for (const auto& row : table.rows) {
        for (std::size_t index = 0; index < row.size(); ++index) {
            if (index > 0) {
                output << ',';
            }
            output << csvEscape(row[index]);
        }
        output << '\n';
    }
    return output.str();
}

std::filesystem::path fileForProfile(const Profile& profile, const AppConfig& config) {
    switch (config.output.organization) {
        case OutputOrganization::OneFile:
            return config.output.directory / "profiles.csv";
        case OutputOrganization::OneFilePerStation:
            return config.output.directory / (sanitized(profile.stationId.empty() ? "unknown_station" : profile.stationId) + ".csv");
        case OutputOrganization::OneFilePerProfile: {
            const auto station = sanitized(profile.stationId.empty() ? "unknown_station" : profile.stationId);
            const auto time = sanitized(profile.observationTime.empty() ? "unknown_time" : profile.observationTime);
            const auto messages = sanitized(joinMessageNumbers(profile.sourceMessageNumbers));
            return config.output.directory / (station + "_" + time + "_msg_" + messages + ".csv");
        }
    }
    throw std::runtime_error("Unknown output organization");
}

}  // namespace

CsvWriter::CsvWriter(AppConfig config) : config_(std::move(config)) {}

std::vector<std::filesystem::path> CsvWriter::writeProfiles(const std::vector<Profile>& profiles) const {
    if (!std::filesystem::exists(config_.output.directory) && !std::filesystem::create_directories(config_.output.directory)) {
        throw std::runtime_error("Failed to create output directory: " + config_.output.directory.string());
    }

    std::map<std::filesystem::path, std::vector<Profile>> groups;
    for (const auto& profile : profiles) {
        groups[fileForProfile(profile, config_)].push_back(profile);
    }

    std::vector<std::filesystem::path> outputs;
    for (const auto& [path, groupedProfiles] : groups) {
        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("Failed to open output file for writing: " + path.string());
        }
        file << renderProfiles(groupedProfiles);
        outputs.push_back(path);
    }
    return outputs;
}

std::string CsvWriter::renderProfiles(const std::vector<Profile>& profiles) const {
    return toCsv(renderTable(profiles, config_));
}

}  // namespace debufr
