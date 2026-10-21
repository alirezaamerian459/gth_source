#pragma once

#include <string>
#include <vector>

namespace gth::application {

enum class UpdateState {
    UpToDate,                 // the copy is Sony's latest (or Sony lists nothing newer)
    UpdateAvailable,          // Sony has a newer release than this copy
    InstalledVersionUnknown,  // Sony's latest is known, but which version this copy is can't be told
    NoSonyData,               // nowhere to ask Sony about this game (PS5 without a saved link, PS3, ...)
    CheckFailed,              // couldn't get an answer (offline, blocked, ...)
};

// One game's answer to "is my copy up to date?".
struct UpdateCheckEntry {
    std::string gameVariantId;
    UpdateState state = UpdateState::CheckFailed;

    std::string latestVersion;      // Sony's newest, empty when unknown
    std::string installedVersion;   // what the verdict is based on, empty when unknown
    bool installedVersionInferred = false;  // true when it was worked out from the package's file sizes
    std::string detail;             // one line of plain-language explanation
};

struct UpdateCheckSummary {
    std::vector<UpdateCheckEntry> entries;
    int upToDate = 0;
    int updatesAvailable = 0;
    int installedUnknown = 0;
    int noSonyData = 0;
    int failed = 0;
    std::string firstFailure;  // why the first CheckFailed happened
};

// Compares every Active game in the library against what Sony's update
// servers say is the newest release. Read-only: it never touches a game's
// files or the library itself.
class IUpdateChecker {
public:
    virtual ~IUpdateChecker() = default;
    virtual UpdateCheckSummary checkAll() = 0;
};

}  // namespace gth::application
