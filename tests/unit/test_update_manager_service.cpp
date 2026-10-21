#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gth/application/updates/ArchiveUpdater.h"
#include "gth/application/updates/DownloadManager.h"
#include "gth/application/updates/UpdateManagerService.h"
#include "gth/core/Guid.h"
#include "gth/database/Database.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"
#include "gth/database/UpdateHistoryRepository.h"
#include "gth/database/UpdateJobRepository.h"
#include "gth/verification/FileVerifier.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;
using namespace gth::verification;
namespace fs = std::filesystem;

namespace {

class ScriptedSource : public IUpdateSource {
public:
    bool updateAvailable = true;
    bool downloadShouldFail = false;
    std::string latestVersion = "1.06";
    std::string content = "real update bytes";

    std::string name() const override { return "Scripted"; }

    std::optional<UpdateSourceInfo> checkForUpdate(const GameVariant&, const std::string&) override {
        UpdateSourceInfo info;
        info.updateAvailable = updateAvailable;
        info.latestVersion = latestVersion;
        info.updateSizeBytes = static_cast<long long>(content.size());
        return info;
    }

    DownloadResult downloadUpdate(const UpdateSourceInfo&, const std::string& destinationPath,
                                   const std::function<void(long long, long long)>&) override {
        if (downloadShouldFail) return DownloadResult{false, "", "simulated download failure"};

        fs::create_directories(destinationPath);
        const auto filePath = fs::path(destinationPath) / "data.bin";
        std::ofstream(filePath) << content;
        return DownloadResult{true, filePath.string(), ""};
    }
};

class UpdateManagerServiceTest : public ::testing::Test {
protected:
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository variantRepository{db};
    GameVersionRepository versionRepository{db};
    UpdateJobRepository jobRepository{db};
    UpdateHistoryRepository historyRepository{db};

    fs::path archiveRoot;
    fs::path tempRoot;
    GameVariant variant;

    ScriptedSource source;
    DownloadManager downloadManager{jobRepository, 3};
    FileVerifier verifier;
    ArchiveUpdater archiveUpdater{jobRepository, variantRepository, versionRepository, storageRepository};

    void SetUp() override {
        Migrator::applyMigrations(db);

        archiveRoot = fs::temp_directory_path() / ("gth_mgr_archive_" + Guid::newGuid());
        tempRoot = fs::temp_directory_path() / ("gth_mgr_temp_" + Guid::newGuid());
        fs::create_directories(archiveRoot);

        Storage storage;
        storage.rootPath = archiveRoot.string();
        storage.name = "Archive";
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

        const auto oldVersionPath = fs::path(variant.relativePath) / "1.04";
        fs::create_directories(archiveRoot / oldVersionPath);
        std::ofstream(archiveRoot / oldVersionPath / "data.bin") << "old bytes";

        GameVersion gv;
        gv.gameVariantId = variant.gameVariantId;
        gv.version = "1.04";
        gv.relativePath = oldVersionPath.string();
        gv.isCurrent = true;
        versionRepository.add(gv);
    }

    void TearDown() override {
        fs::remove_all(archiveRoot);
        fs::remove_all(tempRoot);
    }

    std::string addJob() {
        UpdateJob job;
        job.gameVariantId = variant.gameVariantId;
        job.oldVersion = "1.04";
        job.newVersion = "1.06";
        job.status = UpdateJobStatus::Queued;
        jobRepository.add(job);
        return job.updateJobId;
    }

    UpdateManagerService makeManager() {
        return UpdateManagerService(jobRepository, variantRepository, historyRepository, downloadManager, verifier,
                                     archiveUpdater, tempRoot.string(), RetentionPolicy{1});
    }
};

}  // namespace

TEST_F(UpdateManagerServiceTest, FullPipelineSucceedsAndArchiveEndsUpWithNewCurrentVersion) {
    const auto jobId = addJob();
    auto manager = makeManager();

    auto result = manager.processJob(jobId, source);

    ASSERT_TRUE(result.success) << result.errorMessage;

    auto job = jobRepository.getById(jobId);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, UpdateJobStatus::Completed);

    EXPECT_TRUE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.06\\data.bin"));

    auto versions = versionRepository.getByVariantId(variant.gameVariantId);
    ASSERT_EQ(versions.size(), 2u);
    for (const auto& v : versions) {
        if (v.version == "1.06") EXPECT_TRUE(v.isCurrent);
        if (v.version == "1.04") EXPECT_FALSE(v.isCurrent);
    }

    auto history = historyRepository.getByVariantId(variant.gameVariantId);
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0].result, "Success");
    EXPECT_EQ(history[0].verificationStatus, "Passed");
}

TEST_F(UpdateManagerServiceTest, DownloadFailureLeavesOldVersionCurrentAndRecordsHistory) {
    const auto jobId = addJob();
    source.downloadShouldFail = true;
    auto manager = makeManager();

    auto result = manager.processJob(jobId, source);

    EXPECT_FALSE(result.success);

    auto job = jobRepository.getById(jobId);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, UpdateJobStatus::Failed);

    // Old version is still exactly as it was.
    EXPECT_TRUE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.04\\data.bin"));
    EXPECT_FALSE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.06"));

    auto versions = versionRepository.getByVariantId(variant.gameVariantId);
    ASSERT_EQ(versions.size(), 1u);
    EXPECT_TRUE(versions[0].isCurrent);

    auto history = historyRepository.getByVariantId(variant.gameVariantId);
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0].result, "Failed");
}

TEST_F(UpdateManagerServiceTest, StopsWhenSourceNoLongerReportsUpdateAvailable) {
    const auto jobId = addJob();
    source.updateAvailable = false;  // e.g. someone else already applied it, or it was retracted
    auto manager = makeManager();

    auto result = manager.processJob(jobId, source);

    EXPECT_FALSE(result.success);

    auto job = jobRepository.getById(jobId);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, UpdateJobStatus::Failed);

    EXPECT_FALSE(fs::exists(archiveRoot / "PS5\\Spider-Man 2\\Europe\\1.06"));
}

TEST_F(UpdateManagerServiceTest, ReturnsErrorForNonexistentJob) {
    auto manager = makeManager();
    auto result = manager.processJob("nonexistent-job-id", source);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}
