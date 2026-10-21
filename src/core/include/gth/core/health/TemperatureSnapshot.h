#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core::health {

// One point-in-time temperature reading for a Disk (spec Section 40).
struct TemperatureSnapshot {
    std::string temperatureSnapshotId = Guid::newGuid();
    std::string diskId;

    std::optional<int> temperatureCelsius;

    std::string capturedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core::health
