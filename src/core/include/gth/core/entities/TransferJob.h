#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/TransferStatus.h"

namespace gth::core {

// A named batch of TransferItems (e.g. "Copy Spider-Man 2 to NAS02"). The
// job's own Status reflects the aggregate of its items - see
// TransferQueueService for how that's derived.
struct TransferJob {
    std::string transferJobId = Guid::newGuid();
    std::string name;
    TransferStatus status = TransferStatus::Queued;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
    std::optional<std::string> completedAt;
};

}  // namespace gth::core
