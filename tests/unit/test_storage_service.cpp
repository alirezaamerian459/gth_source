#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <filesystem>

#include "gth/application/storage/StorageService.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

class FakeStorageDetector : public IStorageDetector {
public:
    std::vector<DetectedStorageInfo> results;
    std::vector<DetectedStorageInfo> detect() override { return results; }
};

class FakeStorageHealthProvider : public IStorageHealthProvider {
public:
    StorageHealthInfo response;
    StorageHealthInfo queryHealth(const std::string&) override { return response; }
};

}  // namespace

TEST(StorageServiceTest, DetectAndSyncAddsNewStorageWhenNotPreviouslyKnown) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 500'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    StorageService service(repo, detector);
    const auto result = service.detectAndSyncLocalStorages();

    EXPECT_EQ(result.added, 1);
    EXPECT_EQ(result.updated, 0);
    auto all = repo.getAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].status, StorageStatus::Online);
}

TEST(StorageServiceTest, DetectAndSyncUpdatesExistingStorageWhenRootPathMatches) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage existing;
    existing.name = "Games (D:)";
    existing.rootPath = "D:\\";
    existing.type = StorageType::LocalHdd;
    existing.totalCapacityBytes = 1'000'000'000;
    existing.freeSpaceBytes = 100'000'000;
    existing.status = StorageStatus::Online;
    existing.isAvailable = true;
    repo.add(existing);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 900'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    StorageService service(repo, detector);
    const auto result = service.detectAndSyncLocalStorages();

    EXPECT_EQ(result.added, 0);
    EXPECT_EQ(result.updated, 1);
    auto all = repo.getAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].freeSpaceBytes, 900'000'000);
}

TEST(StorageServiceTest, DetectAndSyncMarksOfflineWhenLocallyDetectableDriveDisappears) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage existing;
    existing.name = "USB Stick";
    existing.rootPath = "F:\\";
    existing.type = StorageType::Usb;
    existing.status = StorageStatus::Online;
    existing.isAvailable = true;
    repo.add(existing);

    FakeStorageDetector detector;  // empty: drive no longer detected

    StorageService service(repo, detector);
    const auto result = service.detectAndSyncLocalStorages();

    EXPECT_EQ(result.markedOffline, 1);
    auto all = repo.getAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].status, StorageStatus::Offline);
    EXPECT_FALSE(all[0].isAvailable);
}

TEST(StorageServiceTest, DetectAndSyncRemovesLocallyDetectableStorageThatStillExistsButWasntReported) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    // Simulates the Windows-system-drive case: a locally-detectable storage
    // whose root path is still physically there (so it's not "unplugged"),
    // but the detector deliberately left it out of this run's results - it
    // should be removed rather than parked offline forever.
    const auto dir = gth::test::testTempRoot() / "gth_storage_service_removed_test";
    std::filesystem::create_directories(dir);

    Storage existing;
    existing.name = "Stale";
    existing.rootPath = dir.string();
    existing.type = StorageType::LocalHdd;
    existing.status = StorageStatus::Online;
    existing.isAvailable = true;
    repo.add(existing);

    FakeStorageDetector detector;  // empty: this run doesn't report it

    StorageService service(repo, detector);
    const auto result = service.detectAndSyncLocalStorages();

    EXPECT_EQ(result.removed, 1);
    EXPECT_EQ(result.markedOffline, 0);
    EXPECT_TRUE(repo.getAll().empty());

    std::filesystem::remove_all(dir);
}
TEST(StorageServiceTest, DetectAndSyncNeverTouchesNonLocallyDetectableStorage) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage existing;
    existing.name = "NAS01";
    existing.rootPath = "\\\\NAS01\\Games";
    existing.type = StorageType::Nas;
    existing.status = StorageStatus::Online;
    existing.isAvailable = true;
    repo.add(existing);

    FakeStorageDetector detector;

    StorageService service(repo, detector);
    const auto result = service.detectAndSyncLocalStorages();

    EXPECT_EQ(result.markedOffline, 0);
    auto all = repo.getAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].status, StorageStatus::Online);
}

TEST(StorageServiceTest, DetectAndSyncRefreshesReachableNasCapacity) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    const auto dir = gth::test::testTempRoot() / "gth_reachable_nas_capacity_test";
    std::filesystem::create_directories(dir);
    Storage existing;
    existing.name = "QNAP";
    existing.rootPath = dir.string();
    existing.type = StorageType::Nas;
    existing.totalCapacityBytes = 0;
    existing.freeSpaceBytes = 0;
    existing.status = StorageStatus::Unknown;
    existing.isAvailable = false;
    repo.add(existing);

    FakeStorageDetector detector;
    StorageService service(repo, detector);
    const auto result = service.detectAndSyncLocalStorages();

    EXPECT_EQ(result.updated, 1);
    const auto found = repo.getByRootPath(dir.string());
    ASSERT_TRUE(found.has_value());
    EXPECT_GT(found->totalCapacityBytes, 0);
    EXPECT_GE(found->freeSpaceBytes, 0);
    EXPECT_EQ(found->status, StorageStatus::Online);
    EXPECT_TRUE(found->isAvailable);

    std::filesystem::remove_all(dir);
}

TEST(StorageServiceTest, DetectAndSyncPopulatesHealthWhenProviderConfigured) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 500'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    FakeStorageHealthProvider healthProvider;
    healthProvider.response = StorageHealthInfo{95, 38};

    StorageService service(repo, detector, &healthProvider);
    service.detectAndSyncLocalStorages();

    auto all = repo.getAll();
    ASSERT_EQ(all.size(), 1u);
    ASSERT_TRUE(all[0].healthPercent.has_value());
    EXPECT_EQ(*all[0].healthPercent, 95);
    ASSERT_TRUE(all[0].temperatureCelsius.has_value());
    EXPECT_EQ(*all[0].temperatureCelsius, 38);
}

TEST(StorageServiceTest, DetectAndSyncLeavesHealthUnknownWithoutProvider) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 500'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    StorageService service(repo, detector);  // no health provider
    service.detectAndSyncLocalStorages();

    auto all = repo.getAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_FALSE(all[0].healthPercent.has_value());
    EXPECT_FALSE(all[0].temperatureCelsius.has_value());
}

TEST(StorageServiceTest, FlagsAlertWhenHealthPercentChangesFromPreviousScan) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage existing;
    existing.name = "Games (D:)";
    existing.rootPath = "D:\\";
    existing.type = StorageType::LocalHdd;
    existing.healthPercent = 100;
    repo.add(existing);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 500'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    FakeStorageHealthProvider healthProvider;
    healthProvider.response = StorageHealthInfo{50, 30};  // healthPercent dropped from 100 to 50

    StorageService service(repo, detector, &healthProvider);
    service.detectAndSyncLocalStorages();

    auto found = repo.getByRootPath("D:\\");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(found->healthAlertReason.has_value());
    EXPECT_NE(found->healthAlertReason->find("Health changed"), std::string::npos);
}

TEST(StorageServiceTest, FlagsAlertWhenTemperatureExceedsThreshold) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 500'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    FakeStorageHealthProvider healthProvider;
    healthProvider.response = StorageHealthInfo{100, 58};  // above the 55C threshold

    StorageService service(repo, detector, &healthProvider);
    service.detectAndSyncLocalStorages();

    auto found = repo.getByRootPath("D:\\");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(found->healthAlertReason.has_value());
    EXPECT_NE(found->healthAlertReason->find("Temperature"), std::string::npos);
}

TEST(StorageServiceTest, NoAlertWhenHealthUnchangedAndTemperatureNormal) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage existing;
    existing.name = "Games (D:)";
    existing.rootPath = "D:\\";
    existing.type = StorageType::LocalHdd;
    existing.healthPercent = 100;
    repo.add(existing);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 500'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    FakeStorageHealthProvider healthProvider;
    healthProvider.response = StorageHealthInfo{100, 35};  // unchanged health, normal temp

    StorageService service(repo, detector, &healthProvider);
    service.detectAndSyncLocalStorages();

    auto found = repo.getByRootPath("D:\\");
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->healthAlertReason.has_value());
}

TEST(StorageServiceTest, FirstEverHealthReadingIsNotTreatedAsAChange) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    FakeStorageDetector detector;
    detector.results = {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 1'000'000'000, 500'000'000,
                                             std::optional<std::string>("NTFS"), true}};

    FakeStorageHealthProvider healthProvider;
    healthProvider.response = StorageHealthInfo{80, 35};  // no previous reading to compare against

    StorageService service(repo, detector, &healthProvider);
    service.detectAndSyncLocalStorages();

    auto found = repo.getByRootPath("D:\\");
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->healthAlertReason.has_value());
}

TEST(StorageServiceTest, GetSummaryAggregatesAcrossAllStorages) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage a;
    a.name = "A";
    a.rootPath = "D:\\";
    a.status = StorageStatus::Online;
    a.totalCapacityBytes = 1000;
    a.freeSpaceBytes = 400;

    Storage b;
    b.name = "B";
    b.rootPath = "E:\\";
    b.status = StorageStatus::Offline;
    b.totalCapacityBytes = 2000;
    b.freeSpaceBytes = 100;

    repo.add(a);
    repo.add(b);

    FakeStorageDetector detector;
    StorageService service(repo, detector);
    const auto summary = service.getSummary();

    EXPECT_EQ(summary.totalStorages, 2);
    EXPECT_EQ(summary.onlineStorages, 1);
    EXPECT_EQ(summary.offlineStorages, 1);
    EXPECT_EQ(summary.totalCapacityBytes, 3000);
    EXPECT_EQ(summary.freeSpaceBytes, 500);
}

TEST(StorageServiceTest, AddManualStorageRegistersAnExistingDirectory) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    const auto dir = gth::test::testTempRoot() / "gth_add_manual_storage_test";
    std::filesystem::create_directories(dir);

    FakeStorageDetector detector;
    StorageService service(repo, detector);
    const auto result = service.addManualStorage(dir.string());

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.storageId.empty());
    auto found = repo.getByRootPath(dir.string());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, StorageStatus::Online);
    EXPECT_TRUE(found->isAvailable);

    std::filesystem::remove_all(dir);
}

TEST(StorageServiceTest, AddManualStorageClassifiesAnOrdinaryPathAsExternalHdd) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    const auto dir = gth::test::testTempRoot() / "gth_add_manual_storage_test2";
    std::filesystem::create_directories(dir);

    FakeStorageDetector detector;
    StorageService service(repo, detector);
    ASSERT_TRUE(service.addManualStorage(dir.string()).success);

    auto found = repo.getByRootPath(dir.string());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->type, StorageType::ExternalHdd);

    std::filesystem::remove_all(dir);
}

TEST(StorageServiceTest, AddManualStorageRejectsNonexistentPath) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    FakeStorageDetector detector;
    StorageService service(repo, detector);
    const auto result = service.addManualStorage("Z:\\ThisPathShouldNotExist_gth_test");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
    EXPECT_TRUE(repo.getAll().empty());
}

TEST(StorageServiceTest, AddManualStorageRejectsDuplicateRootPath) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    const auto dir = gth::test::testTempRoot() / "gth_add_manual_storage_test3";
    std::filesystem::create_directories(dir);

    FakeStorageDetector detector;
    StorageService service(repo, detector);
    ASSERT_TRUE(service.addManualStorage(dir.string()).success);

    const auto second = service.addManualStorage(dir.string());
    EXPECT_FALSE(second.success);
    EXPECT_EQ(repo.getAll().size(), 1u);

    std::filesystem::remove_all(dir);
}

