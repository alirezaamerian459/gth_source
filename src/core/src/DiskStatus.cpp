#include "gth/core/enums/DiskStatus.h"

namespace gth::core {

std::string toString(DiskStatus status) {
    using enum DiskStatus;
    switch (status) {
        case Healthy: return "Healthy";
        case Warning: return "Warning";
        case Critical: return "Critical";
        case Offline: return "Offline";
        case Unknown:
        default: return "Unknown";
    }
}

DiskStatus diskStatusFromString(std::string_view text) {
    using enum DiskStatus;
    if (text == "Healthy") return Healthy;
    if (text == "Warning") return Warning;
    if (text == "Critical") return Critical;
    if (text == "Offline") return Offline;
    return Unknown;
}

}  // namespace gth::core
