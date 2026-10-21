#pragma once

#include "gth/application/updates/IDownloadManager.h"
#include "gth/core/updates/IUpdateJobRepository.h"

namespace gth::application {

class DownloadManager : public IDownloadManager {
public:
    explicit DownloadManager(core::IUpdateJobRepository& jobRepository, int maxRetries = 3);

    DownloadOutcome downloadForJob(const std::string& updateJobId, const core::UpdateSourceInfo& sourceInfo,
                                    core::IUpdateSource& source, const std::string& destinationPath,
                                    const std::function<void(long long, long long)>& onProgress) override;

private:
    core::IUpdateJobRepository& jobRepository_;
    int maxRetries_;
};

}  // namespace gth::application
