#include "gth/core/enums/StorageStatus.h"

namespace gth::core {

std::string toString(StorageStatus status) {
    switch (status) {
        case StorageStatus::Online: return "Online";
        case StorageStatus::Offline: return "Offline";
        case StorageStatus::Error: return "Error";
        case StorageStatus::Unknown:
        default: return "Unknown";
    }
}

StorageStatus storageStatusFromString(std::string_view text) {
    if (text == "Online") return StorageStatus::Online;
    if (text == "Offline") return StorageStatus::Offline;
    if (text == "Error") return StorageStatus::Error;
    return StorageStatus::Unknown;
}

}  // namespace gth::core
