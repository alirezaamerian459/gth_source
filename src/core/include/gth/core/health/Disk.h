#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/DiskStatus.h"

namespace gth::core::health {

// One physical disk inside a NasDevice (spec Section 38). Health and
// temperature are optional, not defaulted to 0/false values - a provider
// that couldn't retrieve real data must leave these unset (nullopt), which
// the UI renders as "Unknown"/"Unavailable" rather than faking a number
// (spec Sections 4, 58-59, 71 - "Unknown must never become 0%").
struct Disk {
    std::string diskId = Guid::newGuid();
    std::string nasDeviceId;

    std::optional<std::string> serialNumber;
    std::optional<std::string> model;
    std::optional<std::string> manufacturer;
    std::optional<std::string> firmware;
    std::optional<long long> capacityBytes;
    std::optional<std::string> interfaceType;  // e.g. "SATA", "SAS" - "Interface" is a reserved-ish word in C++ contexts
    std::optional<std::string> slot;

    DiskStatus status = DiskStatus::Unknown;
    std::optional<int> healthPercent;      // nullopt = unknown, never 0 as a stand-in
    std::optional<int> temperatureCelsius;  // nullopt = unknown
    std::optional<long long> powerOnHours;

    std::optional<std::string> lastSeen;
    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core::health
