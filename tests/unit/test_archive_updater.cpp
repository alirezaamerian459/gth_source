#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gth/application/updates/ArchiveUpdater.h"
#include "gth/core/Guid.h"
#include "gth/database/Database.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"
#include "gth/database/UpdateJobRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;
namespace fs = std::filesystem;

namespace {

class ArchiveUpdaterTest : public ::testing::Test {
protected:
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository variantRepository{db};
    GameVersionRepository versionRepository{db};
    UpdateJobRepository jobRepository{db};

    fs::path archiveRoot;
    fs::path tempSourceDir;
    GameVariant variant;

    void SetUp() override {
        Migrator::applyMigrations(db);

        archiveRoot = fs::temp_directory_path() / ("gth_archive_" + Guid::newGuid());
        fs::create_directories(archiveRoot);

        Storage storage;
        storage.rootPath = archiveRoot.string();
        storage.name = "Test Archive";
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

    void TearDown() override {
        fs::remove_all(archiveRoot);
        if (!tempSourceDir.empty()) fs::remove_all(tempSourceDir);
    }

    // Simulates a verified download sitting in a temp directory, ready to be placed.
    fs::path makeVerifiedTempContent(const std::string& fileContent = "update data") {
        tempSourceDir = fs::temp_directory_path() / ("gth_verified_" + Guid::newGuid());
        fs::create_directories(tempSourceDir);
        std::ofstream(tempSourceDir / "data.bin") << fileContent;
        return tempSourceDir;
    }

    std::string addExistingCurrentVersion(const std::string& version) {
        const auto versionPath = fs::path(variant.relativePath) / version;
        fs::create_directories(archiveRoot / versionPath);
        std::ofstream(archiveRoot / versionPath / "data.bin") << "old data";

        GameVersion gv;
        gv.gameVariantId = variant.gameVariantId;
        gv.version = version;
        gv.relativePath = versionPath.string();
        gv.status = GameVariantStatus::Active;
        gv.isCurrent = true;
        versionRepository.add(gv);
        return gv.gameVersionId;
    }

    std::string addJob(const std::string& oldVersion, const std::string& newVersion) {
        UpdateJob job;
        job.gameVariantId = variant.gameVariantId;
        job.oldVersion = oldVersion;
        job.newVersion = newVersion;
        job.status = UpdateJobStatus::Verified;
        jobRepository.add(job);
        return job.updateJobId;
    }

    ArchiveUpdater makeUpdater() {
        return ArchiveUpdater(jobRepository, variantRepository, versionRepository, storageRepository);
    }
};

}  // namespace

TEST_F(ArchiveUpdaterTest, PlacesNewVersionAtTheConventionalPathAndMarksItCurrent) {
    const auto oldId = addExistingCurrentVersion("1.04");
    const auto jobId = addJob("1.04", "1.06");
    const auto tempPath = makeVerifiedTempContent();

    auto updater = makeUpdater();
    auto outcome = updater.applyVerifiedUpdate(jobId, tempPath.string(), RetentionPolicy{1});

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    EXPECT_TRUE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.06\\data.bin"));

    auto versions = versionRepository.getByVariantId(variant.gameVariantId);
    ASSERT_EQ(versions.size(), 2u);

    for (const auto& v : versions) {
        if (v.version == "1.06") {
            EXPECT_TRUE(v.isCurrent);
            EXPECT_EQ(v.status, GameVariantStatus::Active);
        } else {
            EXPECT_EQ(v.version, "1.04");
            EXPECT_FALSE(v.isCurrent);
        }
    }

    // The old version's files are untouched - this is the core atomicity guarantee.
    EXPECT_TRUE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.04\\data.bin"));
    (void)oldId;
}

TEST_F(ArchiveUpdaterTest, KeepPreviousVersionsZeroPurgesOldVersionImmediately) {
    addExistingCurrentVersion("1.04");
    const auto jobId = addJob("1.04", "1.06");
    const auto tempPath = makeVerifiedTempContent();

    auto updater = makeUpdater();
    auto outcome = updater.applyVerifiedUpdate(jobId, tempPath.string(), RetentionPolicy{0});

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;

    // Old version's folder should be gone from disk...
    EXPECT_FALSE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.04"));

    // ...but its row is kept (marked Missing), preserving history rather than deleting it.
    auto versions = versionRepository.getByVariantId(variant.gameVariantId);
    bool foundOld = false;
    for (const auto& v : versions) {
        if (v.version == "1.04") {
            foundOld = true;
            EXPECT_EQ(v.status, GameVariantStatus::Missing);
        }
    }
    EXPECT_TRUE(foundOld);
}

TEST_F(ArchiveUpdaterTest, KeepAllPolicyNeverPurgesAnything) {
    addExistingCurrentVersion("1.04");
    const auto jobId = addJob("1.04", "1.06");
    const auto tempPath = makeVerifiedTempContent();

    auto updater = makeUpdater();
    auto outcome = updater.applyVerifiedUpdate(jobId, tempPath.string(), RetentionPolicy{-1});

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    EXPECT_TRUE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.04\\data.bin"));

    auto versions = versionRepository.getByVariantId(variant.gameVariantId);
    for (const auto& v : versions) {
        if (v.version == "1.04") EXPECT_EQ(v.status, GameVariantStatus::Active);
    }
}

TEST_F(ArchiveUpdaterTest, FailsAndLeavesArchiveUntouchedWhenVerifiedPathIsMissing) {
    addExistingCurrentVersion("1.04");
    const auto jobId = addJob("1.04", "1.06");

    auto updater = makeUpdater();
    auto outcome = updater.applyVerifiedUpdate(jobId, (fs::temp_directory_path() / "does-not-exist").string(),
                                                RetentionPolicy{1});

    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.06"));

    auto versions = versionRepository.getByVariantId(variant.gameVariantId);
    ASSERT_EQ(versions.size(), 1u);
    EXPECT_TRUE(versions[0].isCurrent);  // old version is still current - nothing changed
}

TEST_F(ArchiveUpdaterTest, RefusesToOverwriteAnExistingDestination) {
    addExistingCurrentVersion("1.04");
    const auto jobId = addJob("1.04", "1.06");
    const auto tempPath = makeVerifiedTempContent();

    // Simulate the destination folder already existing (e.g. a previous partial attempt).
    fs::create_directories(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.06");

    auto updater = makeUpdater();
    auto outcome = updater.applyVerifiedUpdate(jobId, tempPath.string(), RetentionPolicy{1});

    EXPECT_FALSE(outcome.success);
    EXPECT_NE(outcome.errorMessage.find("already exists"), std::string::npos);
}

TEST_F(ArchiveUpdaterTest, FailsWhenGameVariantDoesNotExist) {
    UpdateJob job;
    job.gameVariantId = "nonexistent-variant";
    job.oldVersion = "1.00";
    job.newVersion = "1.01";
    jobRepository.add(job);

    const auto tempPath = makeVerifiedTempContent();
    auto updater = makeUpdater();
    auto outcome = updater.applyVerifiedUpdate(job.updateJobId, tempPath.string(), RetentionPolicy{1});

    EXPECT_FALSE(outcome.success);
}
