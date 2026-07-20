#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Config.hpp"
#include "CsvWriter.hpp"
#include "DebufrParser.hpp"

namespace {

const std::filesystem::path kSampleFile = std::filesystem::path(DEBUFR_TEST_DATA_DIR) / "sample.debufr.out";

debufr::AppConfig makeConfig() {
    debufr::AppConfig config;
    config.inputFile = kSampleFile;
    config.selection.stations = {"30131A", "99999"};
    config.selection.stationSet = {"30131A", "99999"};
    config.selection.messageTypes = {"NC002004"};
    config.selection.messageTypeSet = {"NC002004"};
    config.profile.pressureMnemonic = "PRLC";
    config.profile.variables = {
        {"TMDB", "temperature", debufr::VariableQuantity::Temperature},
        {"TMDP", "dewpoint", debufr::VariableQuantity::Temperature},
        {"REHU", "relative_humidity", debufr::VariableQuantity::Generic},
        {"WDIR", "wind_direction", debufr::VariableQuantity::Generic},
        {"WSPD", "wind_speed", debufr::VariableQuantity::Generic},
        {"UWND", "u_input", debufr::VariableQuantity::Generic},
        {"VWND", "v_input", debufr::VariableQuantity::Generic},
    };
    config.profile.metadata = {"VSIG", "QMPR"};
    return config;
}

debufr::ParseResult parseSample(const debufr::AppConfig& config) {
    std::ifstream input(config.inputFile);
    EXPECT_TRUE(input.good());
    debufr::DebufrParser parser(config);
    return parser.parse(input);
}

}  // namespace

TEST(ParserTest, ExtractsRpidAndMessageType) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    ASSERT_EQ(parsed.profiles.size(), 3U);
    EXPECT_EQ(parsed.profiles.front().stationId, "30131A");
    EXPECT_EQ(parsed.profiles.front().messageType, "NC002004");
    EXPECT_EQ(parsed.profiles[1].stationId, "99999");
}

TEST(ParserTest, ExtractsPressureAndTemperature) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    const auto& level = parsed.profiles.front().levels.front();
    ASSERT_TRUE(level.pressurePa.has_value());
    EXPECT_DOUBLE_EQ(*level.pressurePa, 100000.0);
    ASSERT_TRUE(level.numericValues.at("TMDB").has_value());
    EXPECT_DOUBLE_EQ(*level.numericValues.at("TMDB"), 302.15);
}

TEST(ParserTest, ExtractsPressureAndWindWithoutMixingReplications) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    ASSERT_GE(parsed.profiles.front().levels.size(), 3U);
    const auto& temperatureLevel = parsed.profiles.front().levels[0];
    const auto& windLevel = parsed.profiles.front().levels[2];
    EXPECT_EQ(temperatureLevel.numericValues.count("WDIR"), 0U);
    ASSERT_TRUE(windLevel.numericValues.at("WDIR").has_value());
    EXPECT_DOUBLE_EQ(*windLevel.numericValues.at("WDIR"), 200.0);
    EXPECT_DOUBLE_EQ(*windLevel.pressurePa, 100000.0);
}

TEST(ParserTest, PreventsValuesFromAdjacentReplicationsBeingMixed) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    const auto& firstLevel = parsed.profiles.front().levels[0];
    const auto& secondLevel = parsed.profiles.front().levels[1];
    ASSERT_TRUE(firstLevel.numericValues.find("TMDB") != firstLevel.numericValues.end());
    EXPECT_TRUE(secondLevel.numericValues.find("TMDB") != secondLevel.numericValues.end());
    EXPECT_TRUE(firstLevel.numericValues.find("REHU") == firstLevel.numericValues.end());
    EXPECT_TRUE(secondLevel.numericValues.find("REHU") != secondLevel.numericValues.end());
}

TEST(ParserTest, HandlesMissingValues) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    const auto& level = parsed.profiles.front().levels[1];
    ASSERT_TRUE(level.numericValues.find("TMDP") != level.numericValues.end());
    EXPECT_FALSE(level.numericValues.at("TMDP").has_value());
}

TEST(ParserTest, HandlesDifferentSpacingAndAliases) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    ASSERT_EQ(parsed.profiles[1].stationId, "99999");
    ASSERT_TRUE(parsed.profiles[1].latitude.has_value());
    ASSERT_TRUE(parsed.profiles[1].longitude.has_value());
    EXPECT_NEAR(*parsed.profiles[1].latitude, 40.10, 1e-9);
    EXPECT_NEAR(*parsed.profiles[1].longitude, -75.30, 1e-9);
}

TEST(ParserTest, PreservesDuplicatePressureRowsByDefault) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    std::vector<std::string> warnings;
    const auto prepared = debufr::prepareProfiles(parsed.profiles, config, warnings);
    EXPECT_EQ(prepared.front().levels.size(), 4U);
}

TEST(ParserTest, MergesDuplicatePressureRowsWhenConfigured) {
    auto config = makeConfig();
    config.profile.duplicatePressureHandling = debufr::DuplicatePressureHandling::Merge;
    const auto parsed = parseSample(config);
    std::vector<std::string> warnings;
    const auto prepared = debufr::prepareProfiles(parsed.profiles, config, warnings);
    ASSERT_EQ(prepared.front().levels.size(), 3U);
    const auto& mergedLevel = prepared.front().levels.front();
    EXPECT_EQ(mergedLevel.sourceLevels.size(), 2U);
}

TEST(ParserTest, SelectsOneStationOnly) {
    auto config = makeConfig();
    config.selection.stations = {"99999"};
    config.selection.stationSet = {"99999"};
    const auto parsed = parseSample(config);
    ASSERT_EQ(parsed.profiles.size(), 1U);
    EXPECT_EQ(parsed.profiles.front().stationId, "99999");
}

TEST(ParserTest, ReportsRequestedStationNotFound) {
    auto config = makeConfig();
    config.selection.stations = {"DOES_NOT_EXIST"};
    config.selection.stationSet = {"DOES_NOT_EXIST"};
    const auto parsed = parseSample(config);
    EXPECT_TRUE(parsed.profiles.empty());
    EXPECT_EQ(parsed.stats.stationsFound.count("DOES_NOT_EXIST"), 0U);
}

TEST(ParserTest, AppliesUnitConversions) {
    auto config = makeConfig();
    debufr::CsvWriter writer(config);
    const auto parsed = parseSample(config);
    std::vector<std::string> warnings;
    const auto prepared = debufr::prepareProfiles(parsed.profiles, config, warnings);
    const auto csv = writer.renderProfiles({prepared.front()});
    EXPECT_NE(csv.find("pressure_hPa"), std::string::npos);
    EXPECT_NE(csv.find("temperature_C"), std::string::npos);
    EXPECT_NE(csv.find("1000"), std::string::npos);
    EXPECT_NE(csv.find("29"), std::string::npos);
}

TEST(ParserTest, ComputesWindComponentsWithOriginalValuesTakingPrecedence) {
    auto config = makeConfig();
    config.profile.computeWindComponents = true;
    const auto parsed = parseSample(config);
    std::vector<std::string> warnings;
    const auto prepared = debufr::prepareProfiles(parsed.profiles, config, warnings);
    debufr::CsvWriter writer(config);
    const auto csv = writer.renderProfiles({prepared.front()});
    EXPECT_NE(csv.find("u_wind"), std::string::npos);
    EXPECT_NE(csv.find("v_wind"), std::string::npos);
    EXPECT_NE(csv.find("7.5"), std::string::npos);
    EXPECT_NE(csv.find("13"), std::string::npos);
}

TEST(ParserTest, FinalizesLastProfileAtEndOfFile) {
    auto config = makeConfig();
    const auto parsed = parseSample(config);
    ASSERT_EQ(parsed.profiles.size(), 3U);
    EXPECT_EQ(parsed.profiles.back().stationId, "30131A");
    EXPECT_EQ(parsed.profiles.back().observationTime, "2025-10-27T18:00:00");
}

TEST(ParserTest, MergeAddsSourceLevelsColumnToCsv) {
    auto config = makeConfig();
    config.profile.duplicatePressureHandling = debufr::DuplicatePressureHandling::Merge;
    const auto parsed = parseSample(config);
    std::vector<std::string> warnings;
    const auto prepared = debufr::prepareProfiles(parsed.profiles, config, warnings);
    debufr::CsvWriter writer(config);
    const auto csv = writer.renderProfiles({prepared.front()});
    EXPECT_NE(csv.find("source_levels"), std::string::npos);
    EXPECT_NE(csv.find("1;3"), std::string::npos);
}
