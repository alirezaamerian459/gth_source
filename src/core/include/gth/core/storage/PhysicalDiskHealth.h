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

    // Selected SMART counters retained in their vendor-reported raw units.  A
    // QNAP report and an ATA pass-through report expose the same identifiers,
    // but vendors may encode the high bytes differently; the UI therefore
    // labels these as raw counters and never converts them into guessed bytes.
    struct SmartMetrics {
        std::optional<std::uint64_t> rawReadErrorRate;          // SMART 1
        std::optional<std::uint64_t> spinUpTime;                // SMART 3
        std::optional<std::uint64_t> startStopCount;             // SMART 4
        std::optional<std::uint64_t> reallocatedSectorCount;     // SMART 5
        std::optional<std::uint64_t> seekErrorRate;             // SMART 7
        std::optional<std::uint64_t> powerOnHours;               // SMART 9
        std::optional<std::uint64_t> spinRetryCount;             // SMART 10
        std::optional<std::uint64_t> powerCycleCount;            // SMART 12
        std::optional<std::uint64_t> endToEndErrorCount;         // SMART 184
        std::optional<std::uint64_t> reportedUncorrectableCount; // SMART 187
        std::optional<std::uint64_t> commandTimeoutCount;        // SMART 188
        std::optional<std::uint64_t> powerOffRetractCount;       // SMART 192
        std::optional<std::uint64_t> loadCycleCount;             // SMART 193
        std::optional<std::uint64_t> pendingSectorCount;         // SMART 197
        std::optional<std::uint64_t> uncorrectableSectorCount;   // SMART 198
        std::optional<std::uint64_t> sataRerrorCount;            // SMART 199
        std::optional<std::uint64_t> multiZoneErrorRate;         // SMART 200
        std::optional<std::uint64_t> airflowTemperatureRaw;      // SMART 190 (vendor-packed)
        std::optional<std::uint64_t> headFlyingHours;            // SMART 240
        std::optional<std::uint64_t> totalLbaWritten;             // SMART 241 (raw/vendor units)
        std::optional<std::uint64_t> totalLbaRead;                // SMART 242 (raw/vendor units)

        bool hasTelemetry() const {
            return rawReadErrorRate || spinUpTime || startStopCount || reallocatedSectorCount || seekErrorRate ||
                   powerOnHours || spinRetryCount || powerCycleCount ||
                   endToEndErrorCount || reportedUncorrectableCount || commandTimeoutCount ||
                   powerOffRetractCount || loadCycleCount || pendingSectorCount ||
                   uncorrectableSectorCount || sataRerrorCount || multiZoneErrorRate ||
                   airflowTemperatureRaw || headFlyingHours || totalLbaWritten || totalLbaRead;
        }
    } smartMetrics;

    // QNAP exposes a compact status alongside the SMART table.  Local ATA
    // providers fill this from the same health assessment; an unavailable
    // source remains empty rather than becoming a false "Good".
    std::string smartStatus;
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
