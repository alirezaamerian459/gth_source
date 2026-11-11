#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "gth/ftp/CurlFtpClient.h"
#include "gth/ftpserver/WinsockFtpServer.h"

using namespace gth::core;
using namespace gth::ftp;
using namespace gth::ftpserver;

namespace {

namespace fs = std::filesystem;

// Waits (with a hard timeout so a bug can't hang the test suite forever)
// until predicate() is true - same helper pattern as test_enterprise.cpp's
// CentralServerListener tests, since this exercises a real background
// accept-loop thread too.
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

struct WinsockFtpServerFixture {
    fs::path tempDir = gth::test::testTempRoot() /
                        ("gth_ftpserver_test_" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    WinsockFtpServer server;

    WinsockFtpServerFixture() { fs::create_directories(tempDir); }

    ~WinsockFtpServerFixture() {
        server.stop();
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }

    static FtpConnectParams connectParams(int port) {
        FtpConnectParams params;
        params.host = "127.0.0.1";
        params.port = port;
        params.username.clear();
        params.password.clear();
        params.remoteBasePath = "/";
        return params;
    }
};

}  // namespace

TEST(WinsockFtpServerTest, StartsAndBindsConfiguredPort) {
    WinsockFtpServerFixture fixture;
    FtpServerConfig config;
    config.rootPath = fixture.tempDir.string();
    config.port = 52021;

    fixture.server.start(config);
    // isRunning() flips true synchronously in start(), before the background
    // accept-loop thread has actually bound/listened on the socket - wait
    // for port() too (set only after a successful bind) so the test doesn't
    // race ahead of the real listener.
    ASSERT_TRUE(waitFor([&] { return fixture.server.isRunning() && fixture.server.port() != 0; }));
    EXPECT_EQ(fixture.server.port(), 52021);

    fixture.server.stop();
    EXPECT_FALSE(fixture.server.isRunning());
}

TEST(WinsockFtpServerTest, ListReflectsFilesInRoot) {
    WinsockFtpServerFixture fixture;
    {
        std::ofstream file(fixture.tempDir / "hello.txt", std::ios::binary);
        file << "hello world";
    }

    FtpServerConfig config;
    config.rootPath = fixture.tempDir.string();
    config.port = 52022;
    fixture.server.start(config);
    // isRunning() flips true synchronously in start(), before the background
    // accept-loop thread has actually bound/listened on the socket - wait
    // for port() too (set only after a successful bind) so the test doesn't
    // race ahead of the real listener.
    ASSERT_TRUE(waitFor([&] { return fixture.server.isRunning() && fixture.server.port() != 0; }));

    CurlFtpClient client(false);
    const auto connectResult = client.connect(WinsockFtpServerFixture::connectParams(52022));
    ASSERT_TRUE(connectResult.success) << connectResult.errorMessage;

    const auto entries = client.listDirectory("/");
    bool found = false;
    for (const auto& entry : entries) {
        if (entry.name == "hello.txt") found = true;
    }
    EXPECT_TRUE(found);
}

TEST(WinsockFtpServerTest, UploadThenDownloadRoundTripsContent) {
    WinsockFtpServerFixture fixture;

    FtpServerConfig config;
    config.rootPath = fixture.tempDir.string();
    config.port = 52023;
    fixture.server.start(config);
    // isRunning() flips true synchronously in start(), before the background
    // accept-loop thread has actually bound/listened on the socket - wait
    // for port() too (set only after a successful bind) so the test doesn't
    // race ahead of the real listener.
    ASSERT_TRUE(waitFor([&] { return fixture.server.isRunning() && fixture.server.port() != 0; }));

    const fs::path localUploadSource = fixture.tempDir.parent_path() / "gth_ftpserver_upload_source.txt";
    {
        std::ofstream out(localUploadSource, std::ios::binary);
        out << "round trip content";
    }

    CurlFtpClient uploader(false);
    ASSERT_TRUE(uploader.connect(WinsockFtpServerFixture::connectParams(52023)).success);
    const auto uploadResult = uploader.upload(localUploadSource.string(), "/uploaded.txt", nullptr, nullptr);
    ASSERT_TRUE(uploadResult.success) << uploadResult.errorMessage;
    ASSERT_TRUE(fs::exists(fixture.tempDir / "uploaded.txt"));

    const fs::path localDownloadTarget = fixture.tempDir.parent_path() / "gth_ftpserver_download_target.txt";
    CurlFtpClient downloader(false);
    ASSERT_TRUE(downloader.connect(WinsockFtpServerFixture::connectParams(52023)).success);
    const auto downloadResult = downloader.download("/uploaded.txt", localDownloadTarget.string(), nullptr, nullptr);
    ASSERT_TRUE(downloadResult.success) << downloadResult.errorMessage;

    std::ifstream in(localDownloadTarget, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "round trip content");

    std::error_code ec;
    fs::remove(localUploadSource, ec);
    fs::remove(localDownloadTarget, ec);
}

// Proves the security-critical guarantee: a client asking for something
// outside the shared root - here via ".." traversal - never gets served
// content that lives outside that root, even though the file genuinely
// exists on disk one level up from the shared folder.
TEST(WinsockFtpServerTest, PathEscapeAttemptIsRejectedNotServed) {
    WinsockFtpServerFixture fixture;

    const fs::path secretFile = fixture.tempDir.parent_path() / "gth_ftpserver_secret.txt";
    {
        std::ofstream out(secretFile, std::ios::binary);
        out << "top secret, must never be served";
    }

    FtpServerConfig config;
    config.rootPath = fixture.tempDir.string();
    config.port = 52024;
    fixture.server.start(config);
    // isRunning() flips true synchronously in start(), before the background
    // accept-loop thread has actually bound/listened on the socket - wait
    // for port() too (set only after a successful bind) so the test doesn't
    // race ahead of the real listener.
    ASSERT_TRUE(waitFor([&] { return fixture.server.isRunning() && fixture.server.port() != 0; }));

    CurlFtpClient client(false);
    ASSERT_TRUE(client.connect(WinsockFtpServerFixture::connectParams(52024)).success);

    const fs::path downloadTarget = fixture.tempDir.parent_path() / "gth_ftpserver_escape_attempt.txt";
    std::error_code ec;
    fs::remove(downloadTarget, ec);

    const auto result = client.download("../gth_ftpserver_secret.txt", downloadTarget.string(), nullptr, nullptr);

    EXPECT_FALSE(result.success);
    // CurlFtpClient::download opens (and truncates) the local destination
    // file before it knows whether the transfer will succeed, so the file
    // may exist - what matters is that it never received the secret bytes.
    if (fs::exists(downloadTarget)) {
        std::ifstream in(downloadTarget, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_TRUE(content.empty());
    }

    fs::remove(downloadTarget, ec);
    fs::remove(secretFile, ec);
}

TEST(WinsockFtpServerTest, VirtualJbLibraryIsBrowsableReadableAndReadOnly) {
    WinsockFtpServerFixture fixture;
    const fs::path jbGame = fixture.tempDir.parent_path() / "gth_ftpserver_jb_game";
    fs::create_directories(jbGame);
    {
        std::ofstream out(jbGame / "CUSA00001.pkg", std::ios::binary);
        out << "jb package content";
    }

    FtpServerConfig config;
    config.rootPath = fixture.tempDir.string();
    config.port = 52025;
    config.virtualMappings.push_back({"JB Library/Test Game", jbGame.string(), true});
    fixture.server.start(config);
    ASSERT_TRUE(waitFor([&] { return fixture.server.isRunning() && fixture.server.port() != 0; }));

    CurlFtpClient client(false);
    ASSERT_TRUE(client.connect(WinsockFtpServerFixture::connectParams(52025)).success);

    const auto rootEntries = client.listDirectory("/");
    ASSERT_EQ(rootEntries.size(), 1u);
    EXPECT_EQ(rootEntries[0].name, "JB Library");
    EXPECT_TRUE(rootEntries[0].isDirectory);

    const auto libraryEntries = client.listDirectory("/JB Library");
    ASSERT_EQ(libraryEntries.size(), 1u);
    EXPECT_EQ(libraryEntries[0].name, "Test Game");
    EXPECT_TRUE(libraryEntries[0].isDirectory);

    const auto gameEntries = client.listDirectory("/JB Library/Test Game");
    ASSERT_EQ(gameEntries.size(), 1u);
    EXPECT_EQ(gameEntries[0].name, "CUSA00001.pkg");
    EXPECT_FALSE(gameEntries[0].isDirectory);

    const fs::path localDownload = fixture.tempDir.parent_path() / "gth_ftpserver_jb_download.pkg";
    const auto downloadResult = client.download("/JB Library/Test Game/CUSA00001.pkg", localDownload.string(), nullptr,
                                                nullptr);
    ASSERT_TRUE(downloadResult.success) << downloadResult.errorMessage;
    std::ifstream downloaded(localDownload, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(downloaded)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "jb package content");

    const fs::path uploadSource = fixture.tempDir.parent_path() / "gth_ftpserver_jb_upload.txt";
    {
        std::ofstream out(uploadSource, std::ios::binary);
        out << "must not be written into JB library";
    }
    const auto uploadResult = client.upload(uploadSource.string(), "/JB Library/Test Game/blocked.txt", nullptr, nullptr);
    EXPECT_FALSE(uploadResult.success);
    EXPECT_FALSE(fs::exists(jbGame / "blocked.txt"));

    std::error_code ec;
    fs::remove(localDownload, ec);
    fs::remove(uploadSource, ec);
    fs::remove_all(jbGame, ec);
}
