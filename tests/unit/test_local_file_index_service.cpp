#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "gth/application/indexing/GameIndexerService.h"
#include "gth/application/storage/ILocalFileIndex.h"
#include "gth/application/storage/LocalFileIndexService.h"
#include "gth/database/Database.h"
#include "gth/database/GameFileRepository.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/ScanJobRepository.h"
#include "gth/database/StorageRepository.h"
#include "gth/indexing/EnhancedStorageScanner.h"

using namespace gth::core;
using namespace gth::database;
using gth::application::LocalFileIndexService;
namespace fs = std::filesystem;

namespace {

class LocalFileIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        Migrator::applyMigrations(db);
        root = gth::test::testTempRoot() / ("gth_index_test_" + std::to_string(::GetCurrentProcessId()) + "_" +
                                            std::to_string(counter++));
        fs::create_directories(root);

        Storage storage;
        storage.name = "Games HDD";
        storage.rootPath = root.string();
        storage.type = StorageType::LocalHdd;
        storageRepo.add(storage);
        storageId = storage.storageId;
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path touch(const fs::path& rel, const std::string& content = "x") {
        const auto p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
        return p;
    }

    // Records one file the way the given scanner would have.
    void record(const std::string& variantRel, const std::string& versionRel, const std::string& fileRel,
                GameVariantStatus variantStatus = GameVariantStatus::Active,
                GameVariantStatus versionStatus = GameVariantStatus::Active, bool isCurrent = true) {
        Game game;
        game.title = "Some Game " + std::to_string(counter++);
        gameRepo.add(game);

        GameVariant variant;
        variant.gameId = game.gameId;
        variant.storageId = storageId;
        variant.relativePath = variantRel;
        variant.status = variantStatus;
        variantRepo.add(variant);

        GameVersion version;
        version.gameVariantId = variant.gameVariantId;
        version.relativePath = versionRel;
        version.status = versionStatus;
        version.isCurrent = isCurrent;
        versionRepo.add(version);

        GameFile file;
        file.gameVersionId = version.gameVersionId;
        file.relativePath = fileRel;
        fileRepo.add(file);
    }

    static inline int counter = 0;
    Database db{":memory:"};
    StorageRepository storageRepo{db};
    GameRepository gameRepo{db};
    GameVariantRepository variantRepo{db};
    GameVersionRepository versionRepo{db};
    GameFileRepository fileRepo{db};
    LocalFileIndexService index{storageRepo, variantRepo, versionRepo, fileRepo};
    fs::path root;
    std::string storageId;
};

}  // namespace

TEST_F(LocalFileIndexTest, FindsAFileRecordedRelativeToTheStorageRoot) {
    // the .pkg scanner: file path is relative to the storage root
    const auto file = touch("PS4/Loose/UP0001-CUSA00001_00-GAME-A0100-V0100.pkg");
    record("PS4/Loose/CUSA00001", "PS4/Loose/CUSA00001_00", "PS4/Loose/UP0001-CUSA00001_00-GAME-A0100-V0100.pkg");

    const auto found = index.findByFileName("UP0001-CUSA00001_00-GAME-A0100-V0100.pkg");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(fs::weakly_canonical(*found), fs::weakly_canonical(file));
}

TEST_F(LocalFileIndexTest, FindsAFileRecordedRelativeToItsVersionFolder) {
    // the folder-convention scanner: version path is from the root, file path is inside the version
    const auto file = touch("PS5/Some Game/USA/1.00/game_0.pkg");
    record("PS5/Some Game/USA", "PS5/Some Game/USA/1.00", "game_0.pkg");

    const auto found = index.findByFileName("game_0.pkg");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(fs::weakly_canonical(*found), fs::weakly_canonical(file));
}

TEST_F(LocalFileIndexTest, MatchesRegardlessOfCase) {
    touch("a/Game_0.PKG");
    record("a/v", "a/v1", "a/Game_0.PKG");

    EXPECT_TRUE(index.findByFileName("game_0.pkg").has_value());
    EXPECT_TRUE(index.findByFileName("GAME_0.PKG").has_value());
}

TEST_F(LocalFileIndexTest, FindsOlderVersionsNotJustTheCurrentOne) {
    touch("g/old_patch.pkg");
    record("g/v", "g/v_old", "g/old_patch.pkg", GameVariantStatus::Active, GameVariantStatus::Active,
           /*isCurrent=*/false);

    EXPECT_TRUE(index.findByFileName("old_patch.pkg").has_value());
}

TEST_F(LocalFileIndexTest, SkipsFilesThatNoLongerExistOnDisk) {
    record("g/v", "g/v1", "g/deleted.pkg");  // recorded by a past scan, since deleted

    EXPECT_FALSE(index.findByFileName("deleted.pkg").has_value());
}

TEST_F(LocalFileIndexTest, SkipsMissingVariantsAndVersions) {
    touch("m/missing_variant.pkg");
    touch("m/missing_version.pkg");
    record("m/v", "m/v1", "m/missing_variant.pkg", GameVariantStatus::Missing);
    record("m/w", "m/w1", "m/missing_version.pkg", GameVariantStatus::Active, GameVariantStatus::Missing);

    EXPECT_FALSE(index.findByFileName("missing_variant.pkg").has_value());
    EXPECT_FALSE(index.findByFileName("missing_version.pkg").has_value());
}

TEST_F(LocalFileIndexTest, ReturnsNothingForAnUnknownOrEmptyName) {
    touch("g/known.pkg");
    record("g/v", "g/v1", "g/known.pkg");

    EXPECT_FALSE(index.findByFileName("something_else.pkg").has_value());
    EXPECT_FALSE(index.findByFileName("").has_value());
}

// The real thing end to end: the real .pkg scanner + indexer write the database, and the index must
// find the files through whatever path shape the scanner really recorded.
TEST_F(LocalFileIndexTest, FindsFilesAfterARealScanOfTheStorage) {
    const auto flatPart0 = touch("EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const auto flatPart1 = touch("EP0006-PPSA02343_00-NUTSEU0000000000_1.pkg");
    const auto nested = touch("My PS4 Games/UP9000-CUSA12345_00-GAMENAME00000000-A0100-V0100.pkg");

    gth::indexing::EnhancedStorageScanner scanner;
    ScanJobRepository scanJobRepo{db};
    gth::application::GameIndexerService indexer{storageRepo, scanner, gameRepo, variantRepo, versionRepo, fileRepo, scanJobRepo};
    const auto scan = indexer.scanStorage(storageId);
    ASSERT_TRUE(scan.success) << scan.errorMessage;

    for (const auto& [name, path] : {std::pair<std::string, fs::path>{"EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg", flatPart0},
                                     {"EP0006-PPSA02343_00-NUTSEU0000000000_1.pkg", flatPart1},
                                     {"UP9000-CUSA12345_00-GAMENAME00000000-A0100-V0100.pkg", nested}}) {
        const auto found = index.findByFileName(name);
        ASSERT_TRUE(found.has_value()) << name;
        EXPECT_EQ(fs::weakly_canonical(*found), fs::weakly_canonical(path)) << name;
    }
}

TEST(PackageFileNameTest, ExtractsTheRequestedFileNameFromAUrl) {
    using gth::application::packageFileNameFromUrl;
    EXPECT_EQ(packageFileNameFromUrl("http://gs2.ww.prod.dl.playstation.net/gs2/ppkgo/prod/CUSA1/1/f_abc/f/UP1-CUSA1_00-G-V0100.pkg"),
              "UP1-CUSA1_00-G-V0100.pkg");
    EXPECT_EQ(packageFileNameFromUrl("http://cdn.example.com/a/b/file_0.pkg?country=hk&x=1"), "file_0.pkg");
    EXPECT_EQ(packageFileNameFromUrl("http://cdn.example.com/a/b/file_0.pkg#frag"), "file_0.pkg");
    EXPECT_EQ(packageFileNameFromUrl("nourl"), "");
}

TEST(PackageFileNameTest, OnlyPackageAndFirmwareNamesAreServable) {
    using gth::application::isServablePackageName;
    EXPECT_TRUE(isServablePackageName("game.pkg"));
    EXPECT_TRUE(isServablePackageName("GAME_0.PKG"));
    EXPECT_TRUE(isServablePackageName("PS4UPDATE.PUP"));
    EXPECT_FALSE(isServablePackageName("manifest.json"));
    EXPECT_FALSE(isServablePackageName("index.html"));
    EXPECT_FALSE(isServablePackageName(""));
}
