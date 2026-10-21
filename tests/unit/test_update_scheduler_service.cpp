#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "gth/application/updates/UpdateDetectorService.h"
#include "gth/application/updates/UpdateJobService.h"
#include "gth/application/updates/UpdateSchedulerService.h"
#include "gth/database/Database.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"
#include "gth/database/UpdateInfoRepository.h"
#include "gth/database/UpdateJobRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

class FakeUpdateSource : public IUpdateSource {
public:
    std::optional<UpdateSourceInfo> response;
    std::string name() const override { return "Fake"; }
    std::optional<UpdateSourceInfo> checkForUpdate(const GameVariant&, const std::string&) override {
        return response;
    }
    DownloadResult downloadUpdate(const UpdateSourceInfo&, const std::string&,
                                   const std::function<void(long long, long long)>&) override {
        return DownloadResult{};
    }
};

struct SchedulerFixture {
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository variantRepository{db};
    GameVersionRepository versionRepository{db};
    UpdateInfoRepository updateInfoRepository{db};
    UpdateJobRepository jobRepository{db};
    FakeUpdateSource source;

    GameVariant variant;

    SchedulerFixture() {
        Migrator::applyMigrations(db);

        Storage storage;
        storage.rootPath = "D:\\Games";
        storage.name = "Games";
        storageRepository.add(storage);

        Game game;
        game.title = "Spider-Man 2";
        gameRepository.add(game);

        variant.gameId = game.gameId;
        variant.platform = "PS5";
        variant.region = "Europe";
        variant.storageId = storage.storageId;
        variant.relativePath = "PS5\\Spider-Man 2\\Europe";
        variantRepository.add(variant);

        GameVersion gv;
        gv.gameVariantId = variant.gameVariantId;
        gv.version = "1.04";
        gv.relativePath = variant.relativePath + "\\1.04";
        gv.isCurrent = true;
        versionRepository.add(gv);
    }
};

}  // namespace

TEST(UpdateSchedulerServiceTest, CheckNowQueuesJobForAvailableUpdate) {
    SchedulerFixture fixture;
    fixture.source.response = UpdateSourceInfo{true, "1.06", 100, std::nullopt};

    UpdateDetectorService detector(fixture.variantRepository, fixture.versionRepository,
                                    fixture.updateInfoRepository, std::vector<IUpdateSource*>{&fixture.source});
    UpdateJobService jobService(fixture.jobRepository);
    UpdateSchedulerService scheduler(detector, jobService, std::chrono::hours(6));

    scheduler.checkNow();

    auto jobs = fixture.jobRepository.getByVariantId(fixture.variant.gameVariantId);
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs[0].newVersion, "1.06");
    EXPECT_EQ(jobs[0].status, UpdateJobStatus::Queued);
}

TEST(UpdateSchedulerServiceTest, CheckNowDoesNotQueueWhenAlreadyUpToDate) {
    SchedulerFixture fixture;
    fixture.source.response = UpdateSourceInfo{false, "1.04", 0, std::nullopt};

    UpdateDetectorService detector(fixture.variantRepository, fixture.versionRepository,
                                    fixture.updateInfoRepository, std::vector<IUpdateSource*>{&fixture.source});
    UpdateJobService jobService(fixture.jobRepository);
    UpdateSchedulerService scheduler(detector, jobService, std::chrono::hours(6));

    scheduler.checkNow();

    EXPECT_TRUE(fixture.jobRepository.getByVariantId(fixture.variant.gameVariantId).empty());
}

TEST(UpdateSchedulerServiceTest, RepeatedCheckNowDoesNotDuplicateJobs) {
    SchedulerFixture fixture;
    fixture.source.response = UpdateSourceInfo{true, "1.06", 100, std::nullopt};

    UpdateDetectorService detector(fixture.variantRepository, fixture.versionRepository,
                                    fixture.updateInfoRepository, std::vector<IUpdateSource*>{&fixture.source});
    UpdateJobService jobService(fixture.jobRepository);
    UpdateSchedulerService scheduler(detector, jobService, std::chrono::hours(6));

    scheduler.checkNow();
    scheduler.checkNow();
    scheduler.checkNow();

    EXPECT_EQ(fixture.jobRepository.getByVariantId(fixture.variant.gameVariantId).size(), 1u);
}

TEST(UpdateSchedulerServiceTest, BackgroundTimerFiresAndQueuesJob) {
    SchedulerFixture fixture;
    fixture.source.response = UpdateSourceInfo{true, "1.06", 100, std::nullopt};

    UpdateDetectorService detector(fixture.variantRepository, fixture.versionRepository,
                                    fixture.updateInfoRepository, std::vector<IUpdateSource*>{&fixture.source});
    UpdateJobService jobService(fixture.jobRepository);
    UpdateSchedulerService scheduler(detector, jobService, std::chrono::milliseconds(20));

    EXPECT_FALSE(scheduler.isRunning());
    scheduler.start();
    EXPECT_TRUE(scheduler.isRunning());

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    scheduler.stop();
    EXPECT_FALSE(scheduler.isRunning());

    EXPECT_FALSE(fixture.jobRepository.getByVariantId(fixture.variant.gameVariantId).empty());
}

TEST(UpdateSchedulerServiceTest, StopReturnsQuicklyEvenWithALongInterval) {
    SchedulerFixture fixture;
    UpdateDetectorService detector(fixture.variantRepository, fixture.versionRepository,
                                    fixture.updateInfoRepository, std::vector<IUpdateSource*>{&fixture.source});
    UpdateJobService jobService(fixture.jobRepository);
    UpdateSchedulerService scheduler(detector, jobService, std::chrono::hours(6));

    scheduler.start();
    const auto before = std::chrono::steady_clock::now();
    scheduler.stop();
    const auto elapsed = std::chrono::steady_clock::now() - before;

    EXPECT_LT(elapsed, std::chrono::seconds(1));
}
