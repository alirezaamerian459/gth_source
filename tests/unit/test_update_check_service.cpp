#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "gth/application/indexing/GameIndexerService.h"
#include "gth/application/indexing/UpdateCheckService.h"
#include "gth/database/Database.h"
#include "gth/database/GameFileRepository.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/ScanJobRepository.h"
#include "gth/database/StorageRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

class FakeScanner : public IStorageScanner {
public:
    std::vector<ScannedVariant> results;
    std::vector<ScannedVariant> scan(const std::string&) override { return results; }
};

// What Sony "answers" for each "PLATFORM|TITLEID", and how often it was asked.
class FakeLatestReleases : public ILatestReleaseService {
public:
    std::map<std::string, LatestReleaseResult> answers;
    std::vector<std::string> asked;

    LatestReleaseResult latestRelease(const std::string& platform, const std::string& titleId) override {
        const std::string key = platform + "|" + titleId;
        asked.push_back(key);
        const auto it = answers.find(key);
        if (it != answers.end()) return it->second;
        LatestReleaseResult none;
        none.status = LatestReleaseStatus::NoSonyLink;
        none.detail = "no Sony version link saved for " + titleId;
        return none;
    }

    void found(const std::string& platform, const std::string& titleId, const std::string& version,
               std::vector<long long> pieces = {}, long long total = 0) {
        LatestReleaseResult result;
        result.status = LatestReleaseStatus::Found;
        result.release.version = version;
        result.release.pieceSizes = std::move(pieces);
        result.release.packageSizeBytes = total;
        answers[platform + "|" + titleId] = result;
    }
    void failed(const std::string& platform, const std::string& titleId, const std::string& why) {
        LatestReleaseResult result;
        result.status = LatestReleaseStatus::Failed;
        result.detail = why;
        answers[platform + "|" + titleId] = result;
    }
};

struct Fixture {
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository variantRepository{db};
    GameVersionRepository versionRepository{db};
    GameFileRepository fileRepository{db};
    ScanJobRepository scanJobRepository{db};
    FakeScanner scanner;
    GameIndexerService indexer{storageRepository, scanner,        gameRepository,  variantRepository,
                               versionRepository, fileRepository, scanJobRepository};
    FakeLatestReleases sony;
    UpdateCheckService checker{variantRepository, versionRepository, fileRepository, sony};

    std::string storageId;

    Fixture() {
        Migrator::applyMigrations(db);
        Storage storage;
        storage.name = "Archive";
        storage.rootPath = "D:/Archive";
        storage.type = StorageType::LocalHdd;
        storageRepository.add(storage);
        storageId = storage.storageId;
    }

    // One release: split files "<contentId>_0.pkg", "_1.pkg", ... of the given sizes.
    void addRelease(const std::string& platform, const std::string& title, const std::string& titleId,
                    const std::string& contentId, const std::string& version, const std::vector<long long>& partSizes) {
        ScannedVersion sv;
        sv.version = version;
        sv.relativePath = contentId;
        for (size_t i = 0; i < partSizes.size(); ++i) {
            sv.files.push_back(ScannedFile{contentId + "_" + std::to_string(i) + ".pkg", partSizes[i], "2026-01-01T00:00:00Z"});
        }
        ScannedVariant variant;
        variant.platform = platform;
        variant.title = title;
        variant.region = "Europe";
        variant.relativePath = titleId;
        variant.versions.push_back(std::move(sv));
        scanner.results.push_back(std::move(variant));
    }
    void scan() { indexer.scanStorage(storageId); }

    UpdateCheckEntry onlyEntry() {
        const auto summary = checker.checkAll();
        EXPECT_EQ(summary.entries.size(), 1u);
        return summary.entries.empty() ? UpdateCheckEntry{} : summary.entries[0];
    }
};

const std::vector<long long> kSonyPieces = {4294967296LL, 4294967296LL, 2459566080LL, 89128960LL};
const char* kItTakesTwo = "EP0006-PPSA02343_00-NUTSEU0000000000";

}  // namespace

TEST(UpdateCheckServiceTest, ADumpWithNoVersionMatchingSonysLatestPackageIsUpToDate) {
    Fixture f;
    // The archive holds the package's split files minus its last (metadata) piece, exactly like a real dump.
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "", {4294967296LL, 4294967296LL, 2459566080LL});
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000", kSonyPieces, 45498368000LL);

    const auto entry = f.onlyEntry();

    EXPECT_EQ(entry.state, UpdateState::UpToDate) << entry.detail;
    EXPECT_EQ(entry.latestVersion, "01.500.000");
    EXPECT_EQ(entry.installedVersion, "01.500.000");
    EXPECT_TRUE(entry.installedVersionInferred) << "the version was worked out from sizes, not read from the files";
}

TEST(UpdateCheckServiceTest, ACompleteCopyOfSonysLatestPackageIsUpToDateToo) {
    Fixture f;
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "", kSonyPieces);
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000", kSonyPieces);
    EXPECT_EQ(f.onlyEntry().state, UpdateState::UpToDate);
}

TEST(UpdateCheckServiceTest, ADumpWhoseFilesDifferFromSonysLatestNeedsAnUpdate) {
    Fixture f;
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "", {4294967296LL, 4294967296LL, 2000000000LL});
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000", kSonyPieces, 45498368000LL);

    const auto entry = f.onlyEntry();

    EXPECT_EQ(entry.state, UpdateState::UpdateAvailable);
    EXPECT_EQ(entry.latestVersion, "01.500.000");
    EXPECT_FALSE(entry.installedVersionInferred);
    EXPECT_NE(entry.detail.find("01.500.000"), std::string::npos) << entry.detail;
}

TEST(UpdateCheckServiceTest, ADumpMissingAPartFromTheMiddleIsNotMistakenForTheLatest) {
    Fixture f;
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "", {4294967296LL, 2459566080LL});  // parts 0,1 only, but wrong sizes
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000", kSonyPieces);
    EXPECT_EQ(f.onlyEntry().state, UpdateState::UpdateAvailable);
}

TEST(UpdateCheckServiceTest, AKnownVersionIsComparedWithSonysLatest) {
    Fixture f;
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "01.300.000", {100});
    f.scan();

    f.sony.found("PS5", "PPSA02343", "01.500.000");
    auto behind = f.onlyEntry();
    EXPECT_EQ(behind.state, UpdateState::UpdateAvailable);
    EXPECT_EQ(behind.installedVersion, "01.300.000");
    EXPECT_NE(behind.detail.find("01.300.000"), std::string::npos) << behind.detail;

    f.sony.found("PS5", "PPSA02343", "01.300.000");
    EXPECT_EQ(f.onlyEntry().state, UpdateState::UpToDate);

    f.sony.found("PS5", "PPSA02343", "1.300");  // same version written differently
    EXPECT_EQ(f.onlyEntry().state, UpdateState::UpToDate);

    f.sony.found("PS5", "PPSA02343", "01.200.000");  // ahead of what Sony lists
    EXPECT_EQ(f.onlyEntry().state, UpdateState::UpToDate);
}

TEST(UpdateCheckServiceTest, AVersionNumberBeatsTheSizesWhenBothAreAvailable) {
    Fixture f;
    // Sizes look like the latest package, but the recorded version says otherwise: trust the record.
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "01.300.000", {4294967296LL, 4294967296LL, 2459566080LL});
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000", kSonyPieces);
    EXPECT_EQ(f.onlyEntry().state, UpdateState::UpdateAvailable);
}

TEST(UpdateCheckServiceTest, APs5GameWithoutASavedLinkSaysThereIsNoSonyData) {
    Fixture f;
    f.addRelease("PS5", "EA SPORTS FC 25", "PPSA20050", "EP0006-PPSA20050_00-EASPORTSFC2025BG", "", {1000});
    f.scan();

    const auto summary = f.checker.checkAll();

    ASSERT_EQ(summary.entries.size(), 1u);
    EXPECT_EQ(summary.entries[0].state, UpdateState::NoSonyData);
    EXPECT_EQ(summary.noSonyData, 1);
    EXPECT_NE(summary.entries[0].detail.find("PPSA20050"), std::string::npos);
}

TEST(UpdateCheckServiceTest, ALatestVersionWithNoWayToTellWhatYouHaveIsReportedAsThat) {
    Fixture f;
    f.addRelease("PS4", "God of War", "CUSA07410", "EP9000-CUSA07410_00-00000000GODOFWAR", "", {8000000000LL});
    f.scan();
    f.sony.found("PS4", "CUSA07410", "01.36");  // PS4 gives no piece list to compare sizes with

    const auto entry = f.onlyEntry();

    EXPECT_EQ(entry.state, UpdateState::InstalledVersionUnknown);
    EXPECT_EQ(entry.latestVersion, "01.36");
}

TEST(UpdateCheckServiceTest, APs4GameWithNoPublishedUpdateIsUpToDate) {
    Fixture f;
    f.addRelease("PS4", "Some Game", "CUSA00001", "EP0000-CUSA00001_00-SOMEGAME00000000", "01.00", {100});
    f.scan();
    LatestReleaseResult none;
    none.status = LatestReleaseStatus::NoUpdatePublished;
    f.sony.answers["PS4|CUSA00001"] = none;

    const auto entry = f.onlyEntry();

    EXPECT_EQ(entry.state, UpdateState::UpToDate);
    EXPECT_TRUE(entry.latestVersion.empty());
}

TEST(UpdateCheckServiceTest, FailuresCarryTheirReasonIntoTheSummary) {
    Fixture f;
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "", {100});
    f.scan();
    f.sony.failed("PS5", "PPSA02343", "no answer from Sony: couldn't connect");

    const auto summary = f.checker.checkAll();

    ASSERT_EQ(summary.entries.size(), 1u);
    EXPECT_EQ(summary.entries[0].state, UpdateState::CheckFailed);
    EXPECT_EQ(summary.failed, 1);
    EXPECT_NE(summary.firstFailure.find("PPSA02343"), std::string::npos) << summary.firstFailure;
    EXPECT_NE(summary.firstFailure.find("couldn't connect"), std::string::npos) << summary.firstFailure;
}

TEST(UpdateCheckServiceTest, TheSameGameOnTwoStoragesAsksSonyOnce) {
    Fixture f;
    Storage second;
    second.name = "Second drive";
    second.rootPath = "E:/Archive";
    second.type = StorageType::LocalHdd;
    f.storageRepository.add(second);

    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "01.500.000", {100});
    f.scan();
    f.indexer.scanStorage(second.storageId);
    f.sony.found("PS5", "PPSA02343", "01.500.000");

    const auto summary = f.checker.checkAll();

    EXPECT_EQ(summary.entries.size(), 2u);
    EXPECT_EQ(summary.upToDate, 2);
    EXPECT_EQ(f.sony.asked.size(), 1u);
}

TEST(UpdateCheckServiceTest, OnlyPs4AndPs5GamesThatAreStillThereAreChecked) {
    Fixture f;
    f.addRelease("PS3", "Old Game", "BLUS30001", "UP0001-BLUS30001_00-OLDGAME000000000", "01.00", {100});
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "01.500.000", {100});
    f.scan();
    f.scanner.results.pop_back();  // the PS5 game's files are gone on the next scan; the PS3 game stays
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000");

    const auto summary = f.checker.checkAll();

    EXPECT_TRUE(summary.entries.empty());
    EXPECT_TRUE(f.sony.asked.empty());
}

TEST(OrderedPartSizesTest, PutsSplitFilesInPartOrderNotNameOrder) {
    std::vector<GameFile> files;
    for (int index : {10, 2, 0, 1, 3, 4, 5, 6, 7, 8, 9}) {
        GameFile file;
        file.relativePath = "Game_" + std::to_string(index) + ".pkg";
        file.sizeBytes = 1000 + index;
        files.push_back(file);
    }
    const auto sizes = OrderedPartSizes(files);
    ASSERT_TRUE(sizes.has_value());
    ASSERT_EQ(sizes->size(), 11u);
    EXPECT_EQ((*sizes)[0], 1000);
    EXPECT_EQ((*sizes)[2], 1002);
    EXPECT_EQ((*sizes)[10], 1010) << "_10 comes after _9, not after _1";
}

TEST(OrderedPartSizesTest, RefusesWhenTheOrderCannotBeTrusted) {
    GameFile a, b, c;
    a.relativePath = "Game_0.pkg";
    b.relativePath = "Game_2.pkg";  // _1 is missing
    c.relativePath = "Game.pkg";
    EXPECT_FALSE(OrderedPartSizes({a, b}).has_value()) << "a part is missing from the middle";
    EXPECT_FALSE(OrderedPartSizes({a, c}).has_value()) << "a file with no part number among several";
    EXPECT_FALSE(OrderedPartSizes({}).has_value());

    c.sizeBytes = 42;
    const auto single = OrderedPartSizes({c});
    ASSERT_TRUE(single.has_value());
    EXPECT_EQ((*single)[0], 42) << "a package that is one file is just that file";
}

TEST(PartSizesMatchPackageTest, AllowsOnlyTheLastPieceToBeAbsent) {
    const std::vector<long long> pieces = {10, 10, 5, 2};
    EXPECT_TRUE(PartSizesMatchPackage({10, 10, 5, 2}, pieces));
    EXPECT_TRUE(PartSizesMatchPackage({10, 10, 5}, pieces)) << "metadata piece not archived";
    EXPECT_FALSE(PartSizesMatchPackage({10, 10}, pieces)) << "two pieces missing";
    EXPECT_FALSE(PartSizesMatchPackage({10, 10, 6}, pieces));
    EXPECT_FALSE(PartSizesMatchPackage({10, 9, 5}, pieces));
    EXPECT_FALSE(PartSizesMatchPackage({10, 10, 5, 2, 1}, pieces)) << "more files than the package has";
    EXPECT_FALSE(PartSizesMatchPackage({}, pieces));
    EXPECT_FALSE(PartSizesMatchPackage({10}, {}));
}

TEST(OrderedPartSizesTest, TheMetadataPieceIsAlwaysTheLastPiece) {
    std::vector<GameFile> files;
    for (const char* name : {"Game_sc.pkg", "Game_1.pkg", "Game_0.pkg", "Game_10.pkg", "Game_2.pkg", "Game_3.pkg",
                             "Game_4.pkg", "Game_5.pkg", "Game_6.pkg", "Game_7.pkg", "Game_8.pkg", "Game_9.pkg"}) {
        GameFile file;
        file.relativePath = name;
        file.sizeBytes = std::string(name) == "Game_sc.pkg" ? 999 : 100 + std::atoi(std::string(name).substr(5).c_str());
        files.push_back(file);
    }

    const auto sizes = OrderedPartSizes(files);

    ASSERT_TRUE(sizes.has_value());
    ASSERT_EQ(sizes->size(), 12u);
    EXPECT_EQ(sizes->front(), 100);
    EXPECT_EQ((*sizes)[10], 110);
    EXPECT_EQ(sizes->back(), 999) << "the _sc piece goes last, after _10";
}

TEST(OrderedPartSizesTest, ARepeatedMetadataPieceIsNotAPackageWeUnderstand) {
    GameFile a, b, c;
    a.relativePath = "Game_0.pkg";
    b.relativePath = "Game_sc.pkg";
    c.relativePath = "Other_sc.pkg";
    EXPECT_FALSE(OrderedPartSizes({a, b, c}).has_value());
    EXPECT_FALSE(OrderedPartSizes({b}).has_value()) << "a metadata piece alone is not a package";
}

TEST(UpdateCheckServiceTest, ACompleteReleaseIncludingItsMetadataPieceMatchesSonysFullPackage) {
    Fixture f;
    // Version unknown (say the metadata piece is there but unreadable): the sizes still identify it.
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "", {4294967296LL, 4294967296LL, 2459566080LL});
    f.scanner.results[0].versions[0].files.push_back(
        ScannedFile{std::string(kItTakesTwo) + "_sc.pkg", 89128960LL, "2026-01-01T00:00:00Z"});
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000", kSonyPieces, 45498368000LL);

    const auto entry = f.onlyEntry();

    EXPECT_EQ(entry.state, UpdateState::UpToDate) << entry.detail;
    EXPECT_TRUE(entry.installedVersionInferred);
}

TEST(UpdateCheckServiceTest, ACompleteReleaseWhoseMetadataPieceIsFromAnOlderVersionNeedsAnUpdate) {
    Fixture f;
    f.addRelease("PS5", "It Takes Two", "PPSA02343", kItTakesTwo, "", {4294967296LL, 4294967296LL, 2459566080LL});
    f.scanner.results[0].versions[0].files.push_back(
        ScannedFile{std::string(kItTakesTwo) + "_sc.pkg", 88000000LL, "2026-01-01T00:00:00Z"});  // not Sony's latest size
    f.scan();
    f.sony.found("PS5", "PPSA02343", "01.500.000", kSonyPieces, 45498368000LL);

    EXPECT_EQ(f.onlyEntry().state, UpdateState::UpdateAvailable);
}
