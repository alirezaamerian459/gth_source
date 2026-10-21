#pragma once

#include "gth/application/indexing/IGameIndexer.h"
#include "gth/core/index/IGameFileRepository.h"
#include "gth/core/index/IGameRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/index/IScanJobRepository.h"
#include "gth/core/index/IStorageScanner.h"
#include "gth/core/storage/IStorageRepository.h"

namespace gth::application {

class GameIndexerService : public IGameIndexer {
public:
    GameIndexerService(core::IStorageRepository& storageRepository, core::IStorageScanner& scanner,
                        core::IGameRepository& gameRepository, core::IGameVariantRepository& variantRepository,
                        core::IGameVersionRepository& versionRepository, core::IGameFileRepository& fileRepository,
                        core::IScanJobRepository& scanJobRepository);

    ScanResult scanStorage(const std::string& storageId) override;
    SetVersionResult setVersion(const std::string& gameVariantId, const std::string& version) override;
    ImportVersionsResult importVersions(const std::string& text) override;

private:
    core::IStorageRepository& storageRepository_;
    core::IStorageScanner& scanner_;
    core::IGameRepository& gameRepository_;
    core::IGameVariantRepository& variantRepository_;
    core::IGameVersionRepository& versionRepository_;
    core::IGameFileRepository& fileRepository_;
    core::IScanJobRepository& scanJobRepository_;

    void reconcileVersions(const core::GameVariant& variant, const std::vector<core::ScannedVersion>& scannedVersions,
                            ScanResult& result);
    void markAllVersionsMissing(const std::string& gameVariantId, ScanResult& result);

    // Returns true if anything about the version's file list actually changed.
    bool reconcileFiles(const core::GameVersion& version, const std::vector<core::ScannedFile>& scannedFiles);
};

}  // namespace gth::application
