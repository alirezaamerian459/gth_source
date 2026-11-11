#pragma once

#include <string_view>

#include "gth/core/entities/Storage.h"

namespace gth::app::jb {

inline bool isJbScannableStorage(core::StorageType type) {
    return type == core::StorageType::LocalHdd || type == core::StorageType::LocalSsd ||
           type == core::StorageType::ExternalHdd || type == core::StorageType::Usb ||
           type == core::StorageType::Nas || type == core::StorageType::SmbShare ||
           type == core::StorageType::NetworkShare;
}

inline bool isSelectableStorage(const core::Storage& storage) {
    return storage.isAvailable && isJbScannableStorage(storage.type);
}

inline bool matchesTarget(const core::Storage& storage, std::string_view targetStorageId) {
    return isSelectableStorage(storage) &&
           (targetStorageId.empty() || storage.storageId == targetStorageId);
}

}  // namespace gth::app::jb
