#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "gth/application/transfer/TransferQueueService.h"
#include "gth/core/Guid.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"
#include "gth/database/TransferItemRepository.h"
#include "gth/database/TransferJobRepository.h"
#include "gth/transfer/LocalFileTransferProvider.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;
using namespace gth::transfer;
namespace fs = std::filesystem;

namespace {

class ScriptedTransferProvider : public ITransferProvider {
public:
    bool shouldFail = false;
    bool blockUntilCancelled = false;

    std::string name() const override { return "Scripted"; }

    TransferOutcome copyFile(const std::string&, const std::string&,
                              const std::function<void(long long, long long)>& onProgress,
                              std::atomic<bool>* cancelFlag) override {
        if (onProgress) onProgress(50, 100);

        if (blockUntilCancelled) {
            while (cancelFlag && !cancelFlag->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return TransferOutcome{false, "Cancelled"};
        }

        if (shouldFail) return TransferOutcome{false, "simulated failure"};
        return TransferOutcome{true, ""};
    }
};

// Waits (with a hard timeout so a bug can't hang the test suite forever)
// until predicate() is true.
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

}  // namespace

TEST(TransferQueueServiceTest, CopiesRealFilesAndMarksJobCompleted) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    TransferJobRepository jobRepository(db);
    TransferItemRepository itemRepository(db);
    LocalFileTransferProvider provider;

    const auto srcDir = gth::test::testTempRoot() / ("gth_xfer_src_" + Guid::newGuid());
    const auto dstDir = gth::test::testTempRoot() / ("gth_xfer_dst_" + Guid::newGuid());
    fs::create_directories(srcDir);
    std::ofstream(srcDir / "game.bin") << "some game data";

    TransferQueueService queue(jobRepository, itemRepository, provider, 2);
    queue.start();

    auto job = queue.enqueueJob("Copy to NAS", {{(srcDir / "game.bin").string(), (dstDir / "game.bin").string()}});

    ASSERT_TRUE(waitFor([&] {
        auto j = jobRepository.getById(job.transferJobId);
        return j && j->status == TransferStatus::Completed;
    }));

    EXPECT_TRUE(fs::exists(dstDir / "game.bin"));

    auto items = queue.getItemsForJob(job.transferJobId);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].status, TransferStatus::Completed);
    EXPECT_EQ(items[0].bytesTransferred, items[0].sizeBytes);

    queue.stop();
    fs::remove_all(srcDir);
    fs::remove_all(dstDir);
}

TEST(TransferQueueServiceTest, FailedCopyMarksItemAndJobFailed) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    TransferJobRepository jobRepository(db);
    TransferItemRepository itemRepository(db);
    ScriptedTransferProvider provider;
    provider.shouldFail = true;

    TransferQueueService queue(jobRepository, itemRepository, provider, 1);
    queue.start();

    auto job = queue.enqueueJob("Bad copy", {{"C:\\src.bin", "C:\\dst.bin"}});

    ASSERT_TRUE(waitFor([&] {
        auto j = jobRepository.getById(job.transferJobId);
        return j && j->status == TransferStatus::Failed;
    }));

    auto items = queue.getItemsForJob(job.transferJobId);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].status, TransferStatus::Failed);
    EXPECT_TRUE(items[0].errorMessage.has_value());

    queue.stop();
}

TEST(TransferQueueServiceTest, CancelItemStopsItAndMarksCancelled) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    TransferJobRepository jobRepository(db);
    TransferItemRepository itemRepository(db);
    ScriptedTransferProvider provider;
    provider.blockUntilCancelled = true;

    TransferQueueService queue(jobRepository, itemRepository, provider, 1);
    queue.start();

    auto job = queue.enqueueJob("Slow copy", {{"C:\\src.bin", "C:\\dst.bin"}});

    // Give the worker a moment to pick the item up and start "transferring".
    ASSERT_TRUE(waitFor([&] {
        auto items = queue.getItemsForJob(job.transferJobId);
        return !items.empty() && items[0].status == TransferStatus::Transferring;
    }));

    auto items = queue.getItemsForJob(job.transferJobId);
    queue.cancelItem(items[0].transferItemId);

    ASSERT_TRUE(waitFor([&] {
        auto j = jobRepository.getById(job.transferJobId);
        return j && j->status == TransferStatus::Cancelled;
    }));

    queue.stop();
}

TEST(TransferQueueServiceTest, RecoversAndRestartsItemsLeftMidTransferByAPreviousRun) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    TransferJobRepository jobRepository(db);
    TransferItemRepository itemRepository(db);

    // Simulate a crash: a job/item left in "Transferring" state from a
    // previous (now-gone) TransferQueueService instance.
    TransferJob job;
    job.name = "Interrupted job";
    job.status = TransferStatus::Transferring;
    jobRepository.add(job);

    const auto srcDir = gth::test::testTempRoot() / ("gth_xfer_recover_" + Guid::newGuid());
    fs::create_directories(srcDir);
    std::ofstream(srcDir / "data.bin") << "data";
    const auto dstPath = srcDir / "copied.bin";

    TransferItem item;
    item.transferJobId = job.transferJobId;
    item.sourcePath = (srcDir / "data.bin").string();
    item.destinationPath = dstPath.string();
    item.status = TransferStatus::Transferring;
    item.bytesTransferred = 2;  // partial progress from "before the crash"
    itemRepository.add(item);

    LocalFileTransferProvider provider;
    TransferQueueService queue(jobRepository, itemRepository, provider, 1);
    queue.start();

    ASSERT_TRUE(waitFor([&] {
        auto j = jobRepository.getById(job.transferJobId);
        return j && j->status == TransferStatus::Completed;
    }));

    EXPECT_TRUE(fs::exists(dstPath));

    queue.stop();
    fs::remove_all(srcDir);
}
