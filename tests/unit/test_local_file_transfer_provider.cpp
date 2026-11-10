#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gth/core/Guid.h"
#include "gth/core/enums/TransferStatus.h"
#include "gth/transfer/LocalFileTransferProvider.h"

using namespace gth::core;
using namespace gth::transfer;
namespace fs = std::filesystem;

namespace {

class LocalFileTransferProviderTest : public ::testing::Test {
protected:
    fs::path dir;

    void SetUp() override {
        dir = gth::test::testTempRoot() / ("gth_provider_test_" + Guid::newGuid());
        fs::create_directories(dir);
    }

    void TearDown() override { fs::remove_all(dir); }
};

}  // namespace

TEST_F(LocalFileTransferProviderTest, CopiesFileContentExactly) {
    const auto src = dir / "source.bin";
    const auto dst = dir / "nested" / "destination.bin";
    std::ofstream(src, std::ios::binary) << "exact content";

    LocalFileTransferProvider provider;
    auto outcome = provider.copyFile(src.string(), dst.string(), nullptr, nullptr);

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    ASSERT_TRUE(fs::exists(dst));

    std::ifstream in(dst, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "exact content");
}

TEST_F(LocalFileTransferProviderTest, CreatesDestinationDirectoryIfMissing) {
    const auto src = dir / "source.bin";
    const auto dst = dir / "a" / "b" / "c" / "destination.bin";
    std::ofstream(src, std::ios::binary) << "x";

    LocalFileTransferProvider provider;
    auto outcome = provider.copyFile(src.string(), dst.string(), nullptr, nullptr);

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    EXPECT_TRUE(fs::exists(dst));
}

TEST_F(LocalFileTransferProviderTest, FailsWithClearErrorWhenSourceDoesNotExist) {
    LocalFileTransferProvider provider;
    auto outcome = provider.copyFile((dir / "does-not-exist.bin").string(), (dir / "dst.bin").string(), nullptr,
                                      nullptr);

    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.errorMessage.empty());
}

TEST_F(LocalFileTransferProviderTest, InvokesProgressCallback) {
    const auto src = dir / "source.bin";
    const auto dst = dir / "destination.bin";
    std::ofstream(src, std::ios::binary) << std::string(1024, 'x');

    LocalFileTransferProvider provider;
    bool called = false;
    auto outcome = provider.copyFile(src.string(), dst.string(),
                                      [&](long long transferred, long long total) {
                                          called = true;
                                          EXPECT_GE(total, transferred);
                                      },
                                      nullptr);

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    EXPECT_TRUE(called);
}

TEST(TransferStatusTest, ToStringRoundTrips) {
    EXPECT_EQ(transferStatusFromString(toString(TransferStatus::Queued)), TransferStatus::Queued);
    EXPECT_EQ(transferStatusFromString(toString(TransferStatus::Transferring)), TransferStatus::Transferring);
    EXPECT_EQ(transferStatusFromString(toString(TransferStatus::Completed)), TransferStatus::Completed);
    EXPECT_EQ(transferStatusFromString(toString(TransferStatus::Failed)), TransferStatus::Failed);
    EXPECT_EQ(transferStatusFromString(toString(TransferStatus::Cancelled)), TransferStatus::Cancelled);
}
