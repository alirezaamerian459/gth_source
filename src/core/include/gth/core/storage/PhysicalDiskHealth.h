#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gth/core/storage/StorageHealthInfo.h"

namespace gth::core {

// Above this a drive is flagged, whatever its own health status says (SMART status lags real thermal risk).
inline constexpr int kOverheatThresholdCelsius = 55;

// A health estimate below this ("Good" ends at 70) raises an alert on the storage, with the reason. A steady reading
// above it doesn't: a few remapped sectors on an old disk are normal, and every slightly worn SSD turning red would
// bury the alerts that matter.
inline constexpr int kHealthAlertBelowPercent = 70;

// One physical disk behind a storage. A storage is usually one disk, but a
// spanned/striped volume or a Storage Spaces pool presents several disks as a
// single drive letter - each of those disks has its own health and temperature.
// Unknown values stay nullopt (never defaulted to 0), same rule as StorageHealthInfo.
struct PhysicalDiskHealth {
    int diskNumber = -1;  // Windows disk number: \\.\PhysicalDrive<N>
    std::string model;
    std::uint64_t sizeBytes = 0;  // 0 = unknown
    std::optional<int> healthPercent;
    std::optional<int> temperatureCelsius;
    std::string healthDetail;  // why healthPercent is below 100; empty when it isn't
};

// One storage-level reading out of several disks: only as healthy as the worst
// disk whose health is known, and as hot as the hottest disk whose temperature
// is known. Disks with an unknown value are ignored (not counted as 0).
inline StorageHealthInfo summarizeDisks(const std::vector<PhysicalDiskHealth>& disks) {
    StorageHealthInfo summary;
    for (const auto& disk : disks) {
        if (disk.healthPercent.has_value() &&
            (!summary.healthPercent.has_value() || *disk.healthPercent < *summary.healthPercent)) {
            summary.healthPercent = disk.healthPercent;
            if (!disk.healthDetail.empty()) {
                summary.healthDetail = disk.healthDetail;
            } else {
                summary.healthDetail.reset();
            }
        }
        if (disk.temperatureCelsius.has_value() &&
            (!summary.temperatureCelsius.has_value() || *disk.temperatureCelsius > *summary.temperatureCelsius)) {
            summary.temperatureCelsius = disk.temperatureCelsius;
        }
    }
    return summary;
}

}  // namespace gth::core
