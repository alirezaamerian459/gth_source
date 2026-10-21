#include "gth/core/enums/StorageType.h"

#include <stdexcept>

namespace gth::core {

std::string toString(StorageType type) {
    switch (type) {
        case StorageType::LocalHdd: return "LocalHdd";
        case StorageType::LocalSsd: return "LocalSsd";
        case StorageType::ExternalHdd: return "ExternalHdd";
        case StorageType::Usb: return "Usb";
        case StorageType::Nas: return "Nas";
        case StorageType::SmbShare: return "SmbShare";
        case StorageType::NetworkShare: return "NetworkShare";
        case StorageType::Unknown:
        default: return "Unknown";
    }
}

StorageType storageTypeFromString(std::string_view text) {
    if (text == "LocalHdd") return StorageType::LocalHdd;
    if (text == "LocalSsd") return StorageType::LocalSsd;
    if (text == "ExternalHdd") return StorageType::ExternalHdd;
    if (text == "Usb") return StorageType::Usb;
    if (text == "Nas") return StorageType::Nas;
    if (text == "SmbShare") return StorageType::SmbShare;
    if (text == "NetworkShare") return StorageType::NetworkShare;
    return StorageType::Unknown;
}

}  // namespace gth::core
