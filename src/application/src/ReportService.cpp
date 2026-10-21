#include "gth/application/reports/ReportService.h"

#include "gth/core/enums/GameVariantStatus.h"
#include "gth/core/enums/StorageStatus.h"
#include "gth/core/enums/TransferStatus.h"
#include "gth/core/enums/UpdateJobStatus.h"
#include "gth/core/enums/UpdateStatus.h"

namespace gth::application {

namespace {
bool isTerminalUpdateJob(core::UpdateJobStatus status) {
    return status == core::UpdateJobStatus::Completed || status == core::UpdateJobStatus::Failed ||
           status == core::UpdateJobStatus::Cancelled || status == core::UpdateJobStatus::RolledBack;
}

bool isTerminalTransferJob(core::TransferStatus status) {
    return status == core::TransferStatus::Completed || status == core::TransferStatus::Failed ||
           status == core::TransferStatus::Cancelled;
}
}  // namespace

core::reports::ArchiveHealthReport ReportService::generate() {
    core::reports::ArchiveHealthReport report;

    for (const auto& storage : storageRepository_.getAll()) {
        report.totalStorages++;
        if (storage.status == core::StorageStatus::Online) {
            report.onlineStorages++;
        } else {
            report.offlineStorages++;
        }
    }

    report.totalGames = static_cast<int>(gameRepository_.getAll().size());

    const auto variants = gameVariantRepository_.getAll();
    for (const auto& variant : variants) {
        report.totalVariants++;
        if (variant.status == core::GameVariantStatus::Missing) report.missingVariants++;

        for (const auto& version : gameVersionRepository_.getByVariantId(variant.gameVariantId)) {
            report.totalVersions++;
            if (version.status == core::GameVariantStatus::Missing) report.missingVersions++;
        }
    }

    for (const auto& info : updateInfoRepository_.getAll()) {
        if (info.status == core::UpdateStatus::UpdateAvailable) report.updatesAvailable++;
    }

    for (const auto& job : updateJobRepository_.getAll()) {
        if (job.status == core::UpdateJobStatus::Failed) {
            report.failedUpdateJobs++;
        } else if (!isTerminalUpdateJob(job.status)) {
            report.pendingUpdateJobs++;
        }
    }

    for (const auto& console : consoleRepository_.getAll()) {
        report.totalConsoles++;
        if (console.status == core::ConsoleStatus::Online) {
            report.onlineConsoles++;
        } else {
            report.offlineConsoles++;
        }
    }

    for (const auto& job : transferJobRepository_.getAll()) {
        if (job.status == core::TransferStatus::Completed) {
            report.completedTransferJobs++;
        } else if (job.status == core::TransferStatus::Failed) {
            report.failedTransferJobs++;
        } else if (!isTerminalTransferJob(job.status)) {
            report.activeTransferJobs++;
        }
    }

    return report;
}

}  // namespace gth::application
