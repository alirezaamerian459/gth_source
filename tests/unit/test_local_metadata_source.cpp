#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gth/core/Guid.h"
#include "gth/updates/LocalMetadataSource.h"

using namespace gth::core;
using namespace gth::updates;
namespace fs = std::filesystem;

namespace {

class LocalMetadataSourceTest : public ::testing::Test {
protected:
    fs::path manifestPath;

    void SetUp() override {
        manifestPath = gth::test::testTempRoot() / ("gth_manifest_" + Guid::newGuid() + ".txt");
    }

    void TearDown() override { fs::remove(manifestPath); }

    void writeManifest(const std::string& content) {
        std::ofstream out(manifestPath);
        out << content;
    }

    GameVariant makeVariant(const std::string& relativePath) {
        GameVariant v;
        v.relativePath = relativePath;
        return v;
    }
};

}  // namespace

TEST_F(LocalMetadataSourceTest, ReportsUpdateAvailableWhenManifestHasNewerVersion) {
    writeManifest("PS5\\Spider-Man 2\\Europe\t1.06\t18400000000\n");

    LocalMetadataSource source(manifestPath.string());
    auto result = source.checkForUpdate(makeVariant("PS5\\Spider-Man 2\\Europe"), "1.04");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->updateAvailable);
    EXPECT_EQ(result->latestVersion, "1.06");
    EXPECT_EQ(result->updateSizeBytes, 18400000000);
}

TEST_F(LocalMetadataSourceTest, ReportsNoUpdateWhenCurrentIsAlreadyLatest) {
    writeManifest("PS5\\Spider-Man 2\\Europe\t1.06\t0\n");

    LocalMetadataSource source(manifestPath.string());
    auto result = source.checkForUpdate(makeVariant("PS5\\Spider-Man 2\\Europe"), "1.06");

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->updateAvailable);
}

TEST_F(LocalMetadataSourceTest, ReturnsNulloptForUnknownVariant) {
    writeManifest("PS5\\Spider-Man 2\\Europe\t1.06\t0\n");

    LocalMetadataSource source(manifestPath.string());
    auto result = source.checkForUpdate(makeVariant("PS5\\Some Other Game\\USA"), "1.00");

    EXPECT_FALSE(result.has_value());
}

TEST_F(LocalMetadataSourceTest, IgnoresCommentsAndBlankLines) {
    writeManifest(
        "# this is a comment\n"
        "\n"
        "PS5\\Spider-Man 2\\Europe\t1.06\t0\n");

    LocalMetadataSource source(manifestPath.string());
    auto result = source.checkForUpdate(makeVariant("PS5\\Spider-Man 2\\Europe"), "1.04");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->updateAvailable);
}

TEST_F(LocalMetadataSourceTest, MissingManifestFileIsNotAnErrorJustNoInformation) {
    LocalMetadataSource source((gth::test::testTempRoot() / "does-not-exist.txt").string());
    auto result = source.checkForUpdate(makeVariant("PS5\\Spider-Man 2\\Europe"), "1.04");

    EXPECT_FALSE(result.has_value());
}

TEST_F(LocalMetadataSourceTest, DownloadUpdateAlwaysFailsSinceThisSourceHasNoFiles) {
    LocalMetadataSource source(manifestPath.string());
    auto result = source.downloadUpdate(UpdateSourceInfo{}, "C:\\dest", nullptr);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}
