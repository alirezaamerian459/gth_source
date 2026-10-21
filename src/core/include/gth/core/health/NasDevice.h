#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/NasStatus.h"

namespace gth::core::health {

// A NAS the user has manually registered (spec Section 7 - Manual NAS).
// Deliberately no username/password fields yet: this milestone has no
// provider that actually authenticates against anything, and per Section
// 80 credentials must never be stored plain-text - adding the fields
// before there's a Credential-Manager-backed place to put them would just
// invite that mistake later (the same reasoning that kept FTP transfer
// out of Game Transfer Hub's Milestone 11).
struct NasDevice {
    std::string nasDeviceId = Guid::newGuid();
    std::string name;
    std::string ipAddress;
    int port = 0;

    NasStatus status = NasStatus::Unreachable;
    std::optional<std::string> lastSeen;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core::health
