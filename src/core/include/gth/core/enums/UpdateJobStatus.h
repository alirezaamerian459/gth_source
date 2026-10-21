#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Mirrors the spec's UpdateJob status flow exactly (Section 11):
// Detected -> Queued -> Preparing -> Downloading -> Downloaded -> Verifying
// -> Verified -> Replacing -> Completed, with Failed/Cancelled/RolledBack as
// exits from any in-progress state.
enum class UpdateJobStatus {
    Detected = 0,
    Queued = 1,
    Preparing = 2,
    Downloading = 3,
    Downloaded = 4,
    Verifying = 5,
    Verified = 6,
    Replacing = 7,
    Completed = 8,
    Failed = 9,
    Cancelled = 10,
    RolledBack = 11
};

std::string toString(UpdateJobStatus status);
UpdateJobStatus updateJobStatusFromString(std::string_view text);

}  // namespace gth::core
