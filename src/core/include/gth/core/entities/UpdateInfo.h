#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/UpdateStatus.h"

namespace gth::core {

// Latest known-update snapshot for a GameVariant, as reported by one
// IUpdateSource. Re-checking overwrites this row (it's a snapshot, not a
// history - UpdateHistory owns the record of updates actually applied).
struct UpdateInfo {
    std::string updateInfoId = Guid::newGuid();
    std::string gameVariantId;

    std::string currentVersion;
    std::string latestVersion;
    long long updateSizeBytes = 0;
    std::string source;  // IUpdateSource::name() that produced this info

    std::string detectedAt = Clock::utcNowIso8601();
    std::string checkedAt = Clock::utcNowIso8601();
    UpdateStatus status = UpdateStatus::Unknown;
    std::optional<std::string> metadata;
};

}  // namespace gth::core
