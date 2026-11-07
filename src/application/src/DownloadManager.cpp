#include "gth/application/updates/DownloadManager.h"

#include <filesystem>
#include <format>

#include "gth/core/Clock.h"

namespace gth::application {

using core::UpdateJob;
using core::UpdateJobStatus;

DownloadManager::DownloadManager(core::IUpdateJobRepository& jobRepository, int maxRetries)
    : jobRepository_(jobRepository), maxRetries_(maxRetries) {}

DownloadOutcome DownloadManager::downloadForJob(const std::string& updateJobId,
                                                 const core::UpdateSourceInfo& sourceInfo, core::IUpdateSource& source,
                                                 const std::string& destinationPath,
                                                 const std::function<void(long long, long long)>& onProgress) {
    DownloadOutcome outcome;

    auto jobOpt = jobRepository_.getById(updateJobId);
    if (!jobOpt.has_value()) {
        outcome.errorMessage = "UpdateJob not found: " + updateJobId;
        return outcome;
    }
    UpdateJob job = *jobOpt;

    job.status = UpdateJobStatus::Preparing;
    job.updatedAt = core::Clock::utcNowIso8601();
    jobRepository_.update(job);

    std::error_code ec;
    std::filesystem::create_directories(destinationPath, ec);
    if (ec) {
        job.status = UpdateJobStatus::Failed;
        job.errorMessage = "Failed to prepare temporary storage: " + ec.message();
        job.updatedAt = core::Clock::utcNowIso8601();
        jobRepository_.update(job);

        outcome.errorMessage = *job.errorMessage;
        return outcome;
    }

    core::DownloadResult lastResult;
    for (int attempt = 1; attempt <= maxRetries_; ++attempt) {
        job.status = UpdateJobStatus::Downloading;
        job.updatedAt = core::Clock::utcNowIso8601();
        jobRepository_.update(job);

        lastResult = source.downloadUpdate(sourceInfo, destinationPath, onProgress);
        if (lastResult.success) {
            job.status = UpdateJobStatus::Downloaded;
            job.updatedAt = core::Clock::utcNowIso8601();
            jobRepository_.update(job);

            outcome.success = true;
            outcome.localPath = lastResult.localPath;
            outcome.attemptsUsed = attempt;
            return outcome;
        }
    }

    // Every attempt failed. The archive's active version was never touched -
    // every write attempt above only ever targeted destinationPath, a
    // temporary location the caller owns.
    job.status = UpdateJobStatus::Failed;
    job.errorMessage =
        lastResult.errorMessage.empty()
            ? std::format("Download failed after {} attempt(s)", maxRetries_)
            : lastResult.errorMessage;
    job.updatedAt = core::Clock::utcNowIso8601();
    jobRepository_.update(job);

    outcome.success = false;
    outcome.errorMessage = *job.errorMessage;
    outcome.attemptsUsed = maxRetries_;
    return outcome;
}

}  // namespace gth::application
