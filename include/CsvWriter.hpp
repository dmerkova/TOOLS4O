#pragma once

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include "Config.hpp"
#include "Profile.hpp"

namespace debufr {

class CsvWriter {
  public:
    explicit CsvWriter(AppConfig config);

    [[nodiscard]] std::vector<std::filesystem::path> writeProfiles(const std::vector<Profile>& profiles) const;
    [[nodiscard]] std::string renderProfiles(const std::vector<Profile>& profiles) const;

  private:
    AppConfig config_;
};

}  // namespace debufr
