#include "gth/application/updates/UpdateDetectorService.h"

#include "gth/core/Clock.h"
#include "gth/core/enums/GameVariantStatus.h"

namespace gth::application {

using core::UpdateInfo;
using core::UpdateStatus;

UpdateDetectorService::UpdateDetectorService(core::IGameVariantRepository& variantRepository,
                                              core::IGameVersionRepository& versionRepository,
                                              core::IUpdateInfoRepository& updateInfoRepository,
                                              std::vector<core::IUpdateSource*> sources)
    : variantRepository_(variantRepository),
      versionRepository_(versionRepository),
      updateInfoRepository_(updateInfoRepository),
      sources_(std::move(sources)) {}

std::string UpdateDetectorService::findCurrentVersion(const std::string& gameVariantId) {
    for (const auto& version : versionRepository_.getByVariantId(gameVariantId)) {
        if (version.isCurrent) return version.version;
    }
    return "";
}

UpdateInfo UpdateDetectorService::detectForVariant(const core::GameVariant& variant) {
    UpdateInfo info;
    info.gameVariantId = variant.gameVariantId;
    info.currentVersion = findCurrentVersion(variant.gameVariantId);
    info.checkedAt = core::Clock::utcNowIso8601();

    if (info.currentVersion.empty()) {
        // Nothing indexed as current for this variant - no safe basis for comparison.
        info.latestVersion.clear();
        info.status = UpdateStatus::NoSafeMatch;
        updateInfoRepository_.upsert(info);
        return info;
    }

    for (auto* source : sources_) {
        auto result = source->checkForUpdate(variant, info.currentVersion);
        if (!result.has_value()) continue;  // this source has no information about this variant

        info.source = source->name();
        info.latestVersion = result->updateAvailable ? result->latestVersion : info.currentVersion;
        info.updateSizeBytes = result->updateSizeBytes;
        info.metadata = result->metadata;
        info.status = result->updateAvailable ? UpdateStatus::UpdateAvailable : UpdateStatus::UpToDate;
        updateInfoRepository_.upsert(info);
        return info;
    }

    // No configured source had anything to say about this variant.
    info.latestVersion = info.currentVersion;
    info.status = UpdateStatus::Unknown;
    updateInfoRepository_.upsert(info);
    return info;
}

std::vector<UpdateInfo> UpdateDetectorService::detectForAllVariants() {
    std::vector<UpdateInfo> results;
    for (const auto& variant : variantRepository_.getAll()) {
        if (variant.status != core::GameVariantStatus::Active) continue;
        results.push_back(detectForVariant(variant));
    }
    return results;
}

}  // namespace gth::application
