#pragma once

#include "gth/application/updates/IArchiveUpdater.h"
#include "gth/core/entities/GameVariant.h"
#include "gth/core/entities/Storage.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/storage/IStorageRepository.h"
#include "gth/core/updates/IUpdateJobRepository.h"

namespace gth::application {

class ArchiveUpdater : public IArchiveUpdater {
public:
    ArchiveUpdater(core::IUpdateJobRepository& jobRepository, core::IGameVariantRepository& variantRepository,
                   core::IGameVersionRepository& versionRepository, core::IStorageRepository& storageRepository);

    ReplacementOutcome applyVerifiedUpdate(const std::string& updateJobId, const std::string& verifiedPath,
                                            const RetentionPolicy& policy) override;

private:
    core::IUpdateJobRepository& jobRepository_;
    core::IGameVariantRepository& variantRepository_;
    core::IGameVersionRepository& versionRepository_;
    core::IStorageRepository& storageRepository_;

    void applyRetention(const core::GameVariant& variant, const core::Storage& storage,
                         const RetentionPolicy& policy);
};

}  // namespace gth::application
