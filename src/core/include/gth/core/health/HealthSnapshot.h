#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core::health {

// One point-in-time health reading for a Disk (spec Section 39), kept
// permanently to build history/trend charts later. `source` distinguishes
// a health percentage the provider itself reported ("Provider") from one
// this app computed from other signals ("Calculated") - the spec (Section
// 10) requires the UI to be honest about which one it's showing, never
// presenting a calculated estimate as if it were the disk's real SMART
// health.
struct HealthSnapshot {
    std::string healthSnapshotId = Guid::newGuid();
    std::string diskId;

    std::optional<int> healthPercent;
    std::string source;  // "Provider" | "Calculated"
    std::optional<std::string> reason;

    std::string capturedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core::health
