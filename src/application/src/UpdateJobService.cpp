#include "gth/application/updates/UpdateJobService.h"

namespace gth::application {

using core::UpdateJob;

UpdateJobService::UpdateJobService(core::IUpdateJobRepository& jobRepository) : jobRepository_(jobRepository) {}

UpdateJob UpdateJobService::queueUpdate(const std::string& gameVariantId, const std::string& oldVersion,
                                         const std::string& newVersion) {
    if (auto existing = jobRepository_.getActiveJobForVariant(gameVariantId, newVersion); existing.has_value()) {
        return *existing;
    }

    UpdateJob job;
    job.gameVariantId = gameVariantId;
    job.oldVersion = oldVersion;
    job.newVersion = newVersion;
    job.status = core::UpdateJobStatus::Queued;
    jobRepository_.add(job);
    return job;
}

}  // namespace gth::application
