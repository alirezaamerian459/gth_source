#pragma once

#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

// One file within a GameVersion's folder. SizeBytes + LastModified together
// form the change-detection fingerprint used for incremental scans (a full
// content hash is deliberately not computed here - that belongs to the
// Update Manager's verification step in a later milestone, where correctness
// matters more than scan speed across a multi-terabyte archive).
struct GameFile {
    std::string gameFileId = Guid::newGuid();
    std::string gameVersionId;

    std::string relativePath;  // path within the version's folder
    long long sizeBytes = 0;
    std::string lastModified;  // ISO-8601 UTC

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
