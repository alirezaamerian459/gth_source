#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>
#include <winioctl.h>

#include "gth/application/storage/StorageService.h"
#include "gth/core/storage/PhysicalDiskHealth.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"
#include "gth/storage/WindowsStorageHealthProvider.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

PhysicalDiskHealth disk(int number, std::optional<int> health, std::optional<int> temperature) {
    PhysicalDiskHealth d;
    d.diskNumber = number;
    d.healthPercent = health;
    d.temperatureCelsius = temperature;
    return d;
}

// A VOLUME_DISK_EXTENTS holding the given disk numbers, laid out exactly as IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returns it.
std::vector<unsigned char> extentsBuffer(const std::vector<DWORD>& diskNumbers) {
    std::vector<unsigned char> buffer(offsetof(VOLUME_DISK_EXTENTS, Extents) + diskNumbers.size() * sizeof(DISK_EXTENT));
    auto* extents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buffer.data());
    extents->NumberOfDiskExtents = static_cast<DWORD>(diskNumbers.size());
    for (size_t i = 0; i < diskNumbers.size(); ++i) {
        extents->Extents[i].DiskNumber = diskNumbers[i];
        extents->Extents[i].StartingOffset.QuadPart = static_cast<LONGLONG>(i) * 1'000'000;
        extents->Extents[i].ExtentLength.QuadPart = 500'000'000'000LL;
    }
    return buffer;
}

class DetectorOfOneVolume : public IStorageDetector {
public:
    std::vector<DetectedStorageInfo> detect() override {
        return {DetectedStorageInfo{"Games (D:)", StorageType::LocalHdd, "D:\\", 12'000'000'000'000LL, 4'000'000'000'000LL,
                                    std::optional<std::string>("NTFS"), true}};
    }
};

class ProviderThatSeesEachDisk : public IStorageHealthProvider {
public:
    std::vector<PhysicalDiskHealth> disks;
    int queryHealthCalls = 0;
    StorageHealthInfo queryHealth(const std::string&) override {
        ++queryHealthCalls;
        return {};
    }
    std::optional<std::vector<PhysicalDiskHealth>> queryDisks(const std::string&) override { return disks; }
};

class ProviderWithOneReadingOnly : public IStorageHealthProvider {
public:
    StorageHealthInfo response;
    StorageHealthInfo queryHealth(const std::string&) override { return response; }
};

}  // namespace

TEST(SummarizeDisksTest, WorstHealthAndHottestDiskWin) {
    const auto summary = summarizeDisks({disk(1, 100, 34), disk(2, 50, 41), disk(3, 100, 38)});
    EXPECT_EQ(summary.healthPercent, 50);
    EXPECT_EQ(summary.temperatureCelsius, 41);
}

TEST(SummarizeDisksTest, UnknownReadingsAreIgnoredNotCountedAsZero) {
    const auto summary = summarizeDisks({disk(1, std::nullopt, std::nullopt), disk(2, 100, 36), disk(3, std::nullopt, 45)});
    EXPECT_EQ(summary.healthPercent, 100);
    EXPECT_EQ(summary.temperatureCelsius, 45);
}

TEST(SummarizeDisksTest, NothingKnownStaysUnknown) {
    const auto summary = summarizeDisks({disk(1, std::nullopt, std::nullopt)});
    EXPECT_FALSE(summary.healthPercent.has_value());
    EXPECT_FALSE(summary.temperatureCelsius.has_value());
    EXPECT_FALSE(summarizeDisks({}).healthPercent.has_value());
}

TEST(VolumeExtentsTest, SpannedVolumeNamesEveryDiskOnce) {
    const auto buffer = extentsBuffer({3, 1, 2, 1});  // disk 1 holds two extents
    EXPECT_EQ(gth::storage::diskNumbersFromVolumeExtents(buffer.data(), buffer.size()), (std::vector<int>{1, 2, 3}));
}

TEST(VolumeExtentsTest, PlainVolumeIsOneDisk) {
    const auto buffer = extentsBuffer({4});
    EXPECT_EQ(gth::storage::diskNumbersFromVolumeExtents(buffer.data(), buffer.size()), (std::vector<int>{4}));
}

TEST(VolumeExtentsTest, TruncatedOrBogusBuffersGiveNothingInsteadOfReadingPastTheEnd) {
    const auto buffer = extentsBuffer({1, 2, 3});
    EXPECT_TRUE(gth::storage::diskNumbersFromVolumeExtents(nullptr, 100).empty());
    EXPECT_TRUE(gth::storage::diskNumbersFromVolumeExtents(buffer.data(), 2).empty());
    // Claims three extents but only two are actually there.
    EXPECT_TRUE(gth::storage::diskNumbersFromVolumeExtents(buffer.data(), buffer.size() - sizeof(DISK_EXTENT)).empty());
    // A wild extent count must not overflow the size check.
    auto wild = extentsBuffer({1});
    reinterpret_cast<VOLUME_DISK_EXTENTS*>(wild.data())->NumberOfDiskExtents = 0xFFFFFFFF;
    EXPECT_TRUE(gth::storage::diskNumbersFromVolumeExtents(wild.data(), wild.size()).empty());
}

TEST(StorageServiceDisksTest, ThreeDisksBehindOneDriveLetterAreReportedSeparatelyAndSummarised) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);
    DetectorOfOneVolume detector;
    ProviderThatSeesEachDisk provider;
    provider.disks = {disk(1, 100, 33), disk(2, 50, 47), disk(3, 100, 38)};

    StorageService service(repo, detector, &provider);
    service.detectAndSyncLocalStorages();

    const auto stored = repo.getAll();
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0].healthPercent, 50);        // worst disk
    EXPECT_EQ(stored[0].temperatureCelsius, 47);   // hottest disk
    EXPECT_EQ(provider.queryHealthCalls, 0);       // one lookup per scan, not two

    const auto disks = service.getPhysicalDisks("D:\\");
    ASSERT_EQ(disks.size(), 3u);
    EXPECT_EQ(disks[1].diskNumber, 2);
    EXPECT_EQ(disks[1].temperatureCelsius, 47);
    // Looked up the way the UI will: a different spelling of the same path finds the same disks.
    EXPECT_EQ(service.getPhysicalDisks("d:").size(), 3u);
}

TEST(StorageServiceDisksTest, ProviderThatCannotSeparateDisksStillFillsTheStorageRow) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);
    DetectorOfOneVolume detector;
    ProviderWithOneReadingOnly provider;
    provider.response.healthPercent = 100;
    provider.response.temperatureCelsius = 40;

    StorageService service(repo, detector, &provider);
    service.detectAndSyncLocalStorages();

    const auto stored = repo.getAll();
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0].healthPercent, 100);
    EXPECT_EQ(stored[0].temperatureCelsius, 40);
    EXPECT_TRUE(service.getPhysicalDisks("D:\\").empty());
}

TEST(StorageServiceDisksTest, NoProviderMeansNoDiskList) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);
    DetectorOfOneVolume detector;
    StorageService service(repo, detector);
    service.detectAndSyncLocalStorages();
    EXPECT_TRUE(service.getPhysicalDisks("D:\\").empty());
}

// Runs the real Windows lookup against this machine's own system drive: it must resolve to at least one disk
// and describe it. (Which disk, and whether temperature is readable, depends on the machine and on elevation.)
TEST(WindowsProviderLiveTest, SystemDriveResolvesToARealDisk) {
    gth::storage::WindowsStorageHealthProvider provider;
    const auto disks = provider.queryDisks("C:\\");
    ASSERT_TRUE(disks.has_value());
    if (disks->empty()) GTEST_SKIP() << "the system drive isn't a local volume the OS will describe here";

    for (const auto& d : *disks) {
        EXPECT_GE(d.diskNumber, 0);
        EXPECT_FALSE(d.model.empty()) << "disk " << d.diskNumber << " has no model";
        std::printf("  disk %d: %s, %llu bytes, health=%s, temp=%s\n", d.diskNumber, d.model.c_str(),
                    static_cast<unsigned long long>(d.sizeBytes),
                    d.healthPercent ? std::to_string(*d.healthPercent).c_str() : "unknown",
                    d.temperatureCelsius ? std::to_string(*d.temperatureCelsius).c_str() : "unknown");
    }
}

TEST(WindowsProviderLiveTest, NonLocalPathsGiveAnEmptyListNotAGuess) {
    gth::storage::WindowsStorageHealthProvider provider;
    const auto disks = provider.queryDisks("\\\\server\\share");
    ASSERT_TRUE(disks.has_value());
    EXPECT_TRUE(disks->empty());
    EXPECT_FALSE(provider.queryHealth("\\\\server\\share").healthPercent.has_value());
}

// Manual check against a real volume (used with a temporary spanned volume; see the elevated test script):
//   set GTH_LIVE_ROOT=Z:\  and  GTH_LIVE_EXPECT_DISKS=4,5,6  then run this test on its own.
TEST(WindowsProviderLiveTest, RootFromEnvironment) {
    char* root = nullptr;
    size_t length = 0;
    if (_dupenv_s(&root, &length, "GTH_LIVE_ROOT") != 0 || root == nullptr) GTEST_SKIP() << "GTH_LIVE_ROOT not set";
    const std::string rootPath(root);
    free(root);

    gth::storage::WindowsStorageHealthProvider provider;
    const auto disks = provider.queryDisks(rootPath);
    ASSERT_TRUE(disks.has_value());

    std::string found;
    for (const auto& d : *disks) {
        std::printf("  %s -> disk %d: model='%s', %llu bytes, health=%s, temp=%s\n", rootPath.c_str(), d.diskNumber,
                    d.model.c_str(), static_cast<unsigned long long>(d.sizeBytes),
                    d.healthPercent ? std::to_string(*d.healthPercent).c_str() : "unknown",
                    d.temperatureCelsius ? std::to_string(*d.temperatureCelsius).c_str() : "unknown");
        found += (found.empty() ? "" : ",") + std::to_string(d.diskNumber);
    }
    const auto summary = provider.queryHealth(rootPath);
    std::printf("  storage-level: health=%s temp=%s\n",
                summary.healthPercent ? std::to_string(*summary.healthPercent).c_str() : "unknown",
                summary.temperatureCelsius ? std::to_string(*summary.temperatureCelsius).c_str() : "unknown");

    char* expected = nullptr;
    if (_dupenv_s(&expected, &length, "GTH_LIVE_EXPECT_DISKS") == 0 && expected != nullptr) {
        EXPECT_EQ(found, std::string(expected)) << "the disks GTH found for " << rootPath;
        free(expected);
    }
}
