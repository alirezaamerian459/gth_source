#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "gth/application/storage/ILocalFileIndex.h"

namespace gth::core {
class IStorageRepository;
class IGameVariantRepository;
class IGameVersionRepository;
class IGameFileRepository;
}  // namespace gth::core

namespace gth::application {

// ILocalFileIndex over what the storage scanner already recorded: every file of every Active
// version of every Active variant (not just each game's "current" version - a console asking for
// an older patch's package should still be served if that package is on disk).
//
// The scanner stores a file's path in one of two shapes (relative to the storage root for the
// .pkg scanner, relative to the version folder for the folder-convention scanner), so each row
// yields a few candidate paths and the first one that really exists wins.
//
// The name -> candidates map is built lazily and rebuilt (at most every few seconds) when a lookup
// misses, so a library scan finished while the proxy is running is picked up without a restart.
class LocalFileIndexService : public ILocalFileIndex {
public:
    LocalFileIndexService(core::IStorageRepository& storageRepo, core::IGameVariantRepository& variantRepo,
                          core::IGameVersionRepository& versionRepo, core::IGameFileRepository& fileRepo);

    std::optional<std::filesystem::path> findByFileName(const std::string& fileName) override;

private:
    struct Index {
        std::unordered_map<std::string, std::vector<std::filesystem::path>> byName;
        std::chrono::steady_clock::time_point builtAt;
    };

    std::shared_ptr<const Index> snapshot() const;
    std::shared_ptr<const Index> rebuild(const std::shared_ptr<const Index>& seen);

    core::IStorageRepository& storageRepo_;
    core::IGameVariantRepository& variantRepo_;
    core::IGameVersionRepository& versionRepo_;
    core::IGameFileRepository& fileRepo_;

    mutable std::mutex mutex_;  // guards snapshot_
    std::mutex buildMutex_;     // one rebuild at a time
    std::shared_ptr<const Index> snapshot_;
};

}  // namespace gth::application
