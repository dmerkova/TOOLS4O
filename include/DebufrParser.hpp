#pragma once

#include <iosfwd>
#include <string>
#include <unordered_set>
#include <vector>

#include "Config.hpp"
#include "Profile.hpp"

namespace debufr {

struct ParseStats {
    std::size_t messagesRead{};
    std::size_t malformedLines{};
    std::unordered_set<std::string> stationsFound;
    std::vector<std::string> warnings;
};

struct ParseResult {
    std::vector<Profile> profiles;
    ParseStats stats;
};

class DebufrParser {
  public:
    explicit DebufrParser(AppConfig config);

    [[nodiscard]] ParseResult parse(std::istream& input) const;

  private:
    AppConfig config_;
};

[[nodiscard]] std::vector<Profile> prepareProfiles(const std::vector<Profile>& profiles,
                                                   const AppConfig& config,
                                                   std::vector<std::string>& warnings);

}  // namespace debufr
