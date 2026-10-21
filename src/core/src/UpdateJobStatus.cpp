#include "gth/core/enums/UpdateJobStatus.h"

namespace gth::core {

std::string toString(UpdateJobStatus status) {
    switch (status) {
        case UpdateJobStatus::Detected: return "Detected";
        case UpdateJobStatus::Queued: return "Queued";
        case UpdateJobStatus::Preparing: return "Preparing";
        case UpdateJobStatus::Downloading: return "Downloading";
        case UpdateJobStatus::Downloaded: return "Downloaded";
        case UpdateJobStatus::Verifying: return "Verifying";
        case UpdateJobStatus::Verified: return "Verified";
        case UpdateJobStatus::Replacing: return "Replacing";
        case UpdateJobStatus::Completed: return "Completed";
        case UpdateJobStatus::Failed: return "Failed";
        case UpdateJobStatus::Cancelled: return "Cancelled";
        case UpdateJobStatus::RolledBack: return "RolledBack";
        default: return "Detected";
    }
}

UpdateJobStatus updateJobStatusFromString(std::string_view text) {
    if (text == "Detected") return UpdateJobStatus::Detected;
    if (text == "Queued") return UpdateJobStatus::Queued;
    if (text == "Preparing") return UpdateJobStatus::Preparing;
    if (text == "Downloading") return UpdateJobStatus::Downloading;
    if (text == "Downloaded") return UpdateJobStatus::Downloaded;
    if (text == "Verifying") return UpdateJobStatus::Verifying;
    if (text == "Verified") return UpdateJobStatus::Verified;
    if (text == "Replacing") return UpdateJobStatus::Replacing;
    if (text == "Completed") return UpdateJobStatus::Completed;
    if (text == "Failed") return UpdateJobStatus::Failed;
    if (text == "Cancelled") return UpdateJobStatus::Cancelled;
    if (text == "RolledBack") return UpdateJobStatus::RolledBack;
    return UpdateJobStatus::Detected;
}

}  // namespace gth::core
