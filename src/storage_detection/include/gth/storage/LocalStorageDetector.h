#pragma once

#include "gth/core/storage/IStorageDetector.h"

namespace gth::storage {

// Detects storages reachable as local Windows drive letters: fixed disks,
// removable/USB media, drives already mapped as network shares, and reachable
// UNC directory shortcuts on the user's Desktop.
// HDD vs. SSD is not distinguished yet (that needs a physical-disk media-type
// lookup via IOCTL_STORAGE_QUERY_PROPERTY, deferred until it's actually
// needed) - fixed disks are reported as StorageType::LocalHdd and can be
// reclassified by the user or a future detector without any change to
// callers.
class LocalStorageDetector : public core::IStorageDetector {
public:
    std::vector<core::DetectedStorageInfo> detect() override;
};

}  // namespace gth::storage
