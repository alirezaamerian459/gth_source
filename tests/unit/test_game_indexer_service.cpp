#include <gtest/gtest.h>

#include <windows.h>

#include <filesystem>

#include "gth/application/indexing/GameIndexerService.h"
#include "gth/application/indexing/GameLibraryService.h"
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

class FakeStorageScanner : public IStorageScanner {
public:
    std::vector<ScannedVariant> results;
    std::vector<ScannedVariant> scan(const std::string&) override { return results; }

    struct Pin {
        std::string rootPath;
        std::string versionRelativePath;
        std::string version;
    };
    std::vector<Pin> pins;
    std::string pinFailure;  // non-empty: pinReleaseVersion refuses with this reason

    bool pinReleaseVersion(const std::string& rootPath, const std::string& versionRelativePath,
                           const std::string& version, std::string& error) override {
        if (!pinFailure.empty()) {
            error = pinFailure;
            return false;
        }
        pins.push_back({rootPath, versionRelativePath, version});
        return true;
    }
};

ScannedVersion makeVersion(const std::string& version, std::vector<ScannedFile> files = {}) {
    ScannedVersion sv;
    sv.version = version;
    sv.relativePath = "container\\" + version;
    sv.files = std::move(files);
    return sv;
}

ScannedVariant makeVariant(const std::string& platform, const std::string& title, const std::string& region,
                            std::vector<ScannedVersion> versions) {
    ScannedVariant variant;
    variant.platform = platform;
    variant.title = title;
    variant.region = region;
    variant.relativePath = platform + "\\" + title + "\\" + region;
    variant.versions = std::move(versions);
    return variant;
}

struct IndexerFixture {
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository variantRepository{db};
    GameVersionRepository versionRepository{db};
    GameFileRepository fileRepository{db};
    ScanJobRepository scanJobRepository{db};
    FakeStorageScanner scanner;
    GameIndexerService indexer{storageRepository,   scanner,           gameRepository,   variantRepository,
                                versionRepository,   fileRepository,   scanJobRepository};

    std::string storageId;

    IndexerFixture() {
        Migrator::applyMigrations(db);

        Storage storage;
        storage.name = "Games HDD";
        storage.rootPath = "D:\\Games";
        storage.type = StorageType::LocalHdd;
        storageRepository.add(storage);
        storageId = storage.storageId;
    }
};

}  // namespace

TEST(GameIndexerServiceTest, ReturnsErrorWhenStorageNotFound) {
    IndexerFixture fixture;
    auto result = fixture.indexer.scanStorage("nonexistent-storage-id");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST(GameIndexerServiceTest, RefusesToScanTheWindowsSystemDriveRegardlessOfWhatsInStorages) {
    IndexerFixture fixture;

    wchar_t windowsDir[MAX_PATH];
    ASSERT_NE(::GetWindowsDirectoryW(windowsDir, MAX_PATH), 0u);
    const std::string systemDriveRoot = std::string(1, static_cast<char>(windowsDir[0])) + ":\\";

    // A storage row for the system drive shouldn't exist by the time this
    // ever runs for real (LocalStorageDetector excludes it, StorageService
    // prunes a stale one on sync) - this test is specifically for the case
    // where one ends up in the table anyway (manual edit, old data, a future
    // bug elsewhere): scanStorage() itself must still refuse it.
    Storage systemDrive;
    systemDrive.name = "System";
    systemDrive.rootPath = systemDriveRoot;
    systemDrive.type = StorageType::LocalHdd;
    fixture.storageRepository.add(systemDrive);

    fixture.scanner.results = {makeVariant("Program Files", "SomeApp", "SomeFolder", {makeVersion("1.0")})};

    auto result = fixture.indexer.scanStorage(systemDrive.storageId);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("system drive"), std::string::npos);
    EXPECT_TRUE(fixture.gameRepository.getAll().empty());
}

TEST(GameIndexerServiceTest, AddsNewGameVariantAndVersionWhenNotPreviouslyIndexed) {
    IndexerFixture fixture;
    fixture.scanner.results = {makeVariant(
        "PS5", "Spider-Man 2", "Europe",
        {makeVersion("1.06", {ScannedFile{"data.bin", 100, "2026-01-01T00:00:00Z"}})})};

    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.gamesAdded, 1);
    EXPECT_EQ(result.variantsAdded, 1);
    EXPECT_EQ(result.versionsAdded, 1);

    auto game = fixture.gameRepository.getByTitle("Spider-Man 2");
    ASSERT_TRUE(game.has_value());

    auto variants = fixture.variantRepository.getByGameId(game->gameId);
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].region, "Europe");
    EXPECT_EQ(variants[0].status, GameVariantStatus::Active);

    auto versions = fixture.versionRepository.getByVariantId(variants[0].gameVariantId);
    ASSERT_EQ(versions.size(), 1u);
    EXPECT_EQ(versions[0].version, "1.06");
    EXPECT_TRUE(versions[0].isCurrent);
    EXPECT_EQ(versions[0].sizeBytes, 100);

    auto files = fixture.fileRepository.getByVersionId(versions[0].gameVersionId);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].relativePath, "data.bin");
}

TEST(GameIndexerServiceTest, GroupsMultipleRegionsUnderTheSameGame) {
    IndexerFixture fixture;
    fixture.scanner.results = {makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.06")}),
                                makeVariant("PS5", "Spider-Man 2", "USA", {makeVersion("1.05")})};

    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.gamesAdded, 1);
    EXPECT_EQ(result.variantsAdded, 2);

    auto game = fixture.gameRepository.getByTitle("Spider-Man 2");
    ASSERT_TRUE(game.has_value());
    EXPECT_EQ(fixture.variantRepository.getByGameId(game->gameId).size(), 2u);
}

TEST(GameIndexerServiceTest, SecondIdenticalScanReportsUnchanged) {
    IndexerFixture fixture;
    fixture.scanner.results = {makeVariant(
        "PS5", "Spider-Man 2", "Europe",
        {makeVersion("1.06", {ScannedFile{"data.bin", 100, "2026-01-01T00:00:00Z"}})})};

    fixture.indexer.scanStorage(fixture.storageId);
    auto second = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_TRUE(second.success);
    EXPECT_EQ(second.gamesAdded, 0);
    EXPECT_EQ(second.variantsAdded, 0);
    EXPECT_EQ(second.versionsAdded, 0);
    EXPECT_EQ(second.versionsUpdated, 0);
    EXPECT_EQ(second.versionsUnchanged, 1);
}

// This is the scenario that exposed the original bug: bumping a version
// must be recognized as an update to the *same* GameVariant, not create a
// second, unrelated variant - because Version is no longer part of the
// container path used to match variants.
TEST(GameIndexerServiceTest, VersionBumpKeepsSameVariantAndAddsNewVersion) {
    IndexerFixture fixture;
    fixture.scanner.results = {makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.04")})};
    fixture.indexer.scanStorage(fixture.storageId);

    // 1.04 still present on disk (not yet cleaned up) alongside new 1.06 -
    // exactly the "old version kept during atomic update" scenario from the
    // spec.
    fixture.scanner.results = {
        makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.04"), makeVersion("1.06")})};
    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.variantsAdded, 0);  // same variant, not a new one
    EXPECT_EQ(result.versionsAdded, 1);  // 1.06 is new
    EXPECT_EQ(result.versionsUnchanged, 1);  // 1.04 untouched

    auto game = fixture.gameRepository.getByTitle("Spider-Man 2");
    auto variants = fixture.variantRepository.getByGameId(game->gameId);
    ASSERT_EQ(variants.size(), 1u);  // still exactly one variant

    auto versions = fixture.versionRepository.getByVariantId(variants[0].gameVariantId);
    ASSERT_EQ(versions.size(), 2u);
    for (const auto& v : versions) {
        EXPECT_EQ(v.status, GameVariantStatus::Active);
        EXPECT_EQ(v.isCurrent, v.version == "1.06");  // newer version wins IsCurrent
    }
}

// Regression test for a real bug: a variant's title lives on its associated
// Game row, not on the variant itself, but the reconciliation loop only
// ever looked up/created a Game by title for a *new* variant - an already-
// tracked variant whose title improves on a later scan (a param.json
// becoming reachable, a better label guess, an online title lookup
// succeeding) kept pointing at the same Game row forever, whose title never
// got touched again. The result: the improved title was computed correctly
// but silently never reached the DB (or the UI), which looked like either a
// UI bug or a completely inert backend change.
TEST(GameIndexerServiceTest, RescanWithAnImprovedTitleRenamesTheExistingGameInPlace) {
    IndexerFixture fixture;
    ScannedVariant variant = makeVariant("PS5", "PPSA02343", "Europe", {makeVersion("1.00")});
    fixture.scanner.results = {variant};
    fixture.indexer.scanStorage(fixture.storageId);

    ASSERT_TRUE(fixture.gameRepository.getByTitle("PPSA02343").has_value());

    // Same variant identity (same relativePath - unaffected by title, per
    // EnhancedStorageScanner using the stable title ID rather than the
    // display title to build it), but a later scan resolved the real name.
    variant.title = "It Takes Two";
    fixture.scanner.results = {variant};
    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.variantsAdded, 0) << "same relativePath must still be recognized as the same variant";
    EXPECT_EQ(result.gamesAdded, 0) << "should rename the existing Game in place, not add a second one";

    EXPECT_FALSE(fixture.gameRepository.getByTitle("PPSA02343").has_value())
        << "the stale placeholder title shouldn't still resolve to a (now orphaned) Game row";
    auto renamed = fixture.gameRepository.getByTitle("It Takes Two");
    ASSERT_TRUE(renamed.has_value()) << "the existing Game row should have been renamed to the improved title";

    auto variants = fixture.variantRepository.getByGameId(renamed->gameId);
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].region, "Europe");
}

// The other direction: a name only ever improves. A later scan that couldn't find the name this time (offline, a share
// that didn't answer) hands back just the title ID, and that must not rename a game that already has its real name.
TEST(GameIndexerServiceTest, RescanThatOnlyFoundTheTitleIdDoesNotRenameAGameBackToIt) {
    IndexerFixture fixture;
    ScannedVariant variant = makeVariant("PS5", "It Takes Two", "Europe", {makeVersion("1.00")});
    fixture.scanner.results = {variant};
    fixture.indexer.scanStorage(fixture.storageId);
    ASSERT_TRUE(fixture.gameRepository.getByTitle("It Takes Two").has_value());

    variant.title = "PPSA02343";  // what a scan that couldn't resolve the name returns
    fixture.scanner.results = {variant};
    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.variantsAdded, 0);
    EXPECT_EQ(result.gamesAdded, 0) << "no second Game row for the same release";
    EXPECT_TRUE(fixture.gameRepository.getByTitle("It Takes Two").has_value()) << "the real name must stay";
    EXPECT_FALSE(fixture.gameRepository.getByTitle("PPSA02343").has_value()) << "the title ID must not become a name";
}

// ...but a game that only ever had the title ID still gets its real name when one is found.
TEST(GameIndexerServiceTest, AGameNamedByItsTitleIdIsStillRenamedWhenARealNameArrives) {
    IndexerFixture fixture;
    ScannedVariant variant = makeVariant("PS5", "PPSA02343", "Europe", {makeVersion("1.00")});
    fixture.scanner.results = {variant};
    fixture.indexer.scanStorage(fixture.storageId);

    variant.title = "It Takes Two";
    fixture.scanner.results = {variant};
    fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_TRUE(fixture.gameRepository.getByTitle("It Takes Two").has_value());
    EXPECT_FALSE(fixture.gameRepository.getByTitle("PPSA02343").has_value());
}

// Same trap as the title had: a version detected on a later scan (a real one
// replacing the old made-up "1.00", or one set by hand in the sidecar) has to
// be written back onto the version row that already exists, or the Current
// Version column keeps showing whatever an older build saved the first time.
TEST(GameIndexerServiceTest, RescanUpdatesTheStoredVersionOfAnExistingVersionRow) {
    IndexerFixture fixture;
    ScannedVersion version = makeVersion("1.00");
    version.relativePath = "EP0006-PPSA02343_00-NUTSEU0000000000";
    ScannedVariant variant = makeVariant("PS5", "It Takes Two", "Europe", {version});
    variant.relativePath = "PPSA02343";
    fixture.scanner.results = {variant};
    ASSERT_TRUE(fixture.indexer.scanStorage(fixture.storageId).success);

    variant.versions[0].version = "01.300.000";
    fixture.scanner.results = {variant};
    const auto second = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(second.versionsAdded, 0) << "same version row, not a new one";
    EXPECT_EQ(second.versionsUpdated, 1);

    auto game = fixture.gameRepository.getByTitle("It Takes Two");
    ASSERT_TRUE(game.has_value());
    auto variants = fixture.variantRepository.getByGameId(game->gameId);
    ASSERT_EQ(variants.size(), 1u);
    auto versions = fixture.versionRepository.getByVariantId(variants[0].gameVariantId);
    ASSERT_EQ(versions.size(), 1u);
    EXPECT_EQ(versions[0].version, "01.300.000");
    EXPECT_TRUE(versions[0].isCurrent);
}

// Regression test for the failure seen on a real 100+ game library: "sqlite3_step
// failed: constraint failed", which aborted the whole scan of that drive so no
// name ever updated. Game titles are unique, and the Asia/Japan/Europe/North
// America releases of one game (consecutive PPSA IDs) each already had their
// own placeholder Game row - so once the online lookup gave all of them the
// same real name, renaming the second one to it collided with the first.
TEST(GameIndexerServiceTest, SiblingReleasesThatResolveToTheSameTitleJoinOneGameInsteadOfCollidingOnRename) {
    IndexerFixture fixture;

    auto release = [](const std::string& titleId, const std::string& region, const std::string& title) {
        ScannedVersion version = makeVersion("1.00");
        version.relativePath = "EP0006-" + titleId + "_00-LABEL00000000000";
        ScannedVariant variant = makeVariant("PS5", title, region, {version});
        variant.relativePath = titleId;  // stable per release, like the real scanner
        return variant;
    };

    fixture.scanner.results = {release("PPSA03071", "Asia", "PPSA03071"), release("PPSA03072", "Japan", "PPSA03072"),
                               release("PPSA03073", "Europe", "PPSA03073")};
    ASSERT_TRUE(fixture.indexer.scanStorage(fixture.storageId).success);
    ASSERT_EQ(fixture.gameRepository.getAll().size(), 3u);

    fixture.scanner.results = {release("PPSA03071", "Asia", "Cyberpunk 2077"),
                               release("PPSA03072", "Japan", "Cyberpunk 2077"),
                               release("PPSA03073", "Europe", "Cyberpunk 2077")};
    const auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(result.variantsFailed, 0) << result.firstVariantError;
    auto game = fixture.gameRepository.getByTitle("Cyberpunk 2077");
    ASSERT_TRUE(game.has_value());
    EXPECT_EQ(fixture.variantRepository.getByGameId(game->gameId).size(), 3u)
        << "all three regional releases should be listed under the one game";
    for (const char* stale : {"PPSA03071", "PPSA03072", "PPSA03073"}) {
        const auto leftover = fixture.gameRepository.getByTitle(stale);
        if (leftover) {
            EXPECT_TRUE(fixture.variantRepository.getByGameId(leftover->gameId).empty())
                << stale << " placeholder game must not still own a release";
        }
    }
}

// Renaming a Game in place would rename every release that shares it, so a
// release that improves while a sibling doesn't has to move to its own game.
TEST(GameIndexerServiceTest, ImprovingOneOfTwoReleasesSharingAGameDoesNotRenameTheOther) {
    IndexerFixture fixture;
    ScannedVariant europe = makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.06")});
    ScannedVariant usa = makeVariant("PS5", "Spider-Man 2", "USA", {makeVersion("1.05")});
    europe.relativePath = "PPSA00001";
    usa.relativePath = "PPSA00002";
    fixture.scanner.results = {europe, usa};
    ASSERT_TRUE(fixture.indexer.scanStorage(fixture.storageId).success);
    ASSERT_EQ(fixture.gameRepository.getAll().size(), 1u) << "both start out sharing one game";

    usa.title = "Spider-Man 2 Deluxe";
    fixture.scanner.results = {europe, usa};
    const auto result = fixture.indexer.scanStorage(fixture.storageId);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.variantsFailed, 0) << result.firstVariantError;

    auto original = fixture.gameRepository.getByTitle("Spider-Man 2");
    ASSERT_TRUE(original.has_value()) << "the sibling that didn't improve keeps its game and name";
    auto improved = fixture.gameRepository.getByTitle("Spider-Man 2 Deluxe");
    ASSERT_TRUE(improved.has_value());
    EXPECT_EQ(fixture.variantRepository.getByGameId(original->gameId).size(), 1u);
    EXPECT_EQ(fixture.variantRepository.getByGameId(improved->gameId).size(), 1u);
}

// A scan isn't transactional, so one release the database refuses used to
// abandon every release after it, leaving the library half-updated. It must
// be counted and reported, and the rest still saved.
TEST(GameIndexerServiceTest, OneReleaseTheDatabaseRefusesDoesNotAbandonTheRestOfTheScan) {
    IndexerFixture fixture;

    // Two entries for the same file path inside one version violate the
    // unique (version, path) rule on the file table.
    ScannedVersion broken = makeVersion("1.00", {ScannedFile{"dup.pkg", 10, "2026-01-01T00:00:00Z"},
                                                    ScannedFile{"dup.pkg", 10, "2026-01-01T00:00:00Z"}});
    ScannedVariant badRelease = makeVariant("PS5", "Broken Game", "Europe", {broken});
    badRelease.relativePath = "PPSA10001";
    ScannedVersion fine = makeVersion("1.00");
    fine.relativePath = "good-version";
    ScannedVariant goodRelease = makeVariant("PS5", "Good Game", "Europe", {fine});
    goodRelease.relativePath = "PPSA10002";

    fixture.scanner.results = {badRelease, goodRelease};
    const auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_TRUE(result.success) << "the scan as a whole completed";
    EXPECT_EQ(result.variantsFailed, 1);
    EXPECT_FALSE(result.firstVariantError.empty());
    EXPECT_NE(result.firstVariantError.find("Broken Game"), std::string::npos) << "say which release failed";
    ASSERT_TRUE(fixture.gameRepository.getByTitle("Good Game").has_value())
        << "the release after the failing one must still be saved";
}

// A database written by an older build can hold a release under a path today's
// scan no longer produces (the scanner's path scheme changed several times).
// Its version folder path (folder + content ID) never changed, so the rescan
// must adopt that row - not mark it Missing and create a duplicate beside it,
// which is how an old placeholder name ends up still listed next to the good
// one.
TEST(GameIndexerServiceTest, AdoptsALegacyVariantRowWhoseRelativePathSchemeChanged) {
    IndexerFixture fixture;
    ScannedVersion version = makeVersion("1.00");
    version.relativePath = "EP0006-PPSA20050_00-EASPORTSFC2025BG";

    ScannedVariant legacy = makeVariant("PS5", "EASPORTSFC 2025 BG", "Europe", {version});
    legacy.relativePath = "EASPORTSFC 2025 BG";  // old title-derived path
    fixture.scanner.results = {legacy};
    fixture.indexer.scanStorage(fixture.storageId);

    ScannedVariant current = legacy;
    current.title = "EA SPORTS FC 25";
    current.relativePath = "PPSA20050";  // today's title-ID-derived path
    fixture.scanner.results = {current};
    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.variantsAdded, 0) << "should adopt the existing row, not create a duplicate";
    EXPECT_EQ(result.versionsAdded, 0);

    auto game = fixture.gameRepository.getByTitle("EA SPORTS FC 25");
    ASSERT_TRUE(game.has_value());
    auto variants = fixture.variantRepository.getByGameId(game->gameId);
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].relativePath, "PPSA20050");
    EXPECT_EQ(variants[0].status, GameVariantStatus::Active);
    EXPECT_EQ(fixture.variantRepository.getByStorageId(fixture.storageId).size(), 1u)
        << "no leftover Missing ghost of the old row";
}

// Databases that already went through a build which created a fresh variant
// beside the old one hold a Missing ghost carrying the outdated title. Same
// files as an Active variant means it's a stale duplicate, not a game that's
// gone - the Games list must not show it.
TEST(GameIndexerServiceTest, LibraryHidesAMissingGhostWhoseFilesBelongToAnActiveVariant) {
    IndexerFixture fixture;
    GameLibraryService library(fixture.gameRepository, fixture.variantRepository, fixture.versionRepository);
    const std::string sharedVersionPath = "EP0006-PPSA02343_00-NUTSEU0000000000";

    auto addRow = [&](const std::string& title, const std::string& variantPath, GameVariantStatus status) {
        Game game;
        game.title = title;
        fixture.gameRepository.add(game);

        GameVariant variant;
        variant.gameId = game.gameId;
        variant.platform = "PS5";
        variant.region = "Europe";
        variant.storageId = fixture.storageId;
        variant.relativePath = variantPath;
        variant.status = status;
        fixture.variantRepository.add(variant);

        GameVersion version;
        version.gameVariantId = variant.gameVariantId;
        version.version = "1.00";
        version.relativePath = sharedVersionPath;
        version.status = status;
        version.isCurrent = status == GameVariantStatus::Active;
        fixture.versionRepository.add(version);
    };
    addRow("PPSA02343", "PPSA02343", GameVariantStatus::Missing);         // stale ghost, outdated title
    addRow("It Takes Two", "It Takes Two", GameVariantStatus::Active);    // the real, current row

    const auto entries = library.getAllEntries();
    ASSERT_EQ(entries.size(), 1u) << "the stale Missing duplicate should be hidden";
    EXPECT_EQ(entries[0].title, "It Takes Two");
    EXPECT_EQ(entries[0].variantStatus, GameVariantStatus::Active);
}

TEST(GameIndexerServiceTest, LibraryStillListsAMissingVariantWhoseFilesAreReallyGone) {
    IndexerFixture fixture;
    GameLibraryService library(fixture.gameRepository, fixture.variantRepository, fixture.versionRepository);

    fixture.scanner.results = {makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.06")})};
    fixture.indexer.scanStorage(fixture.storageId);
    fixture.scanner.results = {};
    fixture.indexer.scanStorage(fixture.storageId);

    const auto entries = library.getAllEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].variantStatus, GameVariantStatus::Missing);
}

TEST(GameLibraryServiceTest, HidesEntriesWhenTheirStorageIsOfflineButKeepsTheIndex) {
    IndexerFixture fixture;
    GameLibraryService library(fixture.gameRepository, fixture.variantRepository, fixture.versionRepository,
                               &fixture.storageRepository);

    auto onlineStorage = fixture.storageRepository.getById(fixture.storageId);
    ASSERT_TRUE(onlineStorage.has_value());
    onlineStorage->status = StorageStatus::Online;
    onlineStorage->isAvailable = true;
    fixture.storageRepository.update(*onlineStorage);

    fixture.scanner.results = {makeVariant("PS5", "Grand Theft Auto V", "Europe", {makeVersion("1.00")})};
    ASSERT_TRUE(fixture.indexer.scanStorage(fixture.storageId).success);
    ASSERT_EQ(library.getAllEntries().size(), 1u);

    auto storage = fixture.storageRepository.getById(fixture.storageId);
    ASSERT_TRUE(storage.has_value());
    storage->status = StorageStatus::Offline;
    storage->isAvailable = false;
    fixture.storageRepository.update(*storage);

    EXPECT_TRUE(library.getAllEntries().empty());
    EXPECT_EQ(fixture.variantRepository.getByStorageId(fixture.storageId).size(), 1u);
}

TEST(GameIndexerServiceTest, MarksVersionMissingWhenItsFolderDisappearsButVariantRemains) {
    IndexerFixture fixture;
    fixture.scanner.results = {
        makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.04"), makeVersion("1.06")})};
    fixture.indexer.scanStorage(fixture.storageId);

    // 1.04 cleaned up by retention policy, 1.06 remains.
    fixture.scanner.results = {makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.06")})};
    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.versionsMissing, 1);

    auto game = fixture.gameRepository.getByTitle("Spider-Man 2");
    auto variants = fixture.variantRepository.getByGameId(game->gameId);
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].status, GameVariantStatus::Active);  // variant itself still present

    auto versions = fixture.versionRepository.getByVariantId(variants[0].gameVariantId);
    ASSERT_EQ(versions.size(), 2u);
    for (const auto& v : versions) {
        if (v.version == "1.04") {
            EXPECT_EQ(v.status, GameVariantStatus::Missing);
            EXPECT_FALSE(v.isCurrent);
        } else {
            EXPECT_EQ(v.status, GameVariantStatus::Active);
            EXPECT_TRUE(v.isCurrent);
        }
    }
}

TEST(GameIndexerServiceTest, MarksVariantAndAllItsVersionsMissingWhenContainerDisappears) {
    IndexerFixture fixture;
    fixture.scanner.results = {makeVariant("PS5", "Spider-Man 2", "Europe", {makeVersion("1.06")})};
    fixture.indexer.scanStorage(fixture.storageId);

    fixture.scanner.results = {};  // whole container gone
    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.versionsMissing, 1);

    auto game = fixture.gameRepository.getByTitle("Spider-Man 2");
    auto variants = fixture.variantRepository.getByGameId(game->gameId);
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].status, GameVariantStatus::Missing);

    auto versions = fixture.versionRepository.getByVariantId(variants[0].gameVariantId);
    ASSERT_EQ(versions.size(), 1u);
    EXPECT_EQ(versions[0].status, GameVariantStatus::Missing);
}

TEST(GameIndexerServiceTest, ReconcilesAddedModifiedAndRemovedFilesWithinAVersion) {
    IndexerFixture fixture;
    fixture.scanner.results = {makeVariant(
        "PS5", "Spider-Man 2", "Europe",
        {makeVersion("1.06", {ScannedFile{"a.bin", 100, "2026-01-01T00:00:00Z"},
                               ScannedFile{"b.bin", 200, "2026-01-01T00:00:00Z"}})})};
    fixture.indexer.scanStorage(fixture.storageId);

    // a.bin modified (size changes), b.bin removed, c.bin added.
    fixture.scanner.results = {makeVariant(
        "PS5", "Spider-Man 2", "Europe",
        {makeVersion("1.06", {ScannedFile{"a.bin", 150, "2026-02-01T00:00:00Z"},
                               ScannedFile{"c.bin", 50, "2026-02-01T00:00:00Z"}})})};
    auto result = fixture.indexer.scanStorage(fixture.storageId);

    EXPECT_EQ(result.versionsUpdated, 1);

    auto game = fixture.gameRepository.getByTitle("Spider-Man 2");
    auto variants = fixture.variantRepository.getByGameId(game->gameId);
    auto versions = fixture.versionRepository.getByVariantId(variants[0].gameVariantId);
    auto files = fixture.fileRepository.getByVersionId(versions[0].gameVersionId);

    ASSERT_EQ(files.size(), 2u);
    bool foundA = false, foundC = false;
    for (const auto& f : files) {
        if (f.relativePath == "a.bin") {
            foundA = true;
            EXPECT_EQ(f.sizeBytes, 150);
        }
        if (f.relativePath == "c.bin") foundC = true;
        EXPECT_NE(f.relativePath, "b.bin");
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundC);
}

// ---- Versions the user supplies (PS5 dumps carry no readable version) ------

namespace {

constexpr const char* kItTakesTwoContentId = "EP0006-PPSA02343_00-NUTSEU0000000000";
constexpr const char* kFc25ContentId = "EP0006-PPSA20050_00-EASPORTSFC2025BG";

// One PS5 release whose only version is keyed by "<folder>/<Content ID>", the
// way the real scanner reports it, with no version detected.
ScannedVariant makePs5Release(const std::string& title, const std::string& titleId, const std::string& contentId) {
    ScannedVersion version;
    version.version = "";
    version.relativePath = (std::filesystem::path("PS5") / contentId).string();
    version.files = {ScannedFile{contentId + "_0.pkg", 100, "2026-01-01T00:00:00Z"}};

    ScannedVariant variant;
    variant.platform = "PS5";
    variant.title = title;
    variant.region = "Europe";
    variant.relativePath = (std::filesystem::path("PS5") / titleId).string();
    variant.versions.push_back(std::move(version));
    return variant;
}

std::vector<GameLibraryEntry> libraryEntries(IndexerFixture& fixture) {
    return GameLibraryService(fixture.gameRepository, fixture.variantRepository, fixture.versionRepository)
        .getAllEntries();
}

std::string currentVersionOf(IndexerFixture& fixture, const std::string& title) {
    for (const auto& entry : libraryEntries(fixture)) {
        if (entry.title == title) return entry.currentVersion;
    }
    return "<no such game>";
}

std::string variantIdOf(IndexerFixture& fixture, const std::string& title) {
    for (const auto& entry : libraryEntries(fixture)) {
        if (entry.title == title) return entry.gameVariantId;
    }
    return {};
}

}  // namespace

TEST(GameLibraryServiceTest, EntriesCarryTheTitleIdFromTheContentId) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId)};
    fixture.indexer.scanStorage(fixture.storageId);

    const auto entries = libraryEntries(fixture);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].titleId, "PPSA02343");
}

TEST(GameIndexerServiceTest, SetVersionSavesNextToTheFilesAndUpdatesTheLibrary) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId)};
    fixture.indexer.scanStorage(fixture.storageId);
    ASSERT_EQ(currentVersionOf(fixture, "It Takes Two"), "");

    const auto result = fixture.indexer.setVersion(variantIdOf(fixture, "It Takes Two"), "  v01.300.000 ");

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(currentVersionOf(fixture, "It Takes Two"), "01.300.000");
    ASSERT_EQ(fixture.scanner.pins.size(), 1u);
    EXPECT_EQ(fixture.scanner.pins[0].rootPath, fixture.storageRepository.getById(fixture.storageId)->rootPath);
    EXPECT_EQ(fixture.scanner.pins[0].versionRelativePath,
              (std::filesystem::path("PS5") / kItTakesTwoContentId).string());
    EXPECT_EQ(fixture.scanner.pins[0].version, "01.300.000");
}

TEST(GameIndexerServiceTest, SetVersionRejectsTextThatIsNotAVersion) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId)};
    fixture.indexer.scanStorage(fixture.storageId);
    const std::string variantId = variantIdOf(fixture, "It Takes Two");

    for (const char* bad : {"latest", "1", "1..2", ".5", "1.2.3.4.5", "1.2 beta", "12345.1"}) {
        const auto result = fixture.indexer.setVersion(variantId, bad);
        EXPECT_FALSE(result.success) << bad;
        EXPECT_FALSE(result.errorMessage.empty()) << bad;
    }
    EXPECT_TRUE(fixture.scanner.pins.empty());
    EXPECT_EQ(currentVersionOf(fixture, "It Takes Two"), "");
}

TEST(GameIndexerServiceTest, SetVersionLeavesTheLibraryAloneWhenItCannotBeSaved) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId)};
    fixture.indexer.scanStorage(fixture.storageId);
    fixture.scanner.pinFailure = "The game's folder isn't reachable right now.";

    const auto result = fixture.indexer.setVersion(variantIdOf(fixture, "It Takes Two"), "01.300");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorMessage, "The game's folder isn't reachable right now.");
    EXPECT_EQ(currentVersionOf(fixture, "It Takes Two"), "")
        << "showing a version that was never saved would just vanish on the next scan";
}

TEST(GameIndexerServiceTest, SetVersionWithNothingClearsItForAutomaticDetection) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId)};
    fixture.indexer.scanStorage(fixture.storageId);
    const std::string variantId = variantIdOf(fixture, "It Takes Two");
    ASSERT_TRUE(fixture.indexer.setVersion(variantId, "01.300").success);

    const auto result = fixture.indexer.setVersion(variantId, "   ");

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(fixture.scanner.pins.back().version, "");
    EXPECT_EQ(currentVersionOf(fixture, "It Takes Two"), "");
}

TEST(GameIndexerServiceTest, SetVersionOnAnUnknownGameSaysSo) {
    IndexerFixture fixture;
    const auto result = fixture.indexer.setVersion("no-such-variant", "01.00");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST(GameIndexerServiceTest, ImportVersionsAppliesEachLineToTheGameWithThatTitleId) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId),
                               makePs5Release("EA SPORTS FC 25", "PPSA20050", kFc25ContentId)};
    fixture.indexer.scanStorage(fixture.storageId);

    const auto result = fixture.indexer.importVersions(
        "titleid,version\n"
        "# copied from my console\n"
        "\n"
        "PPSA02343,01.300.000\r\n"
        "ppsa20050_00\t1.12\n"
        "PPSA99999 01.00\n"
        "this line means nothing\n");

    EXPECT_EQ(result.applied, 2);
    EXPECT_EQ(result.failed, 0);
    ASSERT_EQ(result.unmatchedTitleIds.size(), 1u);
    EXPECT_EQ(result.unmatchedTitleIds[0], "PPSA99999");
    EXPECT_EQ(result.linesIgnored, 2) << "the header line and the nonsense line";
    EXPECT_EQ(currentVersionOf(fixture, "It Takes Two"), "01.300.000");
    EXPECT_EQ(currentVersionOf(fixture, "EA SPORTS FC 25"), "1.12");
}

TEST(GameIndexerServiceTest, ImportVersionsAcceptsGameNamesAndContentIdsAndTakesTheLastVersionOnTheLine) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId),
                               makePs5Release("EA SPORTS FC 25", "PPSA20050", kFc25ContentId)};
    fixture.indexer.scanStorage(fixture.storageId);

    const auto result = fixture.indexer.importVersions(
        "PPSA02343, It Takes Two, 01.300.000\n"
        "\"EP0006-PPSA20050_00-EASPORTSFC2025BG\";\"v1.05\";\"1.12\"\n");

    EXPECT_EQ(result.applied, 2);
    EXPECT_EQ(currentVersionOf(fixture, "It Takes Two"), "01.300.000");
    EXPECT_EQ(currentVersionOf(fixture, "EA SPORTS FC 25"), "1.12");
}

TEST(GameIndexerServiceTest, ImportVersionsReportsSaveFailuresWithoutStoppingTheRest) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId)};
    fixture.indexer.scanStorage(fixture.storageId);
    fixture.scanner.pinFailure = "Couldn't write next to the game files.";

    const auto result = fixture.indexer.importVersions("PPSA02343 01.300\n");

    EXPECT_EQ(result.applied, 0);
    EXPECT_EQ(result.failed, 1);
    EXPECT_NE(result.firstError.find("PPSA02343"), std::string::npos) << result.firstError;
    EXPECT_NE(result.firstError.find("Couldn't write"), std::string::npos) << result.firstError;
    EXPECT_TRUE(result.unmatchedTitleIds.empty());
}

TEST(GameIndexerServiceTest, ImportVersionsOfAnEmptyListDoesNothing) {
    IndexerFixture fixture;
    fixture.scanner.results = {makePs5Release("It Takes Two", "PPSA02343", kItTakesTwoContentId)};
    fixture.indexer.scanStorage(fixture.storageId);

    const auto result = fixture.indexer.importVersions("");

    EXPECT_EQ(result.applied, 0);
    EXPECT_EQ(result.linesIgnored, 0);
    EXPECT_TRUE(fixture.scanner.pins.empty());
}
