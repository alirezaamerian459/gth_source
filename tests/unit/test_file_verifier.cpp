#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "gth/core/Guid.h"
#include "gth/verification/FileVerifier.h"
#include "gth/verification/Sha256.h"

using namespace gth::core;
using namespace gth::verification;
namespace fs = std::filesystem;

namespace {

class FileVerifierTest : public ::testing::Test {
protected:
    fs::path dir;

    void SetUp() override {
        dir = fs::temp_directory_path() / ("gth_verify_test_" + Guid::newGuid());
        fs::create_directories(dir);
    }

    void TearDown() override { fs::remove_all(dir); }

    fs::path writeFile(const std::string& name, const std::string& content) {
        const auto path = dir / name;
        std::ofstream(path, std::ios::binary) << content;
        return path;
    }
};

}  // namespace

// Known SHA-256 test vector: sha256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
TEST_F(FileVerifierTest, Sha256OfEmptyFileMatchesKnownVector) {
    const auto path = writeFile("empty.bin", "");
    auto hash = computeSha256Hex(path.string());

    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(*hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// Known SHA-256 test vector: sha256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
TEST_F(FileVerifierTest, Sha256OfKnownContentMatchesKnownVector) {
    const auto path = writeFile("abc.bin", "abc");
    auto hash = computeSha256Hex(path.string());

    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(*hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_F(FileVerifierTest, Sha256ReturnsNulloptForMissingFile) {
    auto hash = computeSha256Hex((dir / "does-not-exist.bin").string());
    EXPECT_FALSE(hash.has_value());
}

TEST_F(FileVerifierTest, FailsWhenPathDoesNotExist) {
    FileVerifier verifier;
    auto result = verifier.verify((dir / "missing.bin").string(), 0, std::nullopt);

    EXPECT_FALSE(result.passed);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(FileVerifierTest, PassesWhenSizeMatchesAndNoHashExpected) {
    const auto path = writeFile("data.bin", "hello world");
    FileVerifier verifier;
    auto result = verifier.verify(path.string(), 11, std::nullopt);

    EXPECT_TRUE(result.passed);
    EXPECT_FALSE(result.hashVerified);
}

TEST_F(FileVerifierTest, FailsWhenSizeDoesNotMatch) {
    const auto path = writeFile("data.bin", "hello world");
    FileVerifier verifier;
    auto result = verifier.verify(path.string(), 999, std::nullopt);

    EXPECT_FALSE(result.passed);
    EXPECT_NE(result.errorMessage.find("Size mismatch"), std::string::npos);
}

TEST_F(FileVerifierTest, SkipsSizeCheckWhenExpectedSizeIsUnknown) {
    const auto path = writeFile("data.bin", "hello world");
    FileVerifier verifier;
    auto result = verifier.verify(path.string(), 0, std::nullopt);

    EXPECT_TRUE(result.passed);
}

TEST_F(FileVerifierTest, PassesAndReportsHashVerifiedWhenHashMatches) {
    const auto path = writeFile("abc.bin", "abc");
    FileVerifier verifier;
    auto result =
        verifier.verify(path.string(), 3, std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    EXPECT_TRUE(result.passed);
    EXPECT_TRUE(result.hashVerified);
}

TEST_F(FileVerifierTest, FailsWhenHashDoesNotMatch) {
    const auto path = writeFile("abc.bin", "abc");
    FileVerifier verifier;
    auto result = verifier.verify(path.string(), 3, std::string(64, '0'));

    EXPECT_FALSE(result.passed);
    EXPECT_NE(result.errorMessage.find("Hash mismatch"), std::string::npos);
}

TEST_F(FileVerifierTest, DirectoryVerificationChecksSizeButSkipsHash) {
    writeFile("a.bin", "hello");
    writeFile("b.bin", "world!");  // 5 + 6 = 11 bytes total

    FileVerifier verifier;
    auto result = verifier.verify(dir.string(), 11, std::string(64, 'f'));

    EXPECT_TRUE(result.passed);       // size checked and matched
    EXPECT_FALSE(result.hashVerified);  // hash not attempted for a directory
}

TEST_F(FileVerifierTest, DirectoryVerificationFailsOnSizeMismatch) {
    writeFile("a.bin", "hello");

    FileVerifier verifier;
    auto result = verifier.verify(dir.string(), 999, std::nullopt);

    EXPECT_FALSE(result.passed);
}
