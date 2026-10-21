#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gth/application/storage/StorageService.h"
#include "gth/core/storage/PhysicalDiskHealth.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"
#include "gth/storage/SmartHealth.h"
#include "gth/storage/WindowsStorageHealthProvider.h"

using namespace gth::storage;

namespace {

struct Entry {
    unsigned char id;
    unsigned short flags;
    unsigned char current;
    unsigned char worst;
    unsigned long long raw;
    unsigned char threshold;
};

// A SMART READ DATA sector holding the entries, laid out as a disk returns it.
std::vector<unsigned char> dataSector(const std::vector<Entry>& entries) {
    std::vector<unsigned char> sector(512, 0);
    sector[0] = 0x10;  // revision
    for (size_t i = 0; i < entries.size(); ++i) {
        unsigned char* e = sector.data() + 2 + i * 12;
        e[0] = entries[i].id;
        e[1] = static_cast<unsigned char>(entries[i].flags & 0xFF);
        e[2] = static_cast<unsigned char>(entries[i].flags >> 8);
        e[3] = entries[i].current;
        e[4] = entries[i].worst;
        for (int b = 0; b < 6; ++b) e[5 + b] = static_cast<unsigned char>((entries[i].raw >> (8 * b)) & 0xFF);
    }
    return sector;
}

std::vector<unsigned char> thresholdSector(const std::vector<Entry>& entries) {
    std::vector<unsigned char> sector(512, 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        unsigned char* e = sector.data() + 2 + i * 12;
        e[0] = entries[i].id;
        e[1] = entries[i].threshold;
    }
    return sector;
}

// Attributes as the provider builds them: parsed from the data sector, thresholds applied.
std::vector<SmartAttribute> attributes(const std::vector<Entry>& entries) {
    const auto data = dataSector(entries);
    const auto thresholds = thresholdSector(entries);
    auto parsed = parseAtaSmartAttributes(data.data(), data.size());
    applyAtaSmartThresholds(parsed, thresholds.data(), thresholds.size());
    return parsed;
}

constexpr unsigned short kPreFail = 0x0001;

}  // namespace

TEST(SmartParseTest, ReadsIdFlagsValuesAndSixByteRawLittleEndian) {
    const auto parsed = attributes({{5, kPreFail, 100, 99, 0x0000'0102'0304ull, 36}, {194, 0x0022, 71, 60, 29, 0}});
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].id, 5);
    EXPECT_TRUE(parsed[0].preFail());
    EXPECT_EQ(parsed[0].current, 100);
    EXPECT_EQ(parsed[0].worst, 99);
    EXPECT_EQ(parsed[0].raw, 0x0000'0102'0304ull);
    EXPECT_EQ(parsed[0].threshold, 36);
    EXPECT_FALSE(parsed[1].preFail());
    EXPECT_EQ(parsed[1].raw, 29u);
}

TEST(SmartParseTest, ShortOrMissingBuffersGiveNothing) {
    unsigned char small[100] = {};
    EXPECT_TRUE(parseAtaSmartAttributes(small, sizeof(small)).empty());
    EXPECT_TRUE(parseAtaSmartAttributes(nullptr, 512).empty());
    EXPECT_TRUE(parseAtaSmartAttributes(dataSector({}).data(), 512).empty());  // all entries unused
}

TEST(SmartTemperatureTest, ReadsTheFirstRawByteOfAttribute194OrFallsBackTo190) {
    EXPECT_EQ(ataTemperatureCelsius(attributes({{194, 0, 71, 60, 0x0012'0000'0029ull, 0}})), 41);  // packed min/max above the reading
    EXPECT_EQ(ataTemperatureCelsius(attributes({{190, 0, 60, 50, 38, 0}})), 38);
    EXPECT_FALSE(ataTemperatureCelsius(attributes({{194, 0, 100, 100, 0, 0}})).has_value());   // 0 is not a reading
    EXPECT_FALSE(ataTemperatureCelsius(attributes({{194, 0, 100, 100, 200, 0}})).has_value()); // implausible
    EXPECT_FALSE(ataTemperatureCelsius(attributes({{5, 0, 100, 100, 0, 0}})).has_value());
}

TEST(SmartAssessTest, AHealthyDiskIsOneHundredWithNoComplaint) {
    const auto result = assessAtaHealth(attributes({{5, kPreFail, 100, 100, 0, 36}, {197, 0, 100, 100, 0, 0}, {198, 0, 100, 100, 0, 0}}));
    EXPECT_EQ(result.percent, 100);
    EXPECT_TRUE(result.detail.empty());
}

TEST(SmartAssessTest, ReallocatedSectorsCostMoreAsTheyPileUp) {
    const auto few = assessAtaHealth(attributes({{5, kPreFail, 100, 100, 3, 36}}));
    EXPECT_EQ(few.percent, 92);
    EXPECT_EQ(few.detail, "Reallocated sectors: 3");
    EXPECT_EQ(assessAtaHealth(attributes({{5, kPreFail, 100, 100, 12, 36}})).percent, 85);
    EXPECT_EQ(assessAtaHealth(attributes({{5, kPreFail, 100, 100, 80, 36}})).percent, 75);
    EXPECT_EQ(assessAtaHealth(attributes({{5, kPreFail, 100, 100, 300, 36}})).percent, 60);
    EXPECT_EQ(assessAtaHealth(attributes({{5, kPreFail, 100, 100, 5000, 36}})).percent, 45);
}

TEST(SmartAssessTest, SeveralProblemsAddUpAndTheBiggestIsNamed) {
    const auto result = assessAtaHealth(attributes({{5, kPreFail, 100, 100, 3, 36}, {197, 0, 100, 100, 3, 0}}));
    EXPECT_EQ(result.percent, 72);  // 100 - 8 (reallocated) - 20 (pending)
    EXPECT_EQ(result.detail, "Pending sectors: 3");
}

TEST(SmartAssessTest, CountersAloneNeverPushBelowTwentyPercent) {
    const auto result = assessAtaHealth(attributes({{5, kPreFail, 100, 100, 9000, 36},   // -55
                                                      {197, 0, 100, 100, 999, 0},          // -35
                                                      {198, 0, 100, 100, 999, 0},          // -35
                                                      {187, 0, 100, 100, 99, 0}}));        // -25
    EXPECT_EQ(result.percent, 20);
}

TEST(SmartAssessTest, RawCountersUseOnlyTheLowThirtyTwoBits) {
    // Some vendors pack extra data above the count; that must not read as billions of bad sectors.
    EXPECT_EQ(assessAtaHealth(attributes({{5, kPreFail, 100, 100, 0x0001'0000'0000ull, 36}})).percent, 100);
}

TEST(SmartAssessTest, AttributeAtItsFailureThresholdMeansReplaceNow) {
    const auto result = assessAtaHealth(attributes({{5, kPreFail, 30, 30, 0, 36}}));
    EXPECT_LE(result.percent, 10);
    EXPECT_EQ(result.detail, "Reallocated sectors has reached its failure threshold");
}

TEST(SmartAssessTest, ValuesThatMeanNotMeasuredAreNeverComparedToAThreshold) {
    EXPECT_EQ(assessAtaHealth(attributes({{1, kPreFail, 0, 0, 0, 51}})).percent, 100);
    EXPECT_EQ(assessAtaHealth(attributes({{1, kPreFail, 254, 254, 0, 51}})).percent, 100);
    EXPECT_EQ(assessAtaHealth(attributes({{1, kPreFail, 255, 255, 0, 51}})).percent, 100);
    // An old-age (not pre-failure) attribute at its threshold is not a failure verdict.
    EXPECT_EQ(assessAtaHealth(attributes({{9, 0x0000, 10, 10, 50000, 10}})).percent, 100);
}

TEST(SmartAssessTest, SsdLifeCountersSetTheCeiling) {
    const auto result = assessAtaHealth(attributes({{231, 0, 72, 72, 0, 0}}));
    EXPECT_EQ(result.percent, 72);
    EXPECT_EQ(result.detail, "SSD life remaining: 72%");
    EXPECT_EQ(assessAtaHealth(attributes({{231, 0, 90, 90, 0, 0}, {177, 0, 85, 85, 0, 0}})).percent, 85);  // the lower one wins
    EXPECT_EQ(assessAtaHealth(attributes({{233, 0, 100, 100, 0, 0}})).percent, 100);                        // new drive
    EXPECT_EQ(assessAtaHealth(attributes({{231, 0, 4, 4, 0, 0}})).percent, 4);                              // nearly worn out is allowed to read low
}

TEST(SmartAssessTest, SsdLifeCountersAreIgnoredOnASpinningDisk) {
    EXPECT_EQ(assessAtaHealth(attributes({{202, 0, 60, 60, 0, 0}}), false).percent, 100);
    EXPECT_EQ(assessAtaHealth(attributes({{202, 0, 60, 60, 0, 0}}), true).percent, 60);
    EXPECT_EQ(assessAtaHealth(attributes({{202, 0, 60, 60, 0, 0}}), std::nullopt).percent, 60);  // unknown media: still read
}

TEST(SmartAssessTest, WornSsdWithBadSectorsTakesTheWorseOfTheTwo) {
    const auto result = assessAtaHealth(attributes({{231, 0, 90, 90, 0, 0}, {5, kPreFail, 100, 100, 100, 10}}));
    EXPECT_EQ(result.percent, 75);  // reallocated (-25) is lower than the life counter (90)
    EXPECT_EQ(result.detail, "Reallocated sectors: 100");
}

namespace {

std::vector<unsigned char> nvmeLog(unsigned char criticalWarning, int kelvin, unsigned char spare, unsigned char spareThreshold, unsigned char used) {
    std::vector<unsigned char> log(512, 0);
    log[0] = criticalWarning;
    log[1] = static_cast<unsigned char>(kelvin & 0xFF);
    log[2] = static_cast<unsigned char>(kelvin >> 8);
    log[3] = spare;
    log[4] = spareThreshold;
    log[5] = used;
    return log;
}

}  // namespace

TEST(NvmeHealthTest, ParsesTheHealthLogPage) {
    const auto log = nvmeLog(0, 310, 100, 10, 7);
    const auto health = parseNvmeHealthLog(log.data(), log.size());
    ASSERT_TRUE(health.has_value());
    EXPECT_EQ(health->temperatureCelsius, 37);
    EXPECT_EQ(health->availableSpare, 100);
    EXPECT_EQ(health->spareThreshold, 10);
    EXPECT_EQ(health->percentageUsed, 7);
    EXPECT_FALSE(parseNvmeHealthLog(log.data(), 10).has_value());
    EXPECT_FALSE(parseNvmeHealthLog(nullptr, 512).has_value());
    const auto noTemp = nvmeLog(0, 0, 100, 10, 0);
    EXPECT_FALSE(parseNvmeHealthLog(noTemp.data(), noTemp.size())->temperatureCelsius.has_value());
}

TEST(NvmeHealthTest, WearIsTheRatedLifeUsedSubtractedFromOneHundred) {
    NvmeHealth health;
    health.percentageUsed = 30;
    const auto result = assessNvmeHealth(health);
    EXPECT_EQ(result.percent, 70);
    EXPECT_EQ(result.detail, "Drive wear: 30% of its rated life used");

    health.percentageUsed = 0;
    EXPECT_EQ(assessNvmeHealth(health).percent, 100);
    EXPECT_TRUE(assessNvmeHealth(health).detail.empty());

    health.percentageUsed = 120;  // the spec lets it run past 100
    EXPECT_EQ(assessNvmeHealth(health).percent, 0);
}

TEST(NvmeHealthTest, CriticalWarningsAndLowSpareMeanReplaceNow) {
    NvmeHealth health;
    health.criticalWarning = 0x01;
    EXPECT_LE(assessNvmeHealth(health).percent, 10);

    health = {};
    health.criticalWarning = 0x04;
    EXPECT_LE(assessNvmeHealth(health).percent, 10);

    health = {};
    health.criticalWarning = 0x08;
    EXPECT_EQ(assessNvmeHealth(health).detail, "The drive has gone read-only");

    health = {};
    health.availableSpare = 5;
    health.spareThreshold = 10;
    EXPECT_LE(assessNvmeHealth(health).percent, 10);

    health = {};
    health.criticalWarning = 0x02;  // temperature warning only: not a health verdict
    EXPECT_EQ(assessNvmeHealth(health).percent, 100);
}

TEST(SummarizeDetailTest, TheWorstDisksReasonTravelsWithTheSummary) {
    using gth::core::PhysicalDiskHealth;
    PhysicalDiskHealth good;
    good.diskNumber = 1;
    good.healthPercent = 100;
    PhysicalDiskHealth bad;
    bad.diskNumber = 2;
    bad.healthPercent = 85;
    bad.healthDetail = "Reallocated sectors: 12";
    const auto summary = gth::core::summarizeDisks({good, bad});
    EXPECT_EQ(summary.healthPercent, 85);
    ASSERT_TRUE(summary.healthDetail.has_value());
    EXPECT_EQ(*summary.healthDetail, "Reallocated sectors: 12");
    EXPECT_FALSE(gth::core::summarizeDisks({good}).healthDetail.has_value());
}

namespace {

class OneVolume : public gth::core::IStorageDetector {
public:
    std::vector<gth::core::DetectedStorageInfo> detect() override {
        return {gth::core::DetectedStorageInfo{"Games (D:)", gth::core::StorageType::LocalHdd, "D:\\", 4'000'000'000'000LL,
                                               1'000'000'000'000LL, std::optional<std::string>("NTFS"), true}};
    }
};

class ProviderWithDisks : public gth::core::IStorageHealthProvider {
public:
    std::vector<gth::core::PhysicalDiskHealth> disks;
    gth::core::StorageHealthInfo queryHealth(const std::string&) override { return {}; }
    std::optional<std::vector<gth::core::PhysicalDiskHealth>> queryDisks(const std::string&) override { return disks; }
};

}  // namespace

TEST(StorageServiceHealthAlertTest, AHealthBelowGoodIsFlaggedWithItsReason) {
    gth::database::Database db(":memory:");
    gth::database::Migrator::applyMigrations(db);
    gth::database::StorageRepository repo(db);
    OneVolume detector;
    ProviderWithDisks provider;
    gth::core::PhysicalDiskHealth disk;
    disk.diskNumber = 1;
    disk.healthPercent = 45;
    disk.healthDetail = "Reallocated sectors: 5000";
    disk.temperatureCelsius = 38;
    provider.disks = {disk};

    gth::application::StorageService service(repo, detector, &provider);
    service.detectAndSyncLocalStorages();

    const auto stored = repo.getAll();
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0].healthPercent, 45);
    ASSERT_TRUE(stored[0].healthAlertReason.has_value());
    EXPECT_EQ(*stored[0].healthAlertReason, "Health 45%: Reallocated sectors: 5000");
}

TEST(StorageServiceHealthAlertTest, AHealthStillGoodIsNotAnAlertJustBecauseItIsBelowOneHundred) {
    gth::database::Database db(":memory:");
    gth::database::Migrator::applyMigrations(db);
    gth::database::StorageRepository repo(db);
    OneVolume detector;
    ProviderWithDisks provider;
    gth::core::PhysicalDiskHealth disk;
    disk.diskNumber = 1;
    disk.healthPercent = 85;
    disk.healthDetail = "Reallocated sectors: 12";
    disk.temperatureCelsius = 38;
    provider.disks = {disk};

    gth::application::StorageService service(repo, detector, &provider);
    service.detectAndSyncLocalStorages();
    EXPECT_EQ(repo.getAll()[0].healthPercent, 85);
    EXPECT_FALSE(repo.getAll()[0].healthAlertReason.has_value());
}

TEST(StorageServiceHealthAlertTest, APerfectDiskRaisesNoAlert) {
    gth::database::Database db(":memory:");
    gth::database::Migrator::applyMigrations(db);
    gth::database::StorageRepository repo(db);
    OneVolume detector;
    ProviderWithDisks provider;
    gth::core::PhysicalDiskHealth disk;
    disk.diskNumber = 1;
    disk.healthPercent = 100;
    disk.temperatureCelsius = 38;
    provider.disks = {disk};

    gth::application::StorageService service(repo, detector, &provider);
    service.detectAndSyncLocalStorages();
    EXPECT_FALSE(repo.getAll()[0].healthAlertReason.has_value());
}

// Reads a real disk's SMART table (needs administrator):  set GTH_LIVE_DISK=0  and run this test on its own.
TEST(WindowsProviderLiveTest, SmartFromEnvironment) {
    char* text = nullptr;
    size_t length = 0;
    if (_dupenv_s(&text, &length, "GTH_LIVE_DISK") != 0 || text == nullptr) GTEST_SKIP() << "GTH_LIVE_DISK not set";
    const int disk = std::atoi(text);
    free(text);

    const auto table = readAtaSmartAttributes(disk);
    if (!table) {
        std::printf("  disk %d: no ATA SMART table (not SATA, not administrator, or not readable this way)\n", disk);
        return;
    }
    std::printf("  disk %d: %zu SMART attributes\n", disk, table->size());
    for (const auto& a : *table) {
        std::printf("    id %3d  flags 0x%04x  current %3d  worst %3d  threshold %3d  raw %llu\n", a.id, a.flags, a.current, a.worst,
                    a.threshold, a.raw);
    }
    const auto assessment = assessAtaHealth(*table);
    std::printf("  -> health %d%%  (%s)  temperature %s\n", assessment.percent, assessment.detail.empty() ? "no problems found" : assessment.detail.c_str(),
                ataTemperatureCelsius(*table) ? (std::to_string(*ataTemperatureCelsius(*table)) + " C").c_str() : "unknown");
}
