#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

// Permanent record of one completed (successful, failed, or rolled-back)
// update attempt for a GameVariant - the audit trail behind the spec's
// "Update History" view (1.00 -> 1.02 -> 1.04 -> 1.06). Written once an
// UpdateJob reaches a terminal state; never edited afterward.
struct UpdateHistory {
    std::string updateHistoryId = Guid::newGuid();
    std::string gameVariantId;

    std::string previousVersion;
    std::string newVersion;

    std::string startedAt = Clock::utcNowIso8601();
    std::optional<std::string> completedAt;
    long long downloadSizeBytes = 0;

    std::string verificationStatus;  // "NotVerified" / "Passed" / "Failed"
    std::string result;              // "Success" / "Failed" / "RolledBack"
    std::optional<std::string> errorMessage;
};

}  // namespace gth::core
