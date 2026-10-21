#pragma once

#include <vector>

#include "gth/core/storage/DetectedStorageInfo.h"

namespace gth::core {

// Discovers storage locations that are physically/logically reachable from
// this machine (local drives today; future implementations may probe NAS or
// network shares). Detectors only observe the environment - reconciling
// results against the archive database is the job of the application layer.
class IStorageDetector {
public:
    virtual ~IStorageDetector() = default;
    virtual std::vector<DetectedStorageInfo> detect() = 0;
};

}  // namespace gth::core
