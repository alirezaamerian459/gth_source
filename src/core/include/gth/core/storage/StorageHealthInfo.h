#pragma once

#include <optional>
#include <string>

namespace gth::core {

// Result of a health lookup for one storage's underlying physical disk.
// Both fields are nullopt when the provider couldn't determine a real
// value (no provider configured, drive doesn't expose SMART/reliability
// data, it's a network share, etc.) - never defaulted to 0, matching the
// same "Unknown must never become 0%" rule used by the NAS Health Monitor
// module's Disk entity.
struct StorageHealthInfo {
    std::optional<int> healthPercent;
    std::optional<int> temperatureCelsius;
    // Why the health isn't 100 ("Reallocated sectors: 12"); nullopt when nothing is wrong or it isn't known.
    std::optional<std::string> healthDetail;
};

}  // namespace gth::core
