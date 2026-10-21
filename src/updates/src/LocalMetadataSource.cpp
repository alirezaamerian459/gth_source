#include "gth/updates/LocalMetadataSource.h"

#include <fstream>
#include <sstream>

#include "gth/core/index/VersionComparer.h"

namespace gth::updates {

LocalMetadataSource::LocalMetadataSource(std::string manifestPath) : manifestPath_(std::move(manifestPath)) {}

namespace {
std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}
}  // namespace

void LocalMetadataSource::reload() {
    entriesByRelativePath_.clear();
    loaded_ = true;

    std::ifstream file(manifestPath_);
    if (!file.is_open()) return;  // no manifest yet is not an error - just no known updates

    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::stringstream ss(trimmed);
        std::string relativePath, latestVersion, sizeText;
        if (!std::getline(ss, relativePath, '\t')) continue;
        if (!std::getline(ss, latestVersion, '\t')) continue;
        std::getline(ss, sizeText, '\t');  // optional

        ManifestEntry entry;
        entry.latestVersion = trim(latestVersion);
        try {
            entry.sizeBytes = sizeText.empty() ? 0 : std::stoll(trim(sizeText));
        } catch (...) {
            entry.sizeBytes = 0;
        }

        entriesByRelativePath_[trim(relativePath)] = entry;
    }
}

void LocalMetadataSource::ensureLoaded() {
    if (!loaded_) reload();
}

std::optional<core::UpdateSourceInfo> LocalMetadataSource::checkForUpdate(const core::GameVariant& variant,
                                                                            const std::string& currentVersion) {
    ensureLoaded();

    auto it = entriesByRelativePath_.find(variant.relativePath);
    if (it == entriesByRelativePath_.end()) return std::nullopt;

    core::UpdateSourceInfo info;
    info.latestVersion = it->second.latestVersion;
    info.updateSizeBytes = it->second.sizeBytes;
    info.updateAvailable = core::compareVersions(currentVersion, info.latestVersion) < 0;
    return info;
}

core::DownloadResult LocalMetadataSource::downloadUpdate(const core::UpdateSourceInfo&, const std::string&,
                                                           const std::function<void(long long, long long)>&) {
    core::DownloadResult result;
    result.success = false;
    result.errorMessage =
        "LocalMetadataSource only reports known update versions - it has no files to download. "
        "A future authorized provider (see IUpdateSource) would implement this.";
    return result;
}

}  // namespace gth::updates
