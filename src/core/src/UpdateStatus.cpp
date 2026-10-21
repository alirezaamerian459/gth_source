#include "gth/core/enums/UpdateStatus.h"

namespace gth::core {

std::string toString(UpdateStatus status) {
    switch (status) {
        case UpdateStatus::UpToDate: return "UpToDate";
        case UpdateStatus::UpdateAvailable: return "UpdateAvailable";
        case UpdateStatus::NoSafeMatch: return "NoSafeMatch";
        case UpdateStatus::Unknown:
        default: return "Unknown";
    }
}

UpdateStatus updateStatusFromString(std::string_view text) {
    if (text == "UpToDate") return UpdateStatus::UpToDate;
    if (text == "UpdateAvailable") return UpdateStatus::UpdateAvailable;
    if (text == "NoSafeMatch") return UpdateStatus::NoSafeMatch;
    return UpdateStatus::Unknown;
}

}  // namespace gth::core
