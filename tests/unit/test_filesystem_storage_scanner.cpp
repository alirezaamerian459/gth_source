#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gth/core/Guid.h"
#include "gth/indexing/FilesystemStorageScanner.h"

using namespace gth::core;
using namespace gth::indexing;
namespace fs = std::filesystem;

namespace {

class FilesystemStorageScannerTest : public ::testing::Test {
protected:
    fs::path root;

    void SetUp() override {
        root = fs::temp_directory_path() / ("gth_scanner_test_" + gth::core::Guid::newGuid());

        writeFile(root / "PS5" / "Spider-Man 2" / "Europe" / "1.04" / "data.bin", "v1.04");
        writeFile(root / "PS5" / "Spider-Man 2" / "Europe" / "1.06" / "data.bin", "v1.06");
        writeFile(root / "PS5" / "Spider-Man 2" / "Europe" / "1.06" / "dlc.bin", "dlc content");
        writeFile(root / "PS5" / "Spider-Man 2" / "USA" / "1.05" / "data.bin", "usa data");
        writeFile(root / "Switch" / "Zelda" / "Europe" / "1.00" / "data.bin", "zelda data");

        // A folder that's too shallow to match the convention - must be skipped, not crash the scan.
        fs::create_directories(root / "PS5" / "StrayFolder");
        writeFile(root / "PS5" / "StrayFolder" / "readme.txt", "not a variant");
    }

    void TearDown() override { fs::remove_all(root); }

    static void writeFile(const fs::path& path, const std::string& content) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }
};

}  // namespace

TEST_F(FilesystemStorageScannerTest, FindsAllConventionCompliantVariantsAndGroupsVersions) {
    FilesystemStorageScanner scanner;
    auto results = scanner.scan(root.string());

    // Three distinct (Platform, Title, Region) containers: Spider-Man 2/Europe
    // (2 version folders, grouped into one entry), Spider-Man 2/USA (a
    // different Region, so a separate container), and Zelda/Europe.
    ASSERT_EQ(results.size(), 3u);

    const ScannedVariant* spiderManEurope = nullptr;
    const ScannedVariant* spiderManUsa = nullptr;
    const ScannedVariant* zelda = nullptr;
    for (const auto& v : results) {
        if (v.title == "Spider-Man 2" && v.region == "Europe") spiderManEurope = &v;
        if (v.title == "Spider-Man 2" && v.region == "USA") spiderManUsa = &v;
        if (v.title == "Zelda") zelda = &v;
    }

    ASSERT_NE(spiderManEurope, nullptr);
    EXPECT_EQ(spiderManEurope->platform, "PS5");
    ASSERT_EQ(spiderManEurope->versions.size(), 2u);  // 1.04 and 1.06 grouped together

    ASSERT_NE(spiderManUsa, nullptr);
    ASSERT_EQ(spiderManUsa->versions.size(), 1u);

    ASSERT_NE(zelda, nullptr);
    EXPECT_EQ(zelda->platform, "Switch");
    ASSERT_EQ(zelda->versions.size(), 1u);
}

TEST_F(FilesystemStorageScannerTest, ReportsCorrectFilesPerVersion) {
    FilesystemStorageScanner scanner;
    auto results = scanner.scan(root.string());

    const ScannedVersion* version106 = nullptr;
    for (const auto& variant : results) {
        if (variant.title != "Spider-Man 2" || variant.region != "Europe") continue;
        for (const auto& version : variant.versions) {
            if (version.version == "1.06") version106 = &version;
        }
    }

    ASSERT_NE(version106, nullptr);
    ASSERT_EQ(version106->files.size(), 2u);

    bool foundData = false, foundDlc = false;
    for (const auto& f : version106->files) {
        if (f.relativePath == "data.bin") {
            foundData = true;
            EXPECT_GT(f.sizeBytes, 0);
            EXPECT_FALSE(f.lastModified.empty());
        }
        if (f.relativePath == "dlc.bin") foundDlc = true;
    }
    EXPECT_TRUE(foundData);
    EXPECT_TRUE(foundDlc);
}

TEST_F(FilesystemStorageScannerTest, ReturnsEmptyForNonexistentRoot) {
    FilesystemStorageScanner scanner;
    auto results = scanner.scan((root / "does-not-exist").string());
    EXPECT_TRUE(results.empty());
}
