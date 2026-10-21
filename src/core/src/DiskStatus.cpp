#include "gth/core/enums/DiskStatus.h"

namespace gth::core {

std::string toString(DiskStatus status) {
    switch (status) {
        case DiskStatus::Healthy: return "Healthy";
        case DiskStatus::Warning: return "Warning";
        case DiskStatus::Critical: return "Critical";
        case DiskStatus::Offline: return "Offline";
        case DiskStatus::Unknown:
        default: return "Unknown";
    }
}

DiskStatus diskStatusFromString(std::string_view text) {
    if (text == "Healthy") return DiskStatus::Healthy;
    if (text == "Warning") return DiskStatus::Warning;
    if (text == "Critical") return DiskStatus::Critical;
    if (text == "Offline") return DiskStatus::Offline;
    return DiskStatus::Unknown;
}

}  // namespace gth::core
