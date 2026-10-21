#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Mirrors the specification's transfer state model (Section 38) exactly.
enum class TransferStatus {
    Queued = 0,
    Preparing = 1,
    Transferring = 2,
    Paused = 3,
    Verifying = 4,
    Completed = 5,
    Failed = 6,
    Cancelled = 7
};

std::string toString(TransferStatus status);
TransferStatus transferStatusFromString(std::string_view text);

}  // namespace gth::core
