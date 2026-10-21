#pragma once

#include <optional>
#include <string>

namespace gth::core {

// What an IUpdateSource reports back for one GameVariant.
struct UpdateSourceInfo {
    bool updateAvailable = false;
    std::string latestVersion;
    long long updateSizeBytes = 0;
    std::optional<std::string> metadata;
};

// Result of IUpdateSource::downloadUpdate. Not every source can actually
// deliver bytes (e.g. a metadata-only source that just reports version
// numbers) - such sources should return success=false with a clear
// errorMessage rather than pretending to support downloads.
struct DownloadResult {
    bool success = false;
    std::string localPath;
    std::string errorMessage;
};

}  // namespace gth::core
