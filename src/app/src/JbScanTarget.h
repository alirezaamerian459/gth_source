#pragma once

#include <string_view>

#include "gth/core/entities/Storage.h"

namespace gth::app::jb {

inline bool isLocalDrive(core::StorageType type) {
    return type == core::StorageType::LocalHdd || type == core::StorageType::LocalSsd ||
           type == core::StorageType::ExternalHdd || type == core::StorageType::Usb;
}

inline bool isSelectableStorage(const core::Storage& storage) {
    return storage.isAvailable && isLocalDrive(storage.type);
}

inline bool matchesTarget(const core::Storage& storage, std::string_view targetStorageId) {
    return isSelectableStorage(storage) &&
           (targetStorageId.empty() || storage.storageId == targetStorageId);
}

}  // namespace gth::app::jb
