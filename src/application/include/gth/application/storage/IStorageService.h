#pragma once

#include <string>
#include <vector>

#include "gth/application/storage/AddStorageResult.h"
#include "gth/application/storage/StorageCategoryResult.h"
#include "gth/application/storage/StorageSummary.h"
#include "gth/application/storage/StorageSyncResult.h"
#include "gth/core/entities/Storage.h"
#include "gth/core/entities/StorageCategory.h"
#include "gth/core/storage/PhysicalDiskHealth.h"

namespace gth::application {

// Application-level orchestration for storage management: listing known
// storages, running detection, and reconciling detected drives with the
// archive database. This is the boundary the UI is expected to call through
// - it never talks to IStorageRepository or IStorageDetector directly.
class IStorageService {
public:
    virtual ~IStorageService() = default;

    virtual std::vector<core::Storage> getAllStorages() = 0;
    virtual std::vector<core::StorageCategory> getCategories() = 0;
    virtual std::vector<std::string> getStorageIdsForCategory(const std::string& categoryId) = 0;

    virtual StorageCategoryResult createCategory(const std::string& name) = 0;
    virtual StorageCategoryResult renameCategory(const std::string& categoryId, const std::string& name) = 0;
    virtual StorageCategoryResult deleteCategory(const std::string& categoryId) = 0;
    virtual StorageCategoryResult addStorageToCategory(const std::string& categoryId,
                                                       const std::string& storageId) = 0;
    virtual StorageCategoryResult removeStorageFromCategory(const std::string& categoryId,
                                                            const std::string& storageId) = 0;

    // Runs detection for locally reachable storages and reconciles the
    // results with the database: new drives are added, previously known
    // drives have their capacity/status/availability refreshed, and drives
    // that are no longer detected are marked offline (never deleted -
    // removing a Storage record is a deliberate user action, not an
    // automatic side effect of a scan).
    virtual StorageSyncResult detectAndSyncLocalStorages() = 0;

    // Registers a storage location the local-drive detector can't discover
    // on its own - a network share (\\server\share) or any other path the
    // user wants indexed. Unlike detectAndSyncLocalStorages(), this never
    // gets silently marked offline by a later local-only scan; removing it
    // is a deliberate user action.
    virtual AddStorageResult addManualStorage(const std::string& path) = 0;

    virtual StorageSummary getSummary() = 0;

    // The physical disks behind a storage, each with its own health and
    // temperature, as of the last detectAndSyncLocalStorages(). A drive letter
    // that spans several disks (spanned/striped volume, Storage Spaces pool)
    // lists all of them. Empty when unknown: not scanned yet, a network share,
    // or no health provider. Never queries the OS itself, so it is cheap to call.
    virtual std::vector<core::PhysicalDiskHealth> getPhysicalDisks(const std::string& rootPath) {
        (void)rootPath;
        return {};
    }
};

}  // namespace gth::application
