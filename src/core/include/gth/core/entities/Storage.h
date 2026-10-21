#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/StorageStatus.h"
#include "gth/core/enums/StorageType.h"

namespace gth::core {

// A single storage location known to the archive (a local drive, an external
// drive, a NAS share, etc.). Foundational entity for the Storage Manager and
// the future Storage Pool / Game Library modules.
struct Storage {
    std::string storageId = Guid::newGuid();
    std::string name;
    StorageType type = StorageType::Unknown;

    // Root path used to reach this storage (e.g. "D:\", "\\NAS01\Games").
    // Used as the natural key for de-duplicating detected storages.
    std::string rootPath;

    long long totalCapacityBytes = 0;
    long long freeSpaceBytes = 0;
    long long usedSpaceBytes = 0;

    StorageStatus status = StorageStatus::Unknown;
    std::optional<std::string> fileSystem;
    std::optional<std::string> lastScan;
    bool isAvailable = false;

    // Physical-disk health, filled in by an IStorageHealthProvider during a
    // scan when one is available (e.g. WindowsStorageHealthProvider for
    // local drives) - nullopt when no provider ran or the drive doesn't
    // expose the data (network shares, some USB enclosures), never a
    // defaulted 0/100 standing in for "unknown".
    std::optional<int> healthPercent;
    std::optional<int> temperatureCelsius;

    // Set by StorageService when a scan finds the health percent differs
    // from the previous scan's reading, or the temperature exceeds the
    // overheat threshold - nullopt means nothing to flag. Free text rather
    // than a bool so the Storage page can show *why* without the UI having
    // to re-derive it from healthPercent/temperatureCelsius history it
    // doesn't otherwise keep.
    std::optional<std::string> healthAlertReason;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
