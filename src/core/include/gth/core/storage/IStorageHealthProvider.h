#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/storage/PhysicalDiskHealth.h"
#include "gth/core/storage/StorageHealthInfo.h"

namespace gth::core {

// Looks up the health/temperature of the physical disk(s) backing a storage
// root path. Separate from IStorageDetector (which only observes
// capacity/filesystem/availability) since health lookups use a different,
// slower Windows API (WMI) that not every caller needs - StorageService
// takes this as an optional dependency and simply leaves the health fields
// unset when none is configured.
class IStorageHealthProvider {
public:
    virtual ~IStorageHealthProvider() = default;

    // One reading for the whole storage. For a storage spread over several
    // disks this is the worst health and the highest temperature among them.
    virtual StorageHealthInfo queryHealth(const std::string& rootPath) = 0;

    // Every physical disk behind the storage, each with its own health and
    // temperature. nullopt = this provider can't tell disks apart (callers
    // fall back to queryHealth); an empty list = it can, and found none
    // (network share, unreadable drive, ...).
    virtual std::optional<std::vector<PhysicalDiskHealth>> queryDisks(const std::string& rootPath) {
        (void)rootPath;
        return std::nullopt;
    }
};

}  // namespace gth::core
