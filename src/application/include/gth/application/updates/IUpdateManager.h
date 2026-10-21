#pragma once

#include <string>

#include "gth/core/updates/IUpdateSource.h"

namespace gth::application {

struct UpdateManagerResult {
    bool success = false;
    std::string errorMessage;
};

// Drives one Queued UpdateJob through the full pipeline: re-check with the
// source -> download (with retry) -> verify -> replace (atomic, with
// retention) -> UpdateHistory record. Mirrors the specification's Update
// Manager Flow (Section 60) exactly. Any failure along the way leaves the
// archive's current version untouched - see IArchiveUpdater and
// IDownloadManager for how that guarantee is structural rather than a
// promise this class has to keep on its own.
class IUpdateManager {
public:
    virtual ~IUpdateManager() = default;

    virtual UpdateManagerResult processJob(const std::string& updateJobId, core::IUpdateSource& source) = 0;
};

}  // namespace gth::application
