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
    fs::path tempDir = fs::temp_directory_path() /
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
