#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "gth/application/storage/StorageService.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"
#include "gth/database/StorageRepository.h"

using namespace gth::application;
using namespace gth::core;
using namespace gth::database;

namespace {

class EmptyStorageDetector : public IStorageDetector {
public:
    std::vector<DetectedStorageInfo> detect() override { return {}; }
};

}  // namespace

TEST(StorageServiceCategoryTest, CreatesTrimmedCategoriesAndRejectsDuplicateNames) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);
    EmptyStorageDetector detector;
    StorageService service(repo, detector);

    const auto created = service.createCategory("  PlayStation HDDs  ");

    ASSERT_TRUE(created.success);
    auto categories = service.getCategories();
    ASSERT_EQ(categories.size(), 1u);
    EXPECT_EQ(categories[0].name, "PlayStation HDDs");

    const auto duplicate = service.createCategory("playstation hdds");
    EXPECT_FALSE(duplicate.success);
    EXPECT_FALSE(duplicate.errorMessage.empty());
    EXPECT_EQ(service.getCategories().size(), 1u);
}

TEST(StorageServiceCategoryTest, StoragesCanBelongToMultipleCategories) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);
    EmptyStorageDetector detector;
    StorageService service(repo, detector);

    Storage drive1;
    drive1.name = "HDD 1";
    drive1.rootPath = "D:\\";
    repo.add(drive1);

    Storage drive2;
    drive2.name = "HDD 2";
    drive2.rootPath = "E:\\";
    repo.add(drive2);

    const auto firstCategory = service.createCategory("PS4");
    const auto secondCategory = service.createCategory("Archive");
    ASSERT_TRUE(firstCategory.success);
    ASSERT_TRUE(secondCategory.success);

    EXPECT_TRUE(service.addStorageToCategory(firstCategory.categoryId, drive1.storageId).success);
    EXPECT_TRUE(service.addStorageToCategory(firstCategory.categoryId, drive2.storageId).success);
    EXPECT_TRUE(service.addStorageToCategory(secondCategory.categoryId, drive1.storageId).success);

    EXPECT_EQ(service.getStorageIdsForCategory(firstCategory.categoryId),
              (std::vector<std::string>{drive1.storageId, drive2.storageId}));
    EXPECT_EQ(service.getStorageIdsForCategory(secondCategory.categoryId), (std::vector<std::string>{drive1.storageId}));
    EXPECT_EQ(service.getAllStorages().size(), 2u);
}

TEST(StorageServiceCategoryTest, RenamingAndDeletingCategoryPreservesItsStorages) {
    Database db(":memory:");
    Migrator::applyMigrations(db);
    StorageRepository repo(db);
    EmptyStorageDetector detector;
    StorageService service(repo, detector);

    Storage drive;
    drive.name = "HDD 1";
    drive.rootPath = "D:\\";
    repo.add(drive);

    const auto category = service.createCategory("Old name");
    ASSERT_TRUE(category.success);
    ASSERT_TRUE(service.addStorageToCategory(category.categoryId, drive.storageId).success);

    const auto renamed = service.renameCategory(category.categoryId, "New name");
    ASSERT_TRUE(renamed.success);
    const auto renamedCategories = service.getCategories();
    ASSERT_EQ(renamedCategories.size(), 1u);
    EXPECT_EQ(renamedCategories[0].name, "New name");
    EXPECT_EQ(service.getStorageIdsForCategory(category.categoryId), (std::vector<std::string>{drive.storageId}));

    ASSERT_TRUE(service.deleteCategory(category.categoryId).success);
    EXPECT_TRUE(service.getCategories().empty());
    EXPECT_TRUE(service.getStorageIdsForCategory(category.categoryId).empty());
    ASSERT_TRUE(repo.getById(drive.storageId).has_value());
    EXPECT_EQ(repo.getAll().size(), 1u);
}
