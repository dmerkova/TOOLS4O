#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "Config.hpp"
#include "CsvWriter.hpp"
#include "DebufrParser.hpp"

namespace {

constexpr const char* kVersion = "1.0.0";

enum ExitCode {
    kSuccess = 0,
    kInvalidArguments = 1,
    kMissingInputFile = 2,
    kMalformedYaml = 3,
    kInvalidConfiguration = 4,
    kOutputFailure = 5,
    kParsingFailure = 6,
};

void printUsage(std::ostream& output) {
    output << "Usage:\n"
           << "  debufr-profile-extractor --config <file>\n"
           << "  debufr-profile-extractor --validate-config <file>\n"
           << "  debufr-profile-extractor --help\n"
           << "  debufr-profile-extractor --version\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            printUsage(std::cout);
            return kSuccess;
        }
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "debufr-profile-extractor " << kVersion << '\n';
            return kSuccess;
        }
        if (argc == 3 && std::string(argv[1]) == "--validate-config") {
            debufr::AppConfig config;
            try {
                config = debufr::loadConfig(argv[2]);
            } catch (const YAML::Exception& error) {
                std::cerr << "Error: " << error.what() << '\n';
                return kMalformedYaml;
            } catch (const std::runtime_error& error) {
                std::cerr << "Error: " << error.what() << '\n';
                return kInvalidConfiguration;
            }
            std::cout << "Config is valid: " << argv[2] << '\n';
            std::cout << "Input file: " << config.inputFile.string() << '\n';
            return kSuccess;
        }
        if (argc != 3 || std::string(argv[1]) != "--config") {
            printUsage(std::cerr);
            return kInvalidArguments;
        }

        debufr::AppConfig config;
        try {
            config = debufr::loadConfig(argv[2]);
        } catch (const YAML::Exception& error) {
            std::cerr << "Error: " << error.what() << '\n';
            return kMalformedYaml;
        } catch (const std::runtime_error& error) {
            std::cerr << "Error: " << error.what() << '\n';
            return kInvalidConfiguration;
        }

        std::ifstream input(config.inputFile);
        if (!input) {
            std::cerr << "Input file does not exist or cannot be opened: " << config.inputFile << '\n';
            return kMissingInputFile;
        }

        debufr::ParseResult parsed;
        try {
            const debufr::DebufrParser parser(config);
            parsed = parser.parse(input);
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << '\n';
            return kParsingFailure;
        }

        std::vector<std::string> warnings = parsed.stats.warnings;
        const auto preparedProfiles = debufr::prepareProfiles(parsed.profiles, config, warnings);
        std::vector<std::filesystem::path> writtenFiles;
        try {
            const debufr::CsvWriter writer(config);
            writtenFiles = writer.writeProfiles(preparedProfiles);
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << '\n';
            return kOutputFailure;
        }

        std::size_t extractedLevels{};
        for (const auto& profile : preparedProfiles) {
            extractedLevels += profile.levels.size();
        }

        std::cout << "BUFR messages read: " << parsed.stats.messagesRead << '\n';
        std::cout << "Matching profiles: " << preparedProfiles.size() << '\n';
        std::cout << "Extracted levels: " << extractedLevels << '\n';
        std::cout << "Stations found:";
        for (const auto& station : parsed.stats.stationsFound) {
            std::cout << ' ' << station;
        }
        std::cout << '\n';

        if (!config.selection.stations.empty()) {
            std::vector<std::string> missingStations;
            for (const auto& station : config.selection.stations) {
                if (parsed.stats.stationsFound.count(station) == 0) {
                    missingStations.push_back(station);
                }
            }
            std::cout << "Requested stations not found:";
            if (missingStations.empty()) {
                std::cout << " none";
            } else {
                for (const auto& station : missingStations) {
                    std::cout << ' ' << station;
                }
            }
            std::cout << '\n';
        }

        for (const auto& output : writtenFiles) {
            std::cout << "Wrote " << output.string() << '\n';
        }
        if (parsed.stats.malformedLines > 0) {
            std::cerr << "Malformed or incomplete lines encountered: " << parsed.stats.malformedLines << '\n';
        }
        for (const auto& warning : warnings) {
            std::cerr << "Warning: " << warning << '\n';
        }
        return kSuccess;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return kParsingFailure;
    }
}
