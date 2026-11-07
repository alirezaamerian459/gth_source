#pragma once

#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

// A user-defined label for grouping known storage locations. A storage can
// belong to more than one category; the relationship is persisted separately.
struct StorageCategory {
    std::string categoryId = Guid::newGuid();
    std::string name;
    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
