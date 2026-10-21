#pragma once

#include <string>

#include "gth/core/Clock.h"

namespace gth::core::reports {

// A point-in-time snapshot of archive/system health, assembled by reading
// already-persisted repository state - nothing here is itself persisted,
// since it's cheap to recompute on demand and would otherwise just be a
// stale copy of data the repositories already own.
struct ArchiveHealthReport {
    std::string generatedAt = Clock::utcNowIso8601();

    int totalStorages = 0;
    int onlineStorages = 0;
    int offlineStorages = 0;

    int totalGames = 0;
    int totalVariants = 0;
    int missingVariants = 0;
    int totalVersions = 0;
    int missingVersions = 0;

    int updatesAvailable = 0;
    int pendingUpdateJobs = 0;
    int failedUpdateJobs = 0;

    int totalConsoles = 0;
    int onlineConsoles = 0;
    int offlineConsoles = 0;

    int activeTransferJobs = 0;
    int completedTransferJobs = 0;
    int failedTransferJobs = 0;
};

}  // namespace gth::core::reports
