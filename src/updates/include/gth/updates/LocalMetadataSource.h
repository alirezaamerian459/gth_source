#pragma once

#include <string>
#include <unordered_map>

#include "gth/core/updates/IUpdateSource.h"

namespace gth::updates {

// A legitimate, always-available IUpdateSource: reads a plain-text manifest
// the archive owner maintains themselves (not scraped, not fetched from any
// service requiring authentication this project doesn't have) mapping a
// variant's folder path to the latest version they know about. This is one
// of the example providers the specification names explicitly
// (Section 5's "LocalMetadataSource") - useful today, and a template for
// how a future authorized network provider would plug into the same
// interface without any change to the detection/update pipeline around it.
//
// Manifest format: one entry per line, tab-separated:
//   RelativePath<TAB>LatestVersion<TAB>SizeBytes
// RelativePath matches GameVariant::relativePath exactly (the
// {Platform}/{Title}/{Region} folder path from the storage root). Blank
// lines and lines starting with '#' are ignored.
class LocalMetadataSource : public core::IUpdateSource {
public:
    explicit LocalMetadataSource(std::string manifestPath);

    std::string name() const override { return "LocalMetadataSource"; }

    std::optional<core::UpdateSourceInfo> checkForUpdate(const core::GameVariant& variant,
                                                           const std::string& currentVersion) override;

    core::DownloadResult downloadUpdate(const core::UpdateSourceInfo& update, const std::string& destinationPath,
                                         const std::function<void(long long, long long)>& onProgress) override;

    // Re-reads the manifest file. Automatically called once on first use;
    // exposed so a caller can pick up manifest edits without restarting.
    void reload();

private:
    struct ManifestEntry {
        std::string latestVersion;
        long long sizeBytes = 0;
    };

    std::string manifestPath_;
    std::unordered_map<std::string, ManifestEntry> entriesByRelativePath_;
    bool loaded_ = false;

    void ensureLoaded();
};

}  // namespace gth::updates
