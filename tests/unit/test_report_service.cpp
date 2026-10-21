#include <gtest/gtest.h>

#include "gth/application/reports/ReportService.h"
#include "gth/database/ConsoleRepository.h"
#include "gth/database/Database.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"
#include "gth/database/TransferJobRepository.h"
#include "gth/database/UpdateInfoRepository.h"
#include "gth/database/UpdateJobRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

struct ReportFixture {
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository gameVariantRepository{db};
    GameVersionRepository gameVersionRepository{db};
    UpdateInfoRepository updateInfoRepository{db};
    UpdateJobRepository updateJobRepository{db};
    ConsoleRepository consoleRepository{db};
    TransferJobRepository transferJobRepository{db};
    ReportService service{storageRepository,   gameRepository,     gameVariantRepository, gameVersionRepository,
                           updateInfoRepository, updateJobRepository, consoleRepository,     transferJobRepository};

    ReportFixture() { Migrator::applyMigrations(db); }

    std::string addStorage(StorageStatus status) {
        Storage storage;
        storage.rootPath = "D:\\" + Guid::newGuid();
        storage.status = status;
        storageRepository.add(storage);
        return storage.storageId;
    }

    std::string addVariant(const std::string& storageId, GameVariantStatus status) {
        Game game;
        game.title = "Game " + Guid::newGuid();
        gameRepository.add(game);

        GameVariant variant;
        variant.gameId = game.gameId;
        variant.storageId = storageId;
        variant.relativePath = "PS5/Title-" + Guid::newGuid() + "/EU";
        variant.status = status;
        gameVariantRepository.add(variant);
        return variant.gameVariantId;
    }

    void addVersion(const std::string& variantId, GameVariantStatus status) {
        GameVersion version;
        version.gameVariantId = variantId;
        version.relativePath = "version-" + Guid::newGuid();
        version.status = status;
        gameVersionRepository.add(version);
    }
};

}  // namespace

TEST(ReportServiceTest, EmptyDatabaseReportsAllZeros) {
    ReportFixture fixture;
    auto report = fixture.service.generate();

    EXPECT_EQ(report.totalStorages, 0);
    EXPECT_EQ(report.totalGames, 0);
    EXPECT_EQ(report.totalVariants, 0);
    EXPECT_EQ(report.totalVersions, 0);
    EXPECT_EQ(report.updatesAvailable, 0);
    EXPECT_EQ(report.totalConsoles, 0);
    EXPECT_EQ(report.activeTransferJobs, 0);
}

TEST(ReportServiceTest, CountsStoragesOnlineAndOffline) {
    ReportFixture fixture;
    fixture.addStorage(StorageStatus::Online);
    fixture.addStorage(StorageStatus::Online);
    fixture.addStorage(StorageStatus::Offline);

    auto report = fixture.service.generate();

    EXPECT_EQ(report.totalStorages, 3);
    EXPECT_EQ(report.onlineStorages, 2);
    EXPECT_EQ(report.offlineStorages, 1);
}

TEST(ReportServiceTest, CountsVariantsAndVersionsIncludingMissing) {
    ReportFixture fixture;
    const auto storageId = fixture.addStorage(StorageStatus::Online);
    const auto activeVariant = fixture.addVariant(storageId, GameVariantStatus::Active);
    fixture.addVariant(storageId, GameVariantStatus::Missing);

    fixture.addVersion(activeVariant, GameVariantStatus::Active);
    fixture.addVersion(activeVariant, GameVariantStatus::Missing);

    auto report = fixture.service.generate();

    EXPECT_EQ(report.totalGames, 2);
    EXPECT_EQ(report.totalVariants, 2);
    EXPECT_EQ(report.missingVariants, 1);
    EXPECT_EQ(report.totalVersions, 2);
    EXPECT_EQ(report.missingVersions, 1);
}

TEST(ReportServiceTest, CountsUpdatesAvailableAndJobsByStatus) {
    ReportFixture fixture;
    const auto storageId = fixture.addStorage(StorageStatus::Online);
    const auto variantId = fixture.addVariant(storageId, GameVariantStatus::Active);

    UpdateInfo info;
    info.gameVariantId = variantId;
    info.status = UpdateStatus::UpdateAvailable;
    fixture.updateInfoRepository.upsert(info);

    UpdateJob pendingJob;
    pendingJob.gameVariantId = variantId;
    pendingJob.status = UpdateJobStatus::Downloading;
    fixture.updateJobRepository.add(pendingJob);

    UpdateJob failedJob;
    failedJob.gameVariantId = variantId;
    failedJob.status = UpdateJobStatus::Failed;
    fixture.updateJobRepository.add(failedJob);

    UpdateJob completedJob;
    completedJob.gameVariantId = variantId;
    completedJob.status = UpdateJobStatus::Completed;
    fixture.updateJobRepository.add(completedJob);

    auto report = fixture.service.generate();

    EXPECT_EQ(report.updatesAvailable, 1);
    EXPECT_EQ(report.pendingUpdateJobs, 1);
    EXPECT_EQ(report.failedUpdateJobs, 1);
}

TEST(ReportServiceTest, CountsConsolesOnlineAndOffline) {
    ReportFixture fixture;
    Console online;
    online.status = ConsoleStatus::Online;
    fixture.consoleRepository.add(online);

    Console offline;
    offline.status = ConsoleStatus::Offline;
    fixture.consoleRepository.add(offline);

    Console unknown;
    unknown.status = ConsoleStatus::Unknown;
    fixture.consoleRepository.add(unknown);

    auto report = fixture.service.generate();

    EXPECT_EQ(report.totalConsoles, 3);
    EXPECT_EQ(report.onlineConsoles, 1);
    EXPECT_EQ(report.offlineConsoles, 2);
}

TEST(ReportServiceTest, CountsTransferJobsByStatus) {
    ReportFixture fixture;
    TransferJob active;
    active.status = TransferStatus::Transferring;
    fixture.transferJobRepository.add(active);

    TransferJob completed;
    completed.status = TransferStatus::Completed;
    fixture.transferJobRepository.add(completed);

    TransferJob failed;
    failed.status = TransferStatus::Failed;
    fixture.transferJobRepository.add(failed);

    auto report = fixture.service.generate();

    EXPECT_EQ(report.activeTransferJobs, 1);
    EXPECT_EQ(report.completedTransferJobs, 1);
    EXPECT_EQ(report.failedTransferJobs, 1);
}
