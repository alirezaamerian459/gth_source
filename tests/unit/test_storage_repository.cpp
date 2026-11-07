#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "gth/database/Database.h"
#include "gth/database/GameFileRepository.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"

using namespace gth::core;
using namespace gth::database;

TEST(StorageRepositoryTest, AddPersistsStorageSoItAppearsInGetAll) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage storage;
    storage.name = "Games HDD";
    storage.rootPath = "D:\\";
    storage.type = StorageType::LocalHdd;

    repo.add(storage);
    auto all = repo.getAll();

    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].name, "Games HDD");
}

TEST(StorageRepositoryTest, GetByRootPathReturnsMatchingStorage) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage storage;
    storage.name = "NAS";
    storage.rootPath = "\\\\NAS01\\Games";
    storage.type = StorageType::Nas;
    repo.add(storage);

    auto found = repo.getByRootPath("\\\\NAS01\\Games");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "NAS");
}

TEST(StorageRepositoryTest, GetByRootPathReturnsNulloptWhenNoMatch) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    auto found = repo.getByRootPath("E:\\");

    EXPECT_FALSE(found.has_value());
}

TEST(StorageRepositoryTest, RemoveDeletesStorage) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage storage;
    storage.name = "USB";
    storage.rootPath = "F:\\";
    storage.type = StorageType::Usb;
    repo.add(storage);

    repo.remove(storage.storageId);
    auto all = repo.getAll();

    EXPECT_TRUE(all.empty());
}

TEST(StorageRepositoryTest, RemoveCascadesToItsGamesVariantsVersionsAndFiles) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository storageRepo(db);
    GameRepository gameRepo(db);
    GameVariantRepository variantRepo(db);
    GameVersionRepository versionRepo(db);
    GameFileRepository fileRepo(db);

    Storage storage;
    storage.name = "C:";
    storage.rootPath = "C:\\";
    storage.type = StorageType::LocalHdd;
    storageRepo.add(storage);

    Game game;
    game.title = "WindowsApps";
    gameRepo.add(game);

    GameVariant variant;
    variant.gameId = game.gameId;
    variant.storageId = storage.storageId;
    variant.relativePath = "Program Files/WindowsApps";
    variantRepo.add(variant);

    GameVersion version;
    version.gameVariantId = variant.gameVariantId;
    version.relativePath = "Program Files/WindowsApps/Microsoft.XboxApp";
    versionRepo.add(version);

    GameFile file;
    file.gameVersionId = version.gameVersionId;
    file.relativePath = "AppxManifest.xml";
    fileRepo.add(file);

    storageRepo.remove(storage.storageId);

    EXPECT_TRUE(gameRepo.getAll().empty());
    EXPECT_TRUE(variantRepo.getByStorageId(storage.storageId).empty());
    EXPECT_TRUE(versionRepo.getByVariantId(variant.gameVariantId).empty());
    EXPECT_TRUE(fileRepo.getByVersionId(version.gameVersionId).empty());
}

TEST(StorageRepositoryTest, RemoveOnlyPrunesGamesLeftWithNoVariantsAtAll) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository storageRepo(db);
    GameRepository gameRepo(db);
    GameVariantRepository variantRepo(db);

    Storage storageA;
    storageA.name = "A";
    storageA.rootPath = "D:\\";
    storageA.type = StorageType::LocalHdd;
    storageRepo.add(storageA);

    Storage storageB;
    storageB.name = "B";
    storageB.rootPath = "E:\\";
    storageB.type = StorageType::LocalHdd;
    storageRepo.add(storageB);

    Game game;
    game.title = "Shared Title";
    gameRepo.add(game);

    // Same game has a variant on each storage - removing one storage must
    // not take the game (or the other storage's variant) down with it.
    GameVariant variantOnA;
    variantOnA.gameId = game.gameId;
    variantOnA.storageId = storageA.storageId;
    variantOnA.relativePath = "PS4/Title/USA";
    variantRepo.add(variantOnA);

    GameVariant variantOnB;
    variantOnB.gameId = game.gameId;
    variantOnB.storageId = storageB.storageId;
    variantOnB.relativePath = "PS4/Title/USA";
    variantRepo.add(variantOnB);

    storageRepo.remove(storageA.storageId);

    ASSERT_EQ(gameRepo.getAll().size(), 1u);
    EXPECT_TRUE(variantRepo.getByStorageId(storageA.storageId).empty());
    EXPECT_EQ(variantRepo.getByStorageId(storageB.storageId).size(), 1u);
}

TEST(StorageRepositoryTest, HealthFieldsRoundTripWhenSet) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage storage;
    storage.name = "Games HDD";
    storage.rootPath = "D:\\";
    storage.type = StorageType::LocalHdd;
    storage.healthPercent = 92;
    storage.temperatureCelsius = 41;
    repo.add(storage);

    auto found = repo.getByRootPath("D:\\");
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(found->healthPercent.has_value());
    EXPECT_EQ(*found->healthPercent, 92);
    ASSERT_TRUE(found->temperatureCelsius.has_value());
    EXPECT_EQ(*found->temperatureCelsius, 41);
}

TEST(StorageRepositoryTest, HealthFieldsStayUnknownWhenNeverSet) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage storage;
    storage.name = "USB";
    storage.rootPath = "F:\\";
    storage.type = StorageType::Usb;
    repo.add(storage);

    auto found = repo.getByRootPath("F:\\");
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->healthPercent.has_value());
    EXPECT_FALSE(found->temperatureCelsius.has_value());
}

TEST(StorageRepositoryTest, UpdateChangesPersistedValues) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage storage;
    storage.name = "Old Name";
    storage.rootPath = "G:\\";
    storage.type = StorageType::ExternalHdd;
    storage.freeSpaceBytes = 100;
    repo.add(storage);

    storage.name = "New Name";
    storage.freeSpaceBytes = 200;
    repo.update(storage);

    auto found = repo.getByRootPath("G:\\");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "New Name");
    EXPECT_EQ(found->freeSpaceBytes, 200);
}

TEST(StorageRepositoryTest, CategoryMembershipsPersistManyToManyAndCategoryDeletionKeepsStorages) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);

    Storage drive1;
    drive1.name = "HDD 1";
    drive1.rootPath = "D:\\";
    repo.add(drive1);

    Storage drive2;
    drive2.name = "HDD 2";
    drive2.rootPath = "E:\\";
    repo.add(drive2);

    StorageCategory category1;
    category1.name = "PlayStation 4";
    repo.addCategory(category1);

    StorageCategory category2;
    category2.name = "Archive";
    repo.addCategory(category2);

    repo.addStorageToCategory(category1.categoryId, drive1.storageId);
    repo.addStorageToCategory(category1.categoryId, drive2.storageId);
    repo.addStorageToCategory(category2.categoryId, drive1.storageId);

    const auto categories = repo.getCategories();
    ASSERT_EQ(categories.size(), 2u);
    EXPECT_EQ(repo.getStorageIdsForCategory(category1.categoryId),
              (std::vector<std::string>{drive1.storageId, drive2.storageId}));
    EXPECT_EQ(repo.getStorageIdsForCategory(category2.categoryId), (std::vector<std::string>{drive1.storageId}));

    repo.removeCategory(category1.categoryId);

    EXPECT_FALSE(repo.getCategoryById(category1.categoryId).has_value());
    EXPECT_TRUE(repo.getById(drive1.storageId).has_value());
    EXPECT_TRUE(repo.getById(drive2.storageId).has_value());
    EXPECT_EQ(repo.getStorageIdsForCategory(category2.categoryId), (std::vector<std::string>{drive1.storageId}));

    repo.remove(drive1.storageId);
    EXPECT_TRUE(repo.getStorageIdsForCategory(category2.categoryId).empty());
}
