#pragma once

#include "gth/application/reports/IReportService.h"
#include "gth/core/devices/IConsoleRepository.h"
#include "gth/core/index/IGameRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/storage/IStorageRepository.h"
#include "gth/core/transfer/ITransferJobRepository.h"
#include "gth/core/updates/IUpdateInfoRepository.h"
#include "gth/core/updates/IUpdateJobRepository.h"

namespace gth::application {

// Read-only aggregation over already-existing repositories - deliberately
// has no repository/table of its own (see ArchiveHealthReport). Touches
// nothing else in the system, so it can be added without changing any
// existing service's behavior or call sites.
class ReportService : public IReportService {
public:
    ReportService(core::IStorageRepository& storageRepository, core::IGameRepository& gameRepository,
                  core::IGameVariantRepository& gameVariantRepository,
                  core::IGameVersionRepository& gameVersionRepository,
                  core::IUpdateInfoRepository& updateInfoRepository, core::IUpdateJobRepository& updateJobRepository,
                  core::IConsoleRepository& consoleRepository, core::ITransferJobRepository& transferJobRepository)
        : storageRepository_(storageRepository),
          gameRepository_(gameRepository),
          gameVariantRepository_(gameVariantRepository),
          gameVersionRepository_(gameVersionRepository),
          updateInfoRepository_(updateInfoRepository),
          updateJobRepository_(updateJobRepository),
          consoleRepository_(consoleRepository),
          transferJobRepository_(transferJobRepository) {}

    core::reports::ArchiveHealthReport generate() override;

private:
    core::IStorageRepository& storageRepository_;
    core::IGameRepository& gameRepository_;
    core::IGameVariantRepository& gameVariantRepository_;
    core::IGameVersionRepository& gameVersionRepository_;
    core::IUpdateInfoRepository& updateInfoRepository_;
    core::IUpdateJobRepository& updateJobRepository_;
    core::IConsoleRepository& consoleRepository_;
    core::ITransferJobRepository& transferJobRepository_;
};

}  // namespace gth::application
