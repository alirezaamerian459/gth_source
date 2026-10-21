#pragma once

#include <string>

#include "gth/application/updates/IArchiveUpdater.h"
#include "gth/application/updates/IDownloadManager.h"
#include "gth/application/updates/IUpdateManager.h"
#include "gth/application/updates/RetentionPolicy.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/updates/IUpdateHistoryRepository.h"
#include "gth/core/updates/IUpdateJobRepository.h"
#include "gth/core/updates/IUpdateVerifier.h"

namespace gth::application {

class UpdateManagerService : public IUpdateManager {
public:
    UpdateManagerService(core::IUpdateJobRepository& jobRepository, core::IGameVariantRepository& variantRepository,
                          core::IUpdateHistoryRepository& historyRepository, IDownloadManager& downloadManager,
                          core::IUpdateVerifier& verifier, IArchiveUpdater& archiveUpdater,
                          std::string tempRootDirectory, RetentionPolicy retentionPolicy = RetentionPolicy{});

    UpdateManagerResult processJob(const std::string& updateJobId, core::IUpdateSource& source) override;

private:
    core::IUpdateJobRepository& jobRepository_;
    core::IGameVariantRepository& variantRepository_;
    core::IUpdateHistoryRepository& historyRepository_;
    IDownloadManager& downloadManager_;
    core::IUpdateVerifier& verifier_;
    IArchiveUpdater& archiveUpdater_;
    std::string tempRootDirectory_;
    RetentionPolicy retentionPolicy_;

    void recordHistory(const core::UpdateJob& job, const std::string& verificationStatus, const std::string& result,
                        long long downloadSizeBytes, const std::optional<std::string>& errorMessage);
    void failJob(core::UpdateJob& job, const std::string& errorMessage);
};

}  // namespace gth::application
