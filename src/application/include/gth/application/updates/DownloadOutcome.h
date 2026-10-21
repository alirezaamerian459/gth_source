#pragma once

#include <string>

namespace gth::application {

struct DownloadOutcome {
    bool success = false;
    std::string localPath;
    std::string errorMessage;
    int attemptsUsed = 0;
};

}  // namespace gth::application
