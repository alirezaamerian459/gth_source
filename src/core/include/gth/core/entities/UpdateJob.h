#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/UpdateJobStatus.h"

namespace gth::core {

// One queued/in-progress/completed update operation for a GameVariant.
// Milestone 4 only creates and tracks these (Detected/Queued); the
// Download/Verification/Replacement machinery that advances a job through
// the rest of its states is later milestones' work.
struct UpdateJob {
    std::string updateJobId = Guid::newGuid();
    std::string gameVariantId;

    std::string oldVersion;
    std::string newVersion;
    UpdateJobStatus status = UpdateJobStatus::Detected;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
    std::optional<std::string> errorMessage;
};

}  // namespace gth::core
