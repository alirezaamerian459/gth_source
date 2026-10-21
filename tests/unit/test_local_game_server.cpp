#include <gtest/gtest.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "gth/acceleratorserver/WinsockCachingProxyServer.h"
#include "gth/application/devices/IConsoleManager.h"
#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/application/storage/ILocalFileIndex.h"
#include "gth/logging/Logging.h"
#include "gth/network/LocalGameServer.h"

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;
using gth::network::LocalGameServer;

namespace {

class FakeCdnUrlService : public gth::application::ICdnUrlService {
public:
    void captureUrl(const std::string&, const std::string&, bool) override {}
    void updateLocalPath(const std::string& id, const std::string& path) override {
        localPathUpdates.emplace_back(id, path);
    }
    void updateMark(const std::string& id, const std::string& mark) override { markUpdates.emplace_back(id, mark); }
    void updateLixianUrl(const std::string&, const std::string&) override {}
    void enableLixian(const std::string&, bool) override {}
    std::vector<gth::core::CdnUrl> getAll() override { return urls; }
    std::vector<gth::core::CdnUrl> getRecent(int) override { return urls; }
    void removeUrl(const std::string&) override {}

    // A row in the CDN URL Log with a user-mapped path (empty path = captured but unmapped).
    gth::core::CdnUrl& add(const std::string& url, const fs::path& path = {}) {
        gth::core::CdnUrl entry;
        entry.url = url;
        const auto u8 = path.u8string();
        entry.localPath = std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        urls.push_back(entry);
        return urls.back();
    }
    void map(const std::string& url, const fs::path& path) { add(url, path); }

    std::vector<gth::core::CdnUrl> urls;
    std::vector<std::pair<std::string, std::string>> localPathUpdates;
    std::vector<std::pair<std::string, std::string>> markUpdates;
};

class FakeLocalFileIndex : public gth::application::ILocalFileIndex {
public:
    std::optional<fs::path> findByFileName(const std::string& fileName) override {
        lookups.push_back(fileName);
        const auto it = files.find(fileName);
        return it == files.end() ? std::nullopt : std::optional<fs::path>(it->second);
    }
    std::map<std::string, fs::path> files;
    std::vector<std::string> lookups;
};

struct SocketPair {
    SOCKET server = INVALID_SOCKET;
    SOCKET client = INVALID_SOCKET;
};

SocketPair makeSocketPair() {
    WSADATA wsa;
    ::WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listener, 1);
    int len = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);

    SocketPair pair;
    pair.client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ::connect(pair.client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    pair.server = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);
    return pair;
}

struct Served {
    std::optional<gth::network::LocalGameServeResult> result;
    std::string raw;  // everything the "console" received
};

// Runs tryServeGameFile against a real loopback socket while a reader thread plays the console.
Served serve(LocalGameServer& server, const std::string& url, const std::string& headers,
             bool consoleHangsUpFirst = false) {
    auto pair = makeSocketPair();
    Served out;
    std::thread reader;
    if (consoleHangsUpFirst) {
        ::closesocket(pair.client);
    } else {
        reader = std::thread([&] {
            char buf[65536];
            int n;
            while ((n = ::recv(pair.client, buf, sizeof(buf), 0)) > 0) out.raw.append(buf, static_cast<size_t>(n));
        });
    }

    out.result = server.tryServeGameFile(url, headers, reinterpret_cast<void*>(static_cast<intptr_t>(pair.server)));
    ::closesocket(pair.server);
    if (reader.joinable()) {
        reader.join();
        ::closesocket(pair.client);
    }
    return out;
}

std::string bodyOf(const std::string& raw) {
    const auto pos = raw.find("\r\n\r\n");
    return pos == std::string::npos ? "" : raw.substr(pos + 4);
}

class LocalGameServerTest : public ::testing::Test {
protected:
    // GTH_LOG_* dereferences the process-wide logger, which only exists after init().
    static void SetUpTestSuite() {
        gth::logging::init(fs::temp_directory_path() / ("gth_lgs_test_logs_" + std::to_string(::GetCurrentProcessId())));
    }

    void SetUp() override {
        dir = fs::temp_directory_path() / ("gth_lgs_test_" + std::to_string(::GetCurrentProcessId()) + "_" +
                                           std::to_string(counter++));
        fs::create_directories(dir);
        server = std::make_unique<LocalGameServer>(&cdn, &index);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    fs::path write(const fs::path& rel, const std::string& content) {
        const auto p = dir / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
        return p;
    }

    static inline std::atomic<int> counter{0};
    fs::path dir;
    FakeCdnUrlService cdn;
    FakeLocalFileIndex index;
    std::unique_ptr<LocalGameServer> server;
};

const char* kGet = "GET /x HTTP/1.1\r\nHost: cdn.example.com\r\n\r\n";
const char* kPkgUrl = "http://gs2.ww.prod.dl.playstation.net/gs2/ppkgo/prod/CUSA00001_00/1/f_abc/f/UP0001-CUSA00001_00-GAME-A0100-V0100.pkg";

}  // namespace

TEST_F(LocalGameServerTest, ServesAMappedFileInFull) {
    const auto file = write("game.pkg", "0123456789");
    cdn.map("http://cdn.example.com/a/game.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/a/game.pkg", kGet);

    ASSERT_TRUE(s.result.has_value());
    EXPECT_TRUE(s.result->success);
    EXPECT_EQ(s.result->fileSize, 10);
    EXPECT_NE(s.raw.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_EQ(bodyOf(s.raw), "0123456789");
}

TEST_F(LocalGameServerTest, ResponseCarriesTheHeadersConsolesExpect) {
    const auto file = write("game.pkg", "0123456789");
    cdn.map("http://cdn.example.com/a/game.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/a/game.pkg", kGet);

    EXPECT_NE(s.raw.find("Accept-Ranges: bytes\r\n"), std::string::npos);
    EXPECT_NE(s.raw.find("Content-Length: 10\r\n"), std::string::npos);
    EXPECT_NE(s.raw.find("Content-Type: application/octet-stream\r\n"), std::string::npos);
    EXPECT_NE(s.raw.find("Last-Modified: "), std::string::npos);
    EXPECT_NE(s.raw.find("Date: "), std::string::npos);
    EXPECT_NE(s.raw.find("GMT\r\n"), std::string::npos);
}

TEST_F(LocalGameServerTest, AMappedFileIsServedEvenIfItIsNamedDifferently) {
    const auto file = write("renamed_copy.bin", "EXPLICIT");
    cdn.map("http://cdn.example.com/a/game.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/a/game.pkg", kGet);

    ASSERT_TRUE(s.result.has_value());
    EXPECT_EQ(bodyOf(s.raw), "EXPLICIT");
}

TEST_F(LocalGameServerTest, MappedFolderServesTheFileNamedInTheUrl) {
    write("game/other.pkg", "WRONG");
    write("game/sub/wanted.bin", "RIGHT");
    cdn.map("http://cdn.example.com/a/wanted.bin", dir / "game");

    const auto s = serve(*server, "http://cdn.example.com/a/wanted.bin", kGet);

    ASSERT_TRUE(s.result.has_value());
    EXPECT_EQ(bodyOf(s.raw), "RIGHT");
}

TEST_F(LocalGameServerTest, MappedFolderNeverGuessesADifferentFile) {
    write("game/readme.txt", "nope");
    write("game/disc/base.pkg", "SOME-OTHER-PACKAGE");
    cdn.map("http://cdn.example.com/a/unrelated_name.pkg", dir / "game");

    const auto s = serve(*server, "http://cdn.example.com/a/unrelated_name.pkg", kGet);

    // handing a console the wrong package would fail its verification - let it use the network
    EXPECT_FALSE(s.result.has_value());
    EXPECT_TRUE(s.raw.empty());
}

TEST_F(LocalGameServerTest, MappingRequiresTheExactUrl) {
    const auto file = write("wanted.bin", "X");
    cdn.map("http://cdn.example.com/a/wanted.bin", file);

    const auto s = serve(*server, "http://cdn.example.com/a/wanted.bin?token=1", kGet);

    EXPECT_FALSE(s.result.has_value());
}

TEST_F(LocalGameServerTest, HonoursLowercaseRangeHeaders) {
    const auto file = write("game.pkg", "0123456789");
    cdn.map("http://cdn.example.com/game.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/game.pkg",
                         "GET /game.pkg HTTP/1.1\r\nHost: x\r\nrange: bytes=2-5\r\n\r\n");

    ASSERT_TRUE(s.result.has_value());
    EXPECT_NE(s.raw.find("HTTP/1.1 206 Partial Content"), std::string::npos);
    EXPECT_NE(s.raw.find("Content-Range: bytes 2-5/10"), std::string::npos);
    EXPECT_NE(s.raw.find("Content-Length: 4\r\n"), std::string::npos);
    EXPECT_EQ(bodyOf(s.raw), "2345");
}

TEST_F(LocalGameServerTest, HonoursOpenEndedRanges) {
    const auto file = write("game.pkg", "0123456789");
    cdn.map("http://cdn.example.com/game.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/game.pkg",
                         "GET /game.pkg HTTP/1.1\r\nRange: bytes=7-\r\n\r\n");

    ASSERT_TRUE(s.result.has_value());
    EXPECT_NE(s.raw.find("Content-Range: bytes 7-9/10"), std::string::npos);
    EXPECT_EQ(bodyOf(s.raw), "789");
}

TEST_F(LocalGameServerTest, HonoursSuffixRanges) {
    const auto file = write("game.pkg", "0123456789");
    cdn.map("http://cdn.example.com/game.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/game.pkg",
                         "GET /game.pkg HTTP/1.1\r\nRange: bytes=-3\r\n\r\n");

    ASSERT_TRUE(s.result.has_value());
    EXPECT_EQ(bodyOf(s.raw), "789");
}

TEST_F(LocalGameServerTest, MissingMappedPathLeavesTheSocketUntouchedSoTheProxyCanUseTheNetwork) {
    cdn.map("http://cdn.example.com/game.pkg", dir / "does_not_exist");

    const auto s = serve(*server, "http://cdn.example.com/game.pkg", kGet);

    EXPECT_FALSE(s.result.has_value());
    EXPECT_TRUE(s.raw.empty());  // nothing written, so no second response can be corrupted
}

TEST_F(LocalGameServerTest, EmptyMappedFolderLeavesTheSocketUntouched) {
    fs::create_directories(dir / "empty");
    cdn.map("http://cdn.example.com/game.pkg", dir / "empty");

    const auto s = serve(*server, "http://cdn.example.com/game.pkg", kGet);

    EXPECT_FALSE(s.result.has_value());
    EXPECT_TRUE(s.raw.empty());
}

TEST_F(LocalGameServerTest, AMappedPathBeatsTheLibraryIndex) {
    const auto mapped = write("mapped/a.pkg", "MAPPED");
    const auto library = write("library/UP0001-CUSA00001_00-GAME-A0100-V0100.pkg", "LIBRARY");
    index.files["UP0001-CUSA00001_00-GAME-A0100-V0100.pkg"] = library;
    cdn.map(kPkgUrl, mapped);

    const auto s = serve(*server, kPkgUrl, kGet);

    ASSERT_TRUE(s.result.has_value());
    EXPECT_EQ(bodyOf(s.raw), "MAPPED");
}

TEST_F(LocalGameServerTest, FindsTheRequestedPackageByNameInTheLibraryWhenNothingIsMapped) {
    const auto library = write("library/UP0001-CUSA00001_00-GAME-A0100-V0100.pkg", "LIBRARY-PKG");
    index.files["UP0001-CUSA00001_00-GAME-A0100-V0100.pkg"] = library;

    const auto s = serve(*server, kPkgUrl, kGet);

    ASSERT_TRUE(s.result.has_value());
    EXPECT_TRUE(s.result->success);
    EXPECT_EQ(bodyOf(s.raw), "LIBRARY-PKG");
    ASSERT_EQ(index.lookups.size(), 1u);
    EXPECT_EQ(index.lookups[0], "UP0001-CUSA00001_00-GAME-A0100-V0100.pkg");
}

TEST_F(LocalGameServerTest, IgnoresTheQueryStringWhenMatchingByName) {
    const auto library = write("library/game_0.pkg", "PIECE-0");
    index.files["game_0.pkg"] = library;

    const auto s = serve(*server, "http://cdn.example.com/p/game_0.pkg?country=hk&t=1", kGet);

    ASSERT_TRUE(s.result.has_value());
    EXPECT_EQ(bodyOf(s.raw), "PIECE-0");
}

TEST_F(LocalGameServerTest, OnlyPackageNamesAreMatchedAutomatically) {
    const auto library = write("library/manifest.json", "{}");
    index.files["manifest.json"] = library;

    const auto s = serve(*server, "http://cdn.example.com/p/manifest.json", kGet);

    EXPECT_FALSE(s.result.has_value());
    EXPECT_TRUE(index.lookups.empty());  // not even looked up
}

TEST_F(LocalGameServerTest, ShowsAnAutomaticMatchInTheCdnUrlLog) {
    const auto library = write("library/game_0.pkg", "PIECE-0");
    index.files["game_0.pkg"] = library;
    const std::string url = "http://cdn.example.com/p/game_0.pkg";
    auto& row = cdn.add(url);  // captured by the proxy, no path yet
    const std::string rowId = row.cdnUrlId;

    const auto s = serve(*server, url, kGet);

    ASSERT_TRUE(s.result.has_value());
    ASSERT_EQ(cdn.localPathUpdates.size(), 1u);
    EXPECT_EQ(cdn.localPathUpdates[0].first, rowId);
    EXPECT_NE(cdn.localPathUpdates[0].second.find("game_0.pkg"), std::string::npos);
    ASSERT_EQ(cdn.markUpdates.size(), 1u);
    EXPECT_EQ(cdn.markUpdates[0].second, "Local: game_0.pkg");
}

TEST_F(LocalGameServerTest, DoesNotOverwriteAMarkTheUserAlreadyWrote) {
    const auto library = write("library/game_0.pkg", "PIECE-0");
    index.files["game_0.pkg"] = library;
    const std::string url = "http://cdn.example.com/p/game_0.pkg";
    cdn.add(url).markText = "my note";

    serve(*server, url, kGet);

    EXPECT_TRUE(cdn.markUpdates.empty());
}

TEST_F(LocalGameServerTest, ReturnsNothingWhenThereIsNoMappingAndNoLibraryMatch) {
    const auto s = serve(*server, kPkgUrl, kGet);

    EXPECT_FALSE(s.result.has_value());
    EXPECT_TRUE(s.raw.empty());
}

TEST_F(LocalGameServerTest, AnInterruptedTransferStillCountsAsHandled) {
    const auto file = write("big.pkg", std::string(16 * 1024 * 1024, 'x'));
    cdn.map("http://cdn.example.com/big.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/big.pkg", kGet, /*consoleHangsUpFirst=*/true);

    // The response had already started, so the proxy must not append an origin response to it.
    ASSERT_TRUE(s.result.has_value());
    EXPECT_FALSE(s.result->success);
}

TEST_F(LocalGameServerTest, ServesFilesUnderNonAsciiFolderNames) {
    const auto file = write(fs::path(L"jääمرحبا") / "game.pkg", "UNICODE");
    cdn.map("http://cdn.example.com/game.pkg", file);

    const auto s = serve(*server, "http://cdn.example.com/game.pkg", kGet);

    ASSERT_TRUE(s.result.has_value());
    EXPECT_EQ(bodyOf(s.raw), "UNICODE");
}

TEST_F(LocalGameServerTest, CanServeLocallyReflectsWhatWouldBeServed) {
    const auto library = write("library/game_0.pkg", "PIECE-0");
    index.files["game_0.pkg"] = library;

    EXPECT_TRUE(server->canServeLocally("http://cdn.example.com/p/game_0.pkg"));
    EXPECT_FALSE(server->canServeLocally("http://cdn.example.com/p/other_0.pkg"));
}

// ---------------------------------------------------------------------------------------------
// End to end: a real WinsockCachingProxyServer + real LocalGameServer, a fake CDN log, and a tiny
// local "origin" HTTP server standing in for the internet.

namespace {

class FakeConsoleManager : public gth::application::IConsoleManager {
public:
    std::vector<gth::core::Console> getAllConsoles() override { return {}; }
    gth::core::Console addConsole(const std::string&, const std::string&, const std::string&, int,
                                   gth::core::ConsoleProtocol) override {
        return {};
    }
    void removeConsole(const std::string&) override {}
    void recordConnection(const std::string&, const std::string&, int, gth::core::ConsoleProtocol) override {}
    gth::core::ConsoleStatus checkReachability(const std::string&) override { return {}; }
};

int freePort() {
    auto pair = makeSocketPair();
    sockaddr_in addr{};
    int len = sizeof(addr);
    ::getsockname(pair.server, reinterpret_cast<sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);  // server-side accepted socket shares the listener's port
    ::closesocket(pair.server);
    ::closesocket(pair.client);
    return port;
}

class FakeOrigin {
public:
    FakeOrigin() {
        WSADATA wsa;
        ::WSAStartup(MAKEWORD(2, 2), &wsa);
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listener_, 8);
        int len = sizeof(addr);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        thread_ = std::thread([this] {
            while (true) {
                SOCKET c = ::accept(listener_, nullptr, nullptr);
                if (c == INVALID_SOCKET) return;
                char buf[4096];
                std::string req;
                while (req.find("\r\n\r\n") == std::string::npos) {
                    const int n = ::recv(c, buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    req.append(buf, static_cast<size_t>(n));
                }
                hits++;
                const std::string resp =
                    "HTTP/1.1 200 OK\r\nContent-Length: 6\r\nConnection: close\r\n\r\nORIGIN";
                ::send(c, resp.data(), static_cast<int>(resp.size()), 0);
                ::closesocket(c);
            }
        });
    }
    ~FakeOrigin() {
        ::closesocket(listener_);
        thread_.join();
    }
    int port = 0;
    std::atomic<int> hits{0};

private:
    SOCKET listener_ = INVALID_SOCKET;
    std::thread thread_;
};

std::string httpGetViaProxy(int proxyPort, const std::string& absoluteUrl, const std::string& extraHeaders = "") {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(proxyPort));
    for (int i = 0; i < 50 && ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0; ++i) {
        ::closesocket(s);
        ::Sleep(50);
        s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    const std::string req =
        "GET " + absoluteUrl + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + extraHeaders + "Connection: close\r\n\r\n";
    ::send(s, req.data(), static_cast<int>(req.size()), 0);
    std::string raw;
    char buf[65536];
    int n;
    while ((n = ::recv(s, buf, sizeof(buf), 0)) > 0) raw.append(buf, static_cast<size_t>(n));
    ::closesocket(s);
    return raw;
}

size_t count(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

}  // namespace

class ProxyEndToEndTest : public LocalGameServerTest {
protected:
    void startProxy() {
        proxy = std::make_unique<gth::acceleratorserver::WinsockCachingProxyServer>(
            [](const std::string&) { return std::optional<std::string>{}; }, consoles, cdn,
            std::make_shared<LocalGameServer>(&cdn, &index));
        gth::core::AcceleratorServerConfig cfg;
        cfg.port = freePort();
        cfg.cacheEnabled = false;
        proxyPort = cfg.port;
        proxy->start(cfg);
    }
    void TearDown() override {
        if (proxy) proxy->stop();
        LocalGameServerTest::TearDown();
    }

    FakeConsoleManager consoles;
    std::unique_ptr<gth::acceleratorserver::WinsockCachingProxyServer> proxy;
    int proxyPort = 0;
};

TEST_F(ProxyEndToEndTest, AMappedUrlIsServedFromDiskAndNeverReachesTheOrigin) {
    FakeOrigin origin;
    const auto file = write("game.pkg", "FROM-THE-HDD");
    const std::string url = "http://127.0.0.1:" + std::to_string(origin.port) + "/dl/game.pkg";
    cdn.map(url, file);
    startProxy();

    const auto raw = httpGetViaProxy(proxyPort, url);

    EXPECT_EQ(bodyOf(raw), "FROM-THE-HDD");
    EXPECT_EQ(origin.hits.load(), 0);
}

TEST_F(ProxyEndToEndTest, ADownloadIsRecognisedByItsFileNameAndServedFromTheLibrary) {
    FakeOrigin origin;
    const auto library = write("library/UP0001-CUSA00001_00-GAME-A0100-V0100.pkg", "0123456789");
    index.files["UP0001-CUSA00001_00-GAME-A0100-V0100.pkg"] = library;
    const std::string url = "http://127.0.0.1:" + std::to_string(origin.port) +
                            "/gs2/ppkgo/prod/CUSA00001_00/1/f_abc/f/UP0001-CUSA00001_00-GAME-A0100-V0100.pkg?country=us";
    startProxy();

    const auto whole = httpGetViaProxy(proxyPort, url);
    const auto part = httpGetViaProxy(proxyPort, url, "Range: bytes=3-6\r\n");

    EXPECT_EQ(bodyOf(whole), "0123456789");
    EXPECT_NE(part.find("206 Partial Content"), std::string::npos);
    EXPECT_EQ(bodyOf(part), "3456");
    EXPECT_EQ(origin.hits.load(), 0);  // the internet was never touched
}

TEST_F(ProxyEndToEndTest, ABrokenMappingFallsBackToTheOriginWithASingleCleanResponse) {
    FakeOrigin origin;
    const std::string url = "http://127.0.0.1:" + std::to_string(origin.port) + "/dl/game.pkg";
    cdn.map(url, dir / "gone");
    startProxy();

    const auto raw = httpGetViaProxy(proxyPort, url);

    EXPECT_EQ(count(raw, "HTTP/1.1"), 1u) << raw;
    EXPECT_EQ(bodyOf(raw), "ORIGIN");
    EXPECT_EQ(origin.hits.load(), 1);
}

TEST_F(ProxyEndToEndTest, AnUnknownPackageStillGoesToTheOrigin) {
    FakeOrigin origin;
    const std::string url = "http://127.0.0.1:" + std::to_string(origin.port) + "/dl/other_0.pkg";
    startProxy();

    const auto raw = httpGetViaProxy(proxyPort, url);

    EXPECT_EQ(bodyOf(raw), "ORIGIN");
    EXPECT_EQ(origin.hits.load(), 1);
}
