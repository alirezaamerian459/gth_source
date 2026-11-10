#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <system_error>
#include <fstream>

#include "gth/application/updates/DownloadManager.h"
#include "gth/core/Guid.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"
#include "gth/database/UpdateJobRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;
namespace fs = std::filesystem;

namespace {

class ScriptedUpdateSource : public IUpdateSource {
public:
    int failuresBeforeSuccess = 0;
    bool alwaysFail = false;
    int callCount = 0;

    std::string name() const override { return "Scripted"; }
    std::optional<UpdateSourceInfo> checkForUpdate(const GameVariant&, const std::string&) override {
        return std::nullopt;
    }

    DownloadResult downloadUpdate(const UpdateSourceInfo&, const std::string& destinationPath,
                                   const std::function<void(long long, long long)>& onProgress) override {
        callCount++;
        if (onProgress) onProgress(50, 100);

        if (alwaysFail || callCount <= failuresBeforeSuccess) {
            return DownloadResult{false, "", "simulated failure"};
        }

        const auto filePath = fs::path(destinationPath) / "update.bin";
        std::ofstream(filePath) << "downloaded content";
        return DownloadResult{true, filePath.string(), ""};
    }
};

struct DownloadFixture {
    Database db{":memory:"};
    UpdateJobRepository jobRepository{db};
    fs::path tempDir;

    DownloadFixture() {
        Migrator::applyMigrations(db);
        tempDir = gth::test::testTempRoot() / ("gth_download_test_" + Guid::newGuid());
    }

    ~DownloadFixture() {
        std::error_code ignored;
        fs::remove_all(tempDir, ignored);
    }

    std::string addJob() {
        UpdateJob job;
        job.gameVariantId = "variant-1";
        job.oldVersion = "1.04";
        job.newVersion = "1.06";
        job.status = UpdateJobStatus::Queued;
        jobRepository.add(job);
        return job.updateJobId;
    }
};

}  // namespace

TEST(DownloadManagerTest, SucceedsOnFirstAttemptAndMarksJobDownloaded) {
    DownloadFixture fixture;
    const auto jobId = fixture.addJob();

    ScriptedUpdateSource source;
    DownloadManager manager(fixture.jobRepository, 3);

    auto outcome = manager.downloadForJob(jobId, UpdateSourceInfo{}, source, fixture.tempDir.string(), nullptr);

    EXPECT_TRUE(outcome.success);
    EXPECT_EQ(outcome.attemptsUsed, 1);
    EXPECT_FALSE(outcome.localPath.empty());
    EXPECT_TRUE(fs::exists(fixture.tempDir / "update.bin"));

    auto job = fixture.jobRepository.getById(jobId);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, UpdateJobStatus::Downloaded);
}

TEST(DownloadManagerTest, RetriesAndEventuallySucceeds) {
    DownloadFixture fixture;
    const auto jobId = fixture.addJob();

    ScriptedUpdateSource source;
    source.failuresBeforeSuccess = 2;  // fails attempts 1 and 2, succeeds on 3
    DownloadManager manager(fixture.jobRepository, 3);

    auto outcome = manager.downloadForJob(jobId, UpdateSourceInfo{}, source, fixture.tempDir.string(), nullptr);

    EXPECT_TRUE(outcome.success);
    EXPECT_EQ(outcome.attemptsUsed, 3);
    EXPECT_EQ(source.callCount, 3);
}

TEST(DownloadManagerTest, StopsAfterMaxRetriesAndMarksJobFailed) {
    DownloadFixture fixture;
    const auto jobId = fixture.addJob();

    ScriptedUpdateSource source;
    source.alwaysFail = true;
    DownloadManager manager(fixture.jobRepository, 3);

    auto outcome = manager.downloadForJob(jobId, UpdateSourceInfo{}, source, fixture.tempDir.string(), nullptr);

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.attemptsUsed, 3);
    EXPECT_EQ(source.callCount, 3);
    EXPECT_FALSE(outcome.errorMessage.empty());

    auto job = fixture.jobRepository.getById(jobId);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, UpdateJobStatus::Failed);
}

TEST(DownloadManagerTest, InvokesProgressCallback) {
    DownloadFixture fixture;
    const auto jobId = fixture.addJob();

    ScriptedUpdateSource source;
    DownloadManager manager(fixture.jobRepository, 3);

    long long lastBytes = -1, lastTotal = -1;
    manager.downloadForJob(jobId, UpdateSourceInfo{}, source, fixture.tempDir.string(),
                            [&](long long bytes, long long total) {
                                lastBytes = bytes;
                                lastTotal = total;
                            });

    EXPECT_EQ(lastBytes, 50);
    EXPECT_EQ(lastTotal, 100);
}

TEST(DownloadManagerTest, ReturnsErrorWhenJobDoesNotExist) {
    DownloadFixture fixture;
    ScriptedUpdateSource source;
    DownloadManager manager(fixture.jobRepository, 3);

    auto outcome = manager.downloadForJob("nonexistent", UpdateSourceInfo{}, source, fixture.tempDir.string(),
                                           nullptr);

    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.errorMessage.empty());
}
