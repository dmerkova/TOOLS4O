#include "DebufrParser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <ios>
#include <istream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace debufr {
namespace {

struct DescriptorRecord {
    std::string mnemonic;
    std::string valueText;
};

struct ObservationParts {
    std::optional<int> year;
    std::optional<int> month;
    std::optional<int> day;
    std::optional<int> hour;
    std::optional<int> minute;

    [[nodiscard]] bool hasAnyValue() const {
        return year || month || day || hour || minute;
    }

    [[nodiscard]] bool isComplete() const {
        return year && month && day && hour;
    }
};

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

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::optional<DescriptorRecord> parseDescriptorLine(const std::string& line) {
    std::istringstream input(line);
    std::string descriptor;
    std::string mnemonic;
    if (!(input >> descriptor >> mnemonic)) {
        return std::nullopt;
    }
    if (!std::all_of(descriptor.begin(), descriptor.end(), [](unsigned char character) { return std::isdigit(character); })) {
        return std::nullopt;
    }

    auto rest = trim(line.substr(line.find(mnemonic) + mnemonic.size()));
    if (rest.empty()) {
        return DescriptorRecord{upperCopy(mnemonic), {}};
    }

    std::size_t separatorPosition = std::string::npos;
    for (std::size_t index = 0; index + 1 < rest.size(); ++index) {
        if (std::isspace(static_cast<unsigned char>(rest[index])) &&
            std::isspace(static_cast<unsigned char>(rest[index + 1]))) {
            separatorPosition = index;
            break;
        }
    }

    if (separatorPosition != std::string::npos) {
        rest = trim(rest.substr(0, separatorPosition));
    }

    return DescriptorRecord{upperCopy(mnemonic), rest};
}

std::optional<double> parseNumericValue(const std::string& text) {
    if (text.empty() || upperCopy(text) == "MISSING") {
        return std::nullopt;
    }
    try {
        std::size_t processed{};
        const auto value = std::stod(text, &processed);
        if (processed != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<int> parseIntegerValue(const std::string& text) {
    if (const auto numeric = parseNumericValue(text)) {
        return static_cast<int>(std::llround(*numeric));
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, std::int64_t>> makeObservationTime(const ObservationParts& parts) {
    if (!parts.isComplete()) {
        return std::nullopt;
    }

    std::ostringstream iso;
    iso << std::setfill('0') << std::setw(4) << *parts.year << '-'
        << std::setw(2) << *parts.month << '-'
        << std::setw(2) << *parts.day << 'T'
        << std::setw(2) << *parts.hour << ':'
        << std::setw(2) << parts.minute.value_or(0) << ":00";

    const auto epochSeconds = parseIso8601(iso.str());
    if (!epochSeconds) {
        return std::nullopt;
    }
    return std::make_pair(iso.str(), *epochSeconds);
}

std::string makePressureKey(double pressurePa) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << pressurePa;
    return output.str();
}

bool profileMatchesSelection(const Profile& profile, const AppConfig& config) {
    return config.stationSelected(profile.stationId) && config.messageTypeSelected(profile.messageType) &&
           config.observationTimeSelected(profile.observationEpochSeconds);
}

std::string makeProfileGroupKey(const Profile& profile) {
    std::ostringstream key;
    key << profile.messageType << '|' << profile.stationId << '|' << profile.observationTime << '|';
    if (profile.latitude) {
        key << std::fixed << std::setprecision(6) << *profile.latitude;
    }
    key << '|';
    if (profile.longitude) {
        key << std::fixed << std::setprecision(6) << *profile.longitude;
    }
    return key.str();
}

Profile mergeProfileLevels(const Profile& profile, std::vector<std::string>& warnings) {
    if (profile.levels.empty()) {
        return profile;
    }

    Profile merged = profile;
    merged.levels.clear();

    std::unordered_map<std::string, std::size_t> pressureToIndex;
    for (const auto& level : profile.levels) {
        if (!level.pressurePa) {
            merged.levels.push_back(level);
            continue;
        }

        const auto key = makePressureKey(*level.pressurePa);
        const auto existing = pressureToIndex.find(key);
        if (existing == pressureToIndex.end()) {
            pressureToIndex.emplace(key, merged.levels.size());
            merged.levels.push_back(level);
            continue;
        }

        auto& destination = merged.levels[existing->second];
        destination.sourceLevels.insert(destination.sourceLevels.end(), level.sourceLevels.begin(), level.sourceLevels.end());
        for (const auto& [mnemonic, value] : level.numericValues) {
            const auto current = destination.numericValues.find(mnemonic);
            if (current == destination.numericValues.end() || !current->second.has_value()) {
                destination.numericValues[mnemonic] = value;
                continue;
            }
            if (!value.has_value()) {
                continue;
            }
            if (!almostEqual(current->second, value)) {
                std::ostringstream message;
                message << "Conflicting duplicate-pressure values for station=" << profile.stationId
                        << " observation_time=" << profile.observationTime
                        << " pressure_pa=" << *level.pressurePa
                        << " mnemonic=" << mnemonic
                        << " first=" << *current->second
                        << " second=" << *value;
                warnings.push_back(message.str());
            }
        }
    }

    return merged;
}

}  // namespace

DebufrParser::DebufrParser(AppConfig config) : config_(std::move(config)) {}

ParseResult DebufrParser::parse(std::istream& input) const {
    ParseResult result;

    std::size_t currentMessageNumber{};
    std::string currentMessageType;
    ObservationParts observationParts;
    bool hasProfile = false;
    bool inUarlvSequence = false;
    std::optional<ProfileLevel> currentLevel;
    Profile currentProfile;

    const auto ensureProfile = [&]() -> Profile& {
        if (!hasProfile) {
            hasProfile = true;
            currentProfile = Profile{};
            if (currentMessageNumber > 0) {
                currentProfile.sourceMessageNumbers = {currentMessageNumber};
            }
            currentProfile.messageType = currentMessageType;
        }
        return currentProfile;
    };

    const auto finalizeLevel = [&]() {
        if (!currentLevel.has_value()) {
            return;
        }
        if (currentLevel->sourceLevels.empty()) {
            currentLevel->sourceLevels.push_back(currentLevel->levelNumber);
        }
        ensureProfile().levels.push_back(*currentLevel);
        currentLevel.reset();
    };

    const auto finalizeProfile = [&]() {
        finalizeLevel();
        if (!hasProfile) {
            inUarlvSequence = false;
            observationParts = ObservationParts{};
            return;
        }

        currentProfile.messageType = currentMessageType;
        if (currentProfile.sourceMessageNumbers.empty() && currentMessageNumber > 0) {
            currentProfile.sourceMessageNumbers = {currentMessageNumber};
        }

        if (const auto observation = makeObservationTime(observationParts)) {
            currentProfile.observationTime = observation->first;
            currentProfile.observationEpochSeconds = observation->second;
        } else if (observationParts.hasAnyValue()) {
            result.stats.warnings.push_back("Incomplete observation time for station=" + currentProfile.stationId +
                                            " in message=" + std::to_string(currentMessageNumber));
        }

        if (!currentProfile.stationId.empty()) {
            result.stats.stationsFound.insert(currentProfile.stationId);
        }
        if ((!currentProfile.stationId.empty() || !currentProfile.levels.empty()) && profileMatchesSelection(currentProfile, config_)) {
            result.profiles.push_back(currentProfile);
        }

        currentProfile = Profile{};
        observationParts = ObservationParts{};
        hasProfile = false;
        inUarlvSequence = false;
    };

    std::string line;
    std::size_t lineNumber{};
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        if (startsWith(trimmed, "Found BUFR message") || startsWith(trimmed, "BUFR message #")) {
            finalizeProfile();
            std::smatch match;
            if (std::regex_search(trimmed, match, std::regex(R"(message\s*#?\s*(\d+))", std::regex::icase)) && match.size() > 1) {
                currentMessageNumber = static_cast<std::size_t>(std::stoul(match[1].str()));
            } else if (std::regex_search(trimmed, match, std::regex(R"(Found BUFR message\s+(\d+))", std::regex::icase)) &&
                       match.size() > 1) {
                currentMessageNumber = static_cast<std::size_t>(std::stoul(match[1].str()));
            } else {
                ++result.stats.malformedLines;
            }
            currentMessageType.clear();
            ++result.stats.messagesRead;
            continue;
        }

        if (startsWith(trimmed, "MESSAGE TYPE")) {
            std::istringstream messageLine(trimmed);
            std::string ignored;
            messageLine >> ignored >> ignored >> currentMessageType;
            if (hasProfile) {
                currentProfile.messageType = currentMessageType;
            }
            continue;
        }

        if (trimmed.find("{UARLV}") != std::string::npos) {
            ensureProfile();
            inUarlvSequence = true;
            continue;
        }

        if (trimmed.find("UARLV") != std::string::npos && trimmed.find("REPLICATION #") != std::string::npos) {
            ensureProfile();
            finalizeLevel();
            currentLevel = ProfileLevel{};
            std::smatch match;
            if (std::regex_search(trimmed, match, std::regex(R"(REPLICATION\s*#\s*(\d+))", std::regex::icase)) && match.size() > 1) {
                currentLevel->levelNumber = static_cast<std::size_t>(std::stoul(match[1].str()));
            } else {
                ++result.stats.malformedLines;
                currentLevel->levelNumber = 0;
            }
            currentLevel->sourceLevels = {currentLevel->levelNumber};
            inUarlvSequence = true;
            continue;
        }

        const auto descriptor = parseDescriptorLine(trimmed);
        if (!descriptor) {
            continue;
        }

        if (descriptor->mnemonic == "RPID") {
            if (hasProfile && !currentProfile.stationId.empty()) {
                finalizeProfile();
            }
            auto& profile = ensureProfile();
            profile.stationId = descriptor->valueText;
            result.stats.stationsFound.insert(profile.stationId);
            continue;
        }

        if (descriptor->mnemonic == "CLAT" || descriptor->mnemonic == "CLATH") {
            ensureProfile().latitude = parseNumericValue(descriptor->valueText);
            continue;
        }
        if (descriptor->mnemonic == "CLON" || descriptor->mnemonic == "CLONH") {
            ensureProfile().longitude = parseNumericValue(descriptor->valueText);
            continue;
        }
        if (descriptor->mnemonic == "YEAR") {
            observationParts.year = parseIntegerValue(descriptor->valueText);
            continue;
        }
        if (descriptor->mnemonic == "MNTH") {
            observationParts.month = parseIntegerValue(descriptor->valueText);
            continue;
        }
        if (descriptor->mnemonic == "DAYS") {
            observationParts.day = parseIntegerValue(descriptor->valueText);
            continue;
        }
        if (descriptor->mnemonic == "HOUR") {
            observationParts.hour = parseIntegerValue(descriptor->valueText);
            continue;
        }
        if (descriptor->mnemonic == "MINU") {
            observationParts.minute = parseIntegerValue(descriptor->valueText);
            continue;
        }

        if (!inUarlvSequence || !currentLevel.has_value()) {
            continue;
        }

        const auto numericValue = parseNumericValue(descriptor->valueText);
        if (descriptor->mnemonic == config_.profile.pressureMnemonic) {
            currentLevel->pressurePa = numericValue;
            continue;
        }

        bool trackedMnemonic = false;
        for (const auto& variable : config_.profile.variables) {
            if (variable.mnemonic == descriptor->mnemonic) {
                currentLevel->numericValues[descriptor->mnemonic] = numericValue;
                trackedMnemonic = true;
                break;
            }
        }
        if (trackedMnemonic) {
            continue;
        }

        for (const auto& metadataMnemonic : config_.profile.metadata) {
            if (metadataMnemonic == descriptor->mnemonic) {
                currentLevel->numericValues[descriptor->mnemonic] = numericValue;
                trackedMnemonic = true;
                break;
            }
        }
        if (!trackedMnemonic && (descriptor->mnemonic == "WDIR" || descriptor->mnemonic == "WSPD" || descriptor->mnemonic == "UWND" ||
                                 descriptor->mnemonic == "VWND")) {
            currentLevel->numericValues[descriptor->mnemonic] = numericValue;
        }
    }

    finalizeProfile();
    return result;
}

std::vector<Profile> prepareProfiles(const std::vector<Profile>& profiles,
                                     const AppConfig& config,
                                     std::vector<std::string>& warnings) {
    std::vector<Profile> mergedAcrossMessages;
    if (config.profile.mergeLevelsAcrossMessages) {
        std::map<std::string, Profile> grouped;
        for (const auto& profile : profiles) {
            const auto key = makeProfileGroupKey(profile);
            auto& destination = grouped[key];
            if (destination.stationId.empty()) {
                destination = profile;
                continue;
            }
            destination.levels.insert(destination.levels.end(), profile.levels.begin(), profile.levels.end());
            destination.sourceMessageNumbers.insert(destination.sourceMessageNumbers.end(),
                                                    profile.sourceMessageNumbers.begin(),
                                                    profile.sourceMessageNumbers.end());
        }
        for (auto& [key, profile] : grouped) {
            (void)key;
            std::sort(profile.sourceMessageNumbers.begin(), profile.sourceMessageNumbers.end());
            profile.sourceMessageNumbers.erase(std::unique(profile.sourceMessageNumbers.begin(), profile.sourceMessageNumbers.end()),
                                               profile.sourceMessageNumbers.end());
            mergedAcrossMessages.push_back(std::move(profile));
        }
    } else {
        mergedAcrossMessages = profiles;
    }

    if (config.profile.duplicatePressureHandling == DuplicatePressureHandling::Preserve) {
        return mergedAcrossMessages;
    }

    std::vector<Profile> deduplicated;
    deduplicated.reserve(mergedAcrossMessages.size());
    for (const auto& profile : mergedAcrossMessages) {
        deduplicated.push_back(mergeProfileLevels(profile, warnings));
    }
    return deduplicated;
}

}  // namespace debufr
