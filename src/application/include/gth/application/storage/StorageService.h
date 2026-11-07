#pragma once

#include <map>
#include <mutex>

#include "gth/application/storage/IStorageService.h"
#include "gth/core/storage/IStorageDetector.h"
#include "gth/core/storage/IStorageHealthProvider.h"
#include "gth/core/storage/IStorageRepository.h"

namespace gth::application {

class StorageService : public IStorageService {
public:
    // healthProvider is optional (nullptr by default) - when absent, every
    // synced Storage simply keeps its health fields unset rather than a
    // fabricated value. Kept as a raw pointer rather than a reference since
    // "no provider configured" is a legitimate, common case (tests, or a
    // platform without one), not an error.
    StorageService(core::IStorageRepository& repository, core::IStorageDetector& detector,
                   core::IStorageHealthProvider* healthProvider = nullptr);

    std::vector<core::Storage> getAllStorages() override;
    std::vector<core::StorageCategory> getCategories() override;
    std::vector<std::string> getStorageIdsForCategory(const std::string& categoryId) override;
    StorageCategoryResult createCategory(const std::string& name) override;
    StorageCategoryResult renameCategory(const std::string& categoryId, const std::string& name) override;
    StorageCategoryResult deleteCategory(const std::string& categoryId) override;
    StorageCategoryResult addStorageToCategory(const std::string& categoryId,
                                               const std::string& storageId) override;
    StorageCategoryResult removeStorageFromCategory(const std::string& categoryId,
                                                    const std::string& storageId) override;
    StorageSyncResult detectAndSyncLocalStorages() override;
    AddStorageResult addManualStorage(const std::string& path) override;
    StorageSummary getSummary() override;
    std::vector<core::PhysicalDiskHealth> getPhysicalDisks(const std::string& rootPath) override;

private:
    void rememberDisks(const std::string& normalisedRoot, std::vector<core::PhysicalDiskHealth> disks);

    core::IStorageRepository& repository_;
    core::IStorageDetector& detector_;
    core::IStorageHealthProvider* healthProvider_;

    // Per-disk readings from the last scan, by normalised root path. The scan runs on a
    // background thread while the UI reads, hence the mutex.
    std::mutex disksMutex_;
    std::map<std::string, std::vector<core::PhysicalDiskHealth>> disksByRoot_;
};

}  // namespace gth::application
