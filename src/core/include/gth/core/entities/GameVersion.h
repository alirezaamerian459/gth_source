#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/GameVariantStatus.h"

namespace gth::core {

// One version folder found under a GameVariant's container path - the
// archive's equivalent of the spec's "GameVersion" entity (Section 29).
// Multiple versions can coexist on disk at once (e.g. during an atomic
// update, or under a "keep N previous versions" retention policy); IsCurrent
// marks whichever one is the active copy, decided by VersionComparer.
// Reuses GameVariantStatus (Active/Missing) - same "never silently delete"
// rule: a version folder that disappears from a scan is marked Missing, not
// removed.
struct GameVersion {
    std::string gameVersionId = Guid::newGuid();
    std::string gameVariantId;

    std::string version;
    std::string relativePath;  // full path from the storage root
    long long sizeBytes = 0;

    GameVariantStatus status = GameVariantStatus::Active;
    bool isCurrent = false;
    std::optional<std::string> lastScan;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
