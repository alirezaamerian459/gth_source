#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/GameVariantStatus.h"

namespace gth::core {

// One region/platform instance of a Game, located at a specific container
// path on a specific Storage - stable across version updates. The set of
// versions that have ever existed under it (1.00, 1.02, 1.04, 1.06, ...) are
// separate GameVersion rows; a version bump changes which GameVersion is
// current, not the GameVariant's own identity.
//
// Matching priority for reconciling a scan result against an existing row is
// TitleId > ContentId > Platform+Region > folder-name metadata - see
// IArchivePathResolver for how the folder-convention fallback is parsed when
// no real platform metadata parser is plugged in yet.
struct GameVariant {
    std::string gameVariantId = Guid::newGuid();
    std::string gameId;

    std::string platform;
    std::string region;

    std::string storageId;
    std::string relativePath;  // container path from the storage root: {Platform}/{Title}/{Region}

    std::optional<std::string> titleId;
    std::optional<std::string> contentId;

    GameVariantStatus status = GameVariantStatus::Active;
    std::optional<std::string> lastScan;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
