#include "gth/application/updates/UpdateManagerService.h"

#include <filesystem>

#include "gth/core/Clock.h"

namespace gth::application {

namespace fs = std::filesystem;
using core::UpdateHistory;
using core::UpdateJob;
using core::UpdateJobStatus;

UpdateManagerService::UpdateManagerService(core::IUpdateJobRepository& jobRepository,
                                            core::IGameVariantRepository& variantRepository,
                                            core::IUpdateHistoryRepository& historyRepository,
                                            IDownloadManager& downloadManager, core::IUpdateVerifier& verifier,
                                            IArchiveUpdater& archiveUpdater, std::string tempRootDirectory,
                                            RetentionPolicy retentionPolicy)
    : jobRepository_(jobRepository),
      variantRepository_(variantRepository),
      historyRepository_(historyRepository),
      downloadManager_(downloadManager),
      verifier_(verifier),
      archiveUpdater_(archiveUpdater),
      tempRootDirectory_(std::move(tempRootDirectory)),
      retentionPolicy_(retentionPolicy) {}

void UpdateManagerService::recordHistory(const UpdateJob& job, const std::string& verificationStatus,
                                          const std::string& result, long long downloadSizeBytes,
                                          const std::optional<std::string>& errorMessage) {
    UpdateHistory history;
    history.gameVariantId = job.gameVariantId;
    history.previousVersion = job.oldVersion;
    history.newVersion = job.newVersion;
    history.startedAt = job.createdAt;
    history.completedAt = core::Clock::utcNowIso8601();
    history.downloadSizeBytes = downloadSizeBytes;
    history.verificationStatus = verificationStatus;
    history.result = result;
    history.errorMessage = errorMessage;
    historyRepository_.add(history);
}

void UpdateManagerService::failJob(UpdateJob& job, const std::string& errorMessage) {
    job.status = UpdateJobStatus::Failed;
    job.errorMessage = errorMessage;
    job.updatedAt = core::Clock::utcNowIso8601();
    jobRepository_.update(job);
}

UpdateManagerResult UpdateManagerService::processJob(const std::string& updateJobId, core::IUpdateSource& source) {
    UpdateManagerResult result;

    auto jobOpt = jobRepository_.getById(updateJobId);
    if (!jobOpt.has_value()) {
        result.errorMessage = "UpdateJob not found: " + updateJobId;
        return result;
    }
    UpdateJob job = *jobOpt;

    auto variantOpt = variantRepository_.getById(job.gameVariantId);
    if (!variantOpt.has_value()) {
        failJob(job, "GameVariant not found: " + job.gameVariantId);
        result.errorMessage = *job.errorMessage;
        return result;
    }

    // Re-check with the source rather than trusting a possibly-stale caller
    // value - the "Exact Variant Matching" and "no update proceeds on an
    // uncertain match" rules apply right up to the moment we download.
    auto sourceInfoOpt = source.checkForUpdate(*variantOpt, job.oldVersion);
    if (!sourceInfoOpt.has_value() || !sourceInfoOpt->updateAvailable) {
        failJob(job, "Source no longer reports an update available for this variant");
        recordHistory(job, "NotVerified", "Failed", 0, job.errorMessage);
        result.errorMessage = *job.errorMessage;
        return result;
    }

    const fs::path tempPath = fs::path(tempRootDirectory_) / job.updateJobId;

    auto downloadOutcome =
        downloadManager_.downloadForJob(updateJobId, *sourceInfoOpt, source, tempPath.string(), nullptr);
    if (!downloadOutcome.success) {
        // DownloadManager already marked the job Failed with its own error detail.
        recordHistory(job, "NotVerified", "Failed", sourceInfoOpt->updateSizeBytes, downloadOutcome.errorMessage);
        result.errorMessage = downloadOutcome.errorMessage;
        return result;
    }

    job.status = UpdateJobStatus::Verifying;
    job.updatedAt = core::Clock::utcNowIso8601();
    jobRepository_.update(job);

    const std::string verifyPath = downloadOutcome.localPath.empty() ? tempPath.string() : downloadOutcome.localPath;
    auto verification = verifier_.verify(verifyPath, sourceInfoOpt->updateSizeBytes, std::nullopt);
    if (!verification.passed) {
        failJob(job, "Verification failed: " + verification.errorMessage);

        std::error_code ec;
        fs::remove_all(tempPath, ec);  // clean up temp; the archive itself was never touched

        recordHistory(job, "Failed", "Failed", sourceInfoOpt->updateSizeBytes, job.errorMessage);
        result.errorMessage = *job.errorMessage;
        return result;
    }

    job.status = UpdateJobStatus::Verified;
    job.updatedAt = core::Clock::utcNowIso8601();
    jobRepository_.update(job);

    job.status = UpdateJobStatus::Replacing;
    job.updatedAt = core::Clock::utcNowIso8601();
    jobRepository_.update(job);

    auto replacement = archiveUpdater_.applyVerifiedUpdate(updateJobId, tempPath.string(), retentionPolicy_);
    if (!replacement.success) {
        failJob(job, "Replacement failed: " + replacement.errorMessage);
        recordHistory(job, "Passed", "Failed", sourceInfoOpt->updateSizeBytes, job.errorMessage);
        result.errorMessage = *job.errorMessage;
        return result;
    }

    job.status = UpdateJobStatus::Completed;
    job.updatedAt = core::Clock::utcNowIso8601();
    jobRepository_.update(job);

    recordHistory(job, "Passed", "Success", sourceInfoOpt->updateSizeBytes, std::nullopt);
    result.success = true;
    return result;
}

}  // namespace gth::application
