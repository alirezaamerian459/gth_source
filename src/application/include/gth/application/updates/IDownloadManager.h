#pragma once

#include <functional>
#include <string>

#include "gth/application/updates/DownloadOutcome.h"
#include "gth/core/updates/IUpdateSource.h"

namespace gth::application {

// Drives one UpdateJob through Preparing -> Downloading -> Downloaded/Failed,
// retrying on failure, always writing into a temporary destination path -
// never the archive's active version folder. The old archive is guaranteed
// untouched by construction: nothing here ever opens a path outside
// destinationPath.
class IDownloadManager {
public:
    virtual ~IDownloadManager() = default;

    virtual DownloadOutcome downloadForJob(const std::string& updateJobId, const core::UpdateSourceInfo& sourceInfo,
                                            core::IUpdateSource& source, const std::string& destinationPath,
                                            const std::function<void(long long, long long)>& onProgress = nullptr) = 0;
};

}  // namespace gth::application
