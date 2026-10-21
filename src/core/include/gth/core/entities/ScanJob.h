#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/ScanJobStatus.h"

namespace gth::core {

// Record of one indexing pass over a Storage - when it ran, what it found,
// and whether it succeeded. Mirrors the same "track background operations
// as job rows" pattern the Update Manager uses for UpdateJob.
struct ScanJob {
    std::string scanJobId = Guid::newGuid();
    std::string storageId;

    ScanJobStatus status = ScanJobStatus::Running;
    std::string startedAt = Clock::utcNowIso8601();
    std::optional<std::string> completedAt;

    int gamesAdded = 0;
    int variantsAdded = 0;
    int versionsAdded = 0;
    int versionsUpdated = 0;
    int versionsMissing = 0;
    int versionsUnchanged = 0;

    std::optional<std::string> errorMessage;
};

}  // namespace gth::core
