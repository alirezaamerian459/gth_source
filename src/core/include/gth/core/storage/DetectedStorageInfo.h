#pragma once

#include <optional>
#include <string>

#include "gth/core/enums/StorageType.h"

namespace gth::core {

// Result of a raw storage detection pass (e.g. enumerating local drives),
// before it has been reconciled against the database. Kept separate from
// the Storage entity so detectors never depend on persistence concerns.
struct DetectedStorageInfo {
    std::string name;
    StorageType type = StorageType::Unknown;
    std::string rootPath;
    long long totalCapacityBytes = 0;
    long long freeSpaceBytes = 0;
    std::optional<std::string> fileSystem;
    bool isAvailable = false;
};

}  // namespace gth::core
