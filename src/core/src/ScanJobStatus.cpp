#include "gth/core/enums/ScanJobStatus.h"

namespace gth::core {

std::string toString(ScanJobStatus status) {
    switch (status) {
        case ScanJobStatus::Running: return "Running";
        case ScanJobStatus::Completed: return "Completed";
        case ScanJobStatus::Failed: return "Failed";
        case ScanJobStatus::Unknown:
        default: return "Unknown";
    }
}

ScanJobStatus scanJobStatusFromString(std::string_view text) {
    if (text == "Running") return ScanJobStatus::Running;
    if (text == "Completed") return ScanJobStatus::Completed;
    if (text == "Failed") return ScanJobStatus::Failed;
    return ScanJobStatus::Unknown;
}

}  // namespace gth::core
