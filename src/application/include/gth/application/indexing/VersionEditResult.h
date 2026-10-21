#pragma once

#include <string>
#include <vector>

namespace gth::application {

// Outcome of typing a version in for one game.
struct SetVersionResult {
    bool success = false;
    std::string errorMessage;  // when !success
};

// Outcome of applying a whole "title ID -> version" list at once.
struct ImportVersionsResult {
    int applied = 0;          // releases whose version was set
    int linesIgnored = 0;     // lines with no recognizable title ID + version
    int failed = 0;           // matched a game but the version couldn't be saved
    std::string firstError;   // why the first failure happened
    // Title IDs from the list that no game in the library has - usually a
    // typo, or a game that lives on a storage this PC doesn't have.
    std::vector<std::string> unmatchedTitleIds;
};

}  // namespace gth::application
