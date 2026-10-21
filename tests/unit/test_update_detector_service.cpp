#include <gtest/gtest.h>

#include "gth/application/updates/UpdateDetectorService.h"
#include "gth/database/Database.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"
#include "gth/database/UpdateInfoRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

class FakeUpdateSource : public IUpdateSource {
public:
    std::string sourceName = "FakeSource";
    std::optional<UpdateSourceInfo> response;

    std::string name() const override { return sourceName; }
    std::optional<UpdateSourceInfo> checkForUpdate(const GameVariant&, const std::string&) override {
        return response;
    }
    DownloadResult downloadUpdate(const UpdateSourceInfo&, const std::string&,
                                   const std::function<void(long long, long long)>&) override {
        return DownloadResult{};
    }
};

struct DetectorFixture {
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository variantRepository{db};
    GameVersionRepository versionRepository{db};
    UpdateInfoRepository updateInfoRepository{db};
    FakeUpdateSource source;

    GameVariant variant;

    DetectorFixture() {
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
    }

    void addCurrentVersion(const std::string& version) {
        GameVersion gv;
        gv.gameVariantId = variant.gameVariantId;
        gv.version = version;
        gv.relativePath = variant.relativePath + "\\" + version;
        gv.isCurrent = true;
        versionRepository.add(gv);
    }

    UpdateDetectorService makeDetector() {
        return UpdateDetectorService(variantRepository, versionRepository, updateInfoRepository,
                                      std::vector<IUpdateSource*>{&source});
    }
};

}  // namespace

TEST(UpdateDetectorServiceTest, ReportsUpdateAvailableWhenLatestIsNewer) {
    DetectorFixture fixture;
    fixture.addCurrentVersion("1.04");
    fixture.source.response = UpdateSourceInfo{true, "1.06", 18'400'000'000, std::nullopt};

    auto detector = fixture.makeDetector();
    auto info = detector.detectForVariant(fixture.variant);

    EXPECT_EQ(info.status, UpdateStatus::UpdateAvailable);
    EXPECT_EQ(info.currentVersion, "1.04");
    EXPECT_EQ(info.latestVersion, "1.06");
    EXPECT_EQ(info.source, "FakeSource");
}

TEST(UpdateDetectorServiceTest, ReportsUpToDateWhenVersionsMatch) {
    DetectorFixture fixture;
    fixture.addCurrentVersion("1.06");
    fixture.source.response = UpdateSourceInfo{false, "1.06", 0, std::nullopt};

    auto detector = fixture.makeDetector();
    auto info = detector.detectForVariant(fixture.variant);

    EXPECT_EQ(info.status, UpdateStatus::UpToDate);
}

TEST(UpdateDetectorServiceTest, ReportsNoSafeMatchWhenVariantHasNoCurrentVersion) {
    DetectorFixture fixture;
    // No addCurrentVersion() call - variant has no indexed version at all.
    fixture.source.response = UpdateSourceInfo{true, "1.06", 0, std::nullopt};

    auto detector = fixture.makeDetector();
    auto info = detector.detectForVariant(fixture.variant);

    EXPECT_EQ(info.status, UpdateStatus::NoSafeMatch);
}

TEST(UpdateDetectorServiceTest, ReportsUnknownWhenNoSourceHasInformation) {
    DetectorFixture fixture;
    fixture.addCurrentVersion("1.04");
    fixture.source.response = std::nullopt;  // source has nothing to say about this variant

    auto detector = fixture.makeDetector();
    auto info = detector.detectForVariant(fixture.variant);

    EXPECT_EQ(info.status, UpdateStatus::Unknown);
}

TEST(UpdateDetectorServiceTest, PersistsResultSoItCanBeReReadLater) {
    DetectorFixture fixture;
    fixture.addCurrentVersion("1.04");
    fixture.source.response = UpdateSourceInfo{true, "1.06", 100, std::nullopt};

    auto detector = fixture.makeDetector();
    detector.detectForVariant(fixture.variant);

    auto persisted = fixture.updateInfoRepository.getByVariantId(fixture.variant.gameVariantId);
    ASSERT_TRUE(persisted.has_value());
    EXPECT_EQ(persisted->status, UpdateStatus::UpdateAvailable);
}

TEST(UpdateDetectorServiceTest, ReCheckOverwritesPreviousSnapshotInsteadOfAccumulating) {
    DetectorFixture fixture;
    fixture.addCurrentVersion("1.04");
    fixture.source.response = UpdateSourceInfo{true, "1.05", 0, std::nullopt};

    auto detector = fixture.makeDetector();
    detector.detectForVariant(fixture.variant);

    fixture.source.response = UpdateSourceInfo{true, "1.06", 0, std::nullopt};
    detector.detectForVariant(fixture.variant);

    auto all = fixture.updateInfoRepository.getAll();
    EXPECT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].latestVersion, "1.06");
}
