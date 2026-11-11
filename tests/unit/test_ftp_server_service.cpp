#include <gtest/gtest.h>

#include "gth/application/ftp/FtpServerService.h"

using namespace gth::core;
using namespace gth::application;

namespace {

class FakeIFtpServer : public IFtpServer {
public:
    bool started = false;
    FtpServerConfig lastConfig;

    void start(const FtpServerConfig& config) override {
        started = true;
        lastConfig = config;
    }
    void stop() override { started = false; }
    bool isRunning() const override { return started; }
    int port() const override { return started ? lastConfig.port : 0; }
};

class FakeNetworkInterfaceDetector : public INetworkInterfaceDetector {
public:
    std::vector<NetworkInterfaceInfo> interfaces;
    std::vector<NetworkInterfaceInfo> detect() override { return interfaces; }
};

class FakeJbLibraryService : public IJbLibraryService {
public:
    JbLibraryScanResult result;

    JbLibraryScanResult scan(const std::string& = {}) override { return result; }
    JbLibraryScanResult snapshot() const override { return result; }
    std::optional<std::filesystem::path> findFileByName(const std::string&) override { return std::nullopt; }
};

NetworkInterfaceInfo makeInterface(const std::string& ipv4, NetworkInterfaceStatus status) {
    NetworkInterfaceInfo info;
    info.name = "Test Adapter";
    info.ipv4 = ipv4;
    info.status = status;
    return info;
}

struct FtpServerServiceFixture {
    FakeIFtpServer server;
    FakeNetworkInterfaceDetector detector;
    FtpServerService service{server, detector};
};

}  // namespace

TEST(FtpServerServiceTest, StartDelegatesToServerWithGivenRootAndPort) {
    FtpServerServiceFixture fixture;

    fixture.service.start("C:/shared", 2121);

    EXPECT_TRUE(fixture.server.started);
    EXPECT_EQ(fixture.server.lastConfig.rootPath, "C:/shared");
    EXPECT_EQ(fixture.server.lastConfig.port, 2121);
    EXPECT_TRUE(fixture.service.isRunning());
    EXPECT_EQ(fixture.service.port(), 2121);
}

TEST(FtpServerServiceTest, StopDelegatesToServer) {
    FtpServerServiceFixture fixture;
    fixture.service.start("C:/shared", 2121);

    fixture.service.stop();

    EXPECT_FALSE(fixture.server.started);
    EXPECT_FALSE(fixture.service.isRunning());
}

TEST(FtpServerServiceTest, StartWithJbLibraryAddsReadOnlyVirtualMappings) {
    FakeIFtpServer server;
    FakeNetworkInterfaceDetector detector;
    FakeJbLibraryService jbLibrary;
    const auto gamePath = std::filesystem::temp_directory_path() / "gth_ftp_service_jb_game";
    std::error_code ec;
    std::filesystem::create_directories(gamePath, ec);
    ASSERT_FALSE(ec);
    jbLibrary.result.entries.push_back(
        {"Test Game", "CUSA00001", "PS4", gamePath, "disk-1", "Games", "C:/"});
    FtpServerService service(server, detector, &jbLibrary);

    service.start("C:/shared", 2121, true);

    ASSERT_EQ(server.lastConfig.virtualMappings.size(), 1u);
    EXPECT_EQ(server.lastConfig.virtualMappings[0].virtualPath, "JB Library/Test Game");
    EXPECT_EQ(server.lastConfig.virtualMappings[0].physicalPath, gamePath.string());
    EXPECT_TRUE(server.lastConfig.virtualMappings[0].readOnly);

    std::filesystem::remove_all(gamePath, ec);
}

TEST(FtpServerServiceTest, IsRunningIsFalseBeforeStart) {
    FtpServerServiceFixture fixture;
    EXPECT_FALSE(fixture.service.isRunning());
}

TEST(FtpServerServiceTest, ListenAddressesReturnsOnlyUpNonEmptyIpv4Interfaces) {
    FtpServerServiceFixture fixture;
    fixture.detector.interfaces = {
        makeInterface("192.168.1.23", NetworkInterfaceStatus::Up),  // NOSONAR: private test-interface fixture; no socket is opened.
        makeInterface("10.0.0.5", NetworkInterfaceStatus::Down),   // NOSONAR: private test-interface fixture; no socket is opened.
        makeInterface("", NetworkInterfaceStatus::Up),
    };

    const auto addresses = fixture.service.listenAddresses();

    ASSERT_EQ(addresses.size(), 1u);
    EXPECT_EQ(addresses[0], "192.168.1.23");
}
