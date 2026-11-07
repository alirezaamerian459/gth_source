#include "gth/application/updates/ArchiveUpdater.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "gth/core/Clock.h"
#include "gth/core/index/VersionComparer.h"

namespace gth::application {

namespace fs = std::filesystem;
using core::GameVariant;
using core::GameVariantStatus;
using core::GameVersion;
using core::Storage;
using core::UpdateJob;

ArchiveUpdater::ArchiveUpdater(core::IUpdateJobRepository& jobRepository,
                                core::IGameVariantRepository& variantRepository,
                                core::IGameVersionRepository& versionRepository,
                                core::IStorageRepository& storageRepository)
    : jobRepository_(jobRepository),
      variantRepository_(variantRepository),
      versionRepository_(versionRepository),
      storageRepository_(storageRepository) {}

ReplacementOutcome ArchiveUpdater::applyVerifiedUpdate(const std::string& updateJobId,
                                                        const std::string& verifiedPath,
                                                        const RetentionPolicy& policy) {
    ReplacementOutcome outcome;

    auto jobOpt = jobRepository_.getById(updateJobId);
    if (!jobOpt.has_value()) {
        outcome.errorMessage = "UpdateJob not found: " + updateJobId;
        return outcome;
    }
    const UpdateJob job = *jobOpt;

    auto variantOpt = variantRepository_.getById(job.gameVariantId);
    if (!variantOpt.has_value()) {
        outcome.errorMessage = "GameVariant not found: " + job.gameVariantId;
        return outcome;
    }
    const GameVariant variant = *variantOpt;

    auto storageOpt = storageRepository_.getById(variant.storageId);
    if (!storageOpt.has_value()) {
        outcome.errorMessage = "Storage not found: " + variant.storageId;
        return outcome;
    }
    const Storage storage = *storageOpt;

    // --- Safety checks (Section 53) - nothing is written to the archive until all of these pass. ---
    std::error_code ec;
    if (!fs::exists(verifiedPath, ec)) {
        outcome.errorMessage = "Verified content missing at: " + verifiedPath;
        return outcome;
    }

    long long requiredBytes = 0;
    for (const auto& entry :
         fs::recursive_directory_iterator(verifiedPath, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec)) requiredBytes += static_cast<long long>(entry.file_size(ec));
    }

    const auto spaceInfo = fs::space(fs::path(storage.rootPath), ec);
    if (!ec && static_cast<long long>(spaceInfo.available) < requiredBytes) {
        outcome.errorMessage = "Not enough free space on '" + storage.rootPath + "': need " +
                                std::to_string(requiredBytes) + " bytes, have " +
                                std::to_string(spaceInfo.available) + " bytes";
        return outcome;
    }

    const fs::path destinationPath = fs::path(storage.rootPath) / variant.relativePath / job.newVersion;
    if (fs::exists(destinationPath, ec)) {
        outcome.errorMessage = "Destination already exists, refusing to overwrite: " + destinationPath.string();
        return outcome;
    }

    // --- Place the new version. This is the only step that touches the archive, and it only ever
    // creates a new folder - the current version's folder is never opened. ---
    fs::create_directories(destinationPath.parent_path(), ec);
    fs::rename(verifiedPath, destinationPath, ec);
    if (ec) {
        // rename() fails across volumes/drives - fall back to copy, then clean up the source.
        ec.clear();
        fs::copy(verifiedPath, destinationPath, fs::copy_options::recursive, ec);
        if (ec) {
            outcome.errorMessage = "Failed to place new version at '" + destinationPath.string() + "': " + ec.message();
            return outcome;
        }
        fs::remove_all(verifiedPath, ec);  // best-effort temp cleanup; failure here doesn't undo the copy
    }

    // --- Only now, with the new version safely on disk, update the database. ---
    for (auto version : versionRepository_.getByVariantId(variant.gameVariantId)) {
        if (!version.isCurrent) continue;
        version.isCurrent = false;
        version.updatedAt = core::Clock::utcNowIso8601();
        versionRepository_.update(version);
    }

    GameVersion newVersion;
    newVersion.gameVariantId = variant.gameVariantId;
    newVersion.version = job.newVersion;
    newVersion.relativePath = (fs::path(variant.relativePath) / job.newVersion).string();
    newVersion.sizeBytes = requiredBytes;
    newVersion.status = GameVariantStatus::Active;
    newVersion.isCurrent = true;
    newVersion.lastScan = core::Clock::utcNowIso8601();
    versionRepository_.add(newVersion);

    applyRetention(variant, storage, policy);

    outcome.success = true;
    return outcome;
}

void ArchiveUpdater::applyRetention(const GameVariant& variant, const Storage& storage,
                                     const RetentionPolicy& policy) {
    if (policy.keepPreviousVersions < 0) return;  // keep all - nothing to purge

    std::vector<GameVersion> previousActive;
    for (const auto& version : versionRepository_.getByVariantId(variant.gameVariantId)) {
        if (!version.isCurrent && version.status == GameVariantStatus::Active) previousActive.push_back(version);
    }

    // Newest previous version first, so the ones we keep are the most recent.
    std::sort(previousActive.begin(), previousActive.end(), [](const GameVersion& a, const GameVersion& b) {
        return core::compareVersions(a.version, b.version) > 0;
    });

    for (size_t i = static_cast<size_t>(policy.keepPreviousVersions); i < previousActive.size(); ++i) {
        auto old = previousActive[i];

        std::error_code ec;
        fs::remove_all(fs::path(storage.rootPath) / old.relativePath, ec);

        // Mark it Missing regardless of whether the filesystem removal fully
        // succeeded - the database must never claim a version is Active
        // when a purge was just attempted against it, and the version's
        // history (spec Section 10) is preserved rather than deleting the row.
        old.status = GameVariantStatus::Missing;
        old.isCurrent = false;
        old.updatedAt = core::Clock::utcNowIso8601();
        versionRepository_.update(old);
    }
}

}  // namespace gth::application
