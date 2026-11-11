#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gth/application/jb/JbLibraryService.h"

namespace fs = std::filesystem;
using namespace gth::application;
using namespace gth::core;

namespace {

std::string pathUtf8(const fs::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

class FakeStorageService : public IStorageService {
public:
    std::vector<Storage> storages;

    std::vector<Storage> getAllStorages() override { return storages; }
    std::vector<StorageCategory> getCategories() override { return {}; }
    std::vector<std::string> getStorageIdsForCategory(const std::string&) override { return {}; }
    StorageCategoryResult createCategory(const std::string&) override { return {}; }
    StorageCategoryResult renameCategory(const std::string&, const std::string&) override { return {}; }
    StorageCategoryResult deleteCategory(const std::string&) override { return {}; }
    StorageCategoryResult addStorageToCategory(const std::string&, const std::string&) override { return {}; }
    StorageCategoryResult removeStorageFromCategory(const std::string&, const std::string&) override { return {}; }
    StorageSyncResult detectAndSyncLocalStorages() override { return {}; }
    AddStorageResult addManualStorage(const std::string&) override { return {}; }
    StorageSummary getSummary() override { return {}; }
};

struct JbLibraryServiceFixture {
    fs::path root = gth::test::testTempRoot() / ("gth_jb_library_" + Guid::newGuid());
    FakeStorageService storage;
    JbLibraryService service{storage};

    JbLibraryServiceFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "hacked" / "CUSA00001_Game");
        fs::create_directories(root / "p30day" / "PPSA00002_Game");
        std::ofstream(root / "hacked" / "CUSA00001_Game" / "CUSA00001.pkg") << "pkg";
        std::ofstream(root / "p30day" / "PPSA00002_Game" / "PPSA00002.fpkg") << "fpkg";

        Storage drive;
        drive.storageId = "drive-1";
        drive.name = "Test JB Drive";
        drive.type = StorageType::LocalHdd;
        drive.rootPath = pathUtf8(root);
        drive.isAvailable = true;
        storage.storages.push_back(std::move(drive));
    }

    ~JbLibraryServiceFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

}  // namespace

TEST(JbLibraryServiceTest, ScansHackedAndP30dayFoldersAndIndexesPackageFiles) {
    JbLibraryServiceFixture fixture;

    const auto result = fixture.service.scan();

    ASSERT_EQ(result.entries.size(), 2u);
    EXPECT_EQ(result.libraryFolderCount, 2);
    EXPECT_EQ(result.storageCount, 1);
    EXPECT_TRUE(fixture.service.findFileByName("cusa00001.pkg").has_value());
    EXPECT_TRUE(fixture.service.findFileByName("PPSA00002.FPKG").has_value());
}

TEST(JbLibraryServiceTest, TargetStorageIdRestrictsTheScan) {
    JbLibraryServiceFixture fixture;
    Storage other;
    other.storageId = "drive-2";
    other.name = "Other";
    other.type = StorageType::LocalHdd;
    other.rootPath = pathUtf8(fixture.root / "missing");
    other.isAvailable = true;
    fixture.storage.storages.push_back(std::move(other));

    const auto result = fixture.service.scan("drive-2");

    EXPECT_TRUE(result.entries.empty());
    EXPECT_EQ(result.storageCount, 1);
}
