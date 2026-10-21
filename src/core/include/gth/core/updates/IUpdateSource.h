#pragma once

#include <functional>
#include <optional>
#include <string>

#include "gth/core/entities/GameVariant.h"
#include "gth/core/updates/UpdateSourceTypes.h"

namespace gth::core {

// Abstraction boundary for wherever update metadata/files come from.
//
// This project never bypasses authentication, DRM, or licensing to reach an
// update. An implementation of this interface must only use sources the
// user is legitimately authorized to access (their own maintained metadata,
// an official API they hold credentials for, ...). If no such access is
// available for a given platform/service, the right answer is to NOT
// implement a source for it - not to fake one.
class IUpdateSource {
public:
    virtual ~IUpdateSource() = default;

    // Identifies this source in UpdateInfo.source and in logs.
    virtual std::string name() const = 0;

    // currentVersion is the variant's IsCurrent GameVersion.version. Returns
    // nullopt if this source has no information about this variant at all
    // (not the same as "no update available" - that's
    // UpdateSourceInfo::updateAvailable = false).
    virtual std::optional<UpdateSourceInfo> checkForUpdate(const GameVariant& variant,
                                                             const std::string& currentVersion) = 0;

    // Fetches the update's files to destinationPath, a temporary directory
    // the Download Manager owns (never the archive's active version folder -
    // see the spec's Atomic Archive Update rule). onProgress, if non-null,
    // may be called zero or more times with (bytesDownloaded, totalBytes) -
    // sources that can't report progress (or that fail immediately, like
    // LocalMetadataSource) are not required to call it at all.
    virtual DownloadResult downloadUpdate(const UpdateSourceInfo& update, const std::string& destinationPath,
                                           const std::function<void(long long, long long)>& onProgress) = 0;
};

}  // namespace gth::core
