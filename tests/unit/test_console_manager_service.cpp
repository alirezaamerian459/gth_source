#include <gtest/gtest.h>

#include "gth/application/devices/ConsoleManagerService.h"
#include "gth/database/ConsoleRepository.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

class FakeReachabilityChecker : public IReachabilityChecker {
public:
    bool shouldSucceed = true;
    std::string lastIp;
    int lastPort = 0;

    bool isReachable(const std::string& ipAddress, int port, int) override {
        lastIp = ipAddress;
        lastPort = port;
        return shouldSucceed;
    }
};

struct ConsoleFixture {
    Database db{":memory:"};
    ConsoleRepository repository{db};
    FakeReachabilityChecker checker;
    ConsoleManagerService service{repository, checker};

    ConsoleFixture() { Migrator::applyMigrations(db); }
};

}  // namespace

TEST(ConsoleManagerServiceTest, AddConsolePersistsIt) {
    ConsoleFixture fixture;
    auto console = fixture.service.addConsole("Living Room PS5", "PS5", "192.168.1.50", 2121, ConsoleProtocol::Ftp);  // NOSONAR: RFC1918 fixture value; this fake service does not connect to it.

    EXPECT_EQ(console.name, "Living Room PS5");
    EXPECT_EQ(console.status, ConsoleStatus::Unknown);

    auto all = fixture.service.getAllConsoles();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].ipAddress, "192.168.1.50");
    EXPECT_EQ(all[0].port, 2121);
    EXPECT_EQ(all[0].protocol, ConsoleProtocol::Ftp);
}

TEST(ConsoleManagerServiceTest, RemoveConsoleDeletesIt) {
    ConsoleFixture fixture;
    auto console = fixture.service.addConsole("Test", "Switch", "192.168.1.60", 21, ConsoleProtocol::Ftp);  // NOSONAR: RFC1918 fixture value; this fake service does not connect to it.

    fixture.service.removeConsole(console.consoleId);

    EXPECT_TRUE(fixture.service.getAllConsoles().empty());
}

TEST(ConsoleManagerServiceTest, CheckReachabilityUpdatesStatusToOnlineAndSetsLastConnected) {
    ConsoleFixture fixture;
    auto console = fixture.service.addConsole("Test", "PS5", "192.168.1.50", 2121, ConsoleProtocol::Ftp);  // NOSONAR: RFC1918 fixture value; this fake service does not connect to it.
    fixture.checker.shouldSucceed = true;

    auto status = fixture.service.checkReachability(console.consoleId);

    EXPECT_EQ(status, ConsoleStatus::Online);
    EXPECT_EQ(fixture.checker.lastIp, "192.168.1.50");
    EXPECT_EQ(fixture.checker.lastPort, 2121);

    auto all = fixture.service.getAllConsoles();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].status, ConsoleStatus::Online);
    EXPECT_TRUE(all[0].lastConnected.has_value());
}

TEST(ConsoleManagerServiceTest, CheckReachabilityUpdatesStatusToOfflineWithoutTouchingLastConnected) {
    ConsoleFixture fixture;
    auto console = fixture.service.addConsole("Test", "PS5", "192.168.1.50", 2121, ConsoleProtocol::Ftp);  // NOSONAR: RFC1918 fixture value; this fake service does not connect to it.
    fixture.checker.shouldSucceed = false;

    auto status = fixture.service.checkReachability(console.consoleId);

    EXPECT_EQ(status, ConsoleStatus::Offline);

    auto all = fixture.service.getAllConsoles();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].status, ConsoleStatus::Offline);
    EXPECT_FALSE(all[0].lastConnected.has_value());
}

TEST(ConsoleManagerServiceTest, CheckReachabilityReturnsUnknownForNonexistentConsole) {
    ConsoleFixture fixture;
    auto status = fixture.service.checkReachability("nonexistent-id");
    EXPECT_EQ(status, ConsoleStatus::Unknown);
}

TEST(ConsoleManagerServiceTest, RecordConnectionCreatesAutoDetectedConsoleOnFirstSighting) {
    ConsoleFixture fixture;
    fixture.service.recordConnection("192.168.1.77", "libhttp/1.0 (PlayStation 5)", 8080, ConsoleProtocol::Http);  // NOSONAR: RFC1918 fixture value; no socket is opened.

    auto all = fixture.service.getAllConsoles();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].ipAddress, "192.168.1.77");
    EXPECT_EQ(all[0].platform, "libhttp/1.0 (PlayStation 5)");
    EXPECT_EQ(all[0].port, 8080);
    EXPECT_EQ(all[0].protocol, ConsoleProtocol::Http);
    EXPECT_EQ(all[0].status, ConsoleStatus::Online);
    EXPECT_TRUE(all[0].autoDetected);
    EXPECT_TRUE(all[0].lastConnected.has_value());
}

TEST(ConsoleManagerServiceTest, RecordConnectionUpsertsExistingConsoleByIpInsteadOfDuplicating) {
    ConsoleFixture fixture;
    fixture.service.recordConnection("192.168.1.77", "", 8080, ConsoleProtocol::Http);  // NOSONAR: RFC1918 fixture value; no socket is opened.
    auto firstPass = fixture.service.getAllConsoles();
    ASSERT_EQ(firstPass.size(), 1u);
    const std::string consoleId = firstPass[0].consoleId;

    fixture.service.recordConnection("192.168.1.77", "", 8081, ConsoleProtocol::Http);  // NOSONAR: RFC1918 fixture value; no socket is opened.

    auto all = fixture.service.getAllConsoles();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].consoleId, consoleId);
    EXPECT_EQ(all[0].port, 8081);
}

TEST(ConsoleManagerServiceTest, RecordConnectionNeverOverwritesAnAlreadyKnownPlatformOrManualFlag) {
    ConsoleFixture fixture;
    fixture.service.addConsole("Living Room PS5", "PS5", "192.168.1.77", 2121, ConsoleProtocol::Ftp);  // NOSONAR: RFC1918 fixture value; this fake service does not connect to it.

    fixture.service.recordConnection("192.168.1.77", "some-other-hint", 8080, ConsoleProtocol::Http);  // NOSONAR: RFC1918 fixture value; no socket is opened.

    auto all = fixture.service.getAllConsoles();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].platform, "PS5");
    EXPECT_FALSE(all[0].autoDetected);
    EXPECT_EQ(all[0].status, ConsoleStatus::Online);
}

TEST(ConsoleManagerServiceTest, RecordConnectionIgnoresEmptyIpAddress) {
    ConsoleFixture fixture;
    fixture.service.recordConnection("", "hint", 8080, ConsoleProtocol::Http);
    EXPECT_TRUE(fixture.service.getAllConsoles().empty());
}

TEST(ConsoleTest, ProtocolAndStatusToStringRoundTrip) {
    EXPECT_EQ(consoleProtocolFromString(toString(ConsoleProtocol::Ftp)), ConsoleProtocol::Ftp);
    EXPECT_EQ(consoleProtocolFromString(toString(ConsoleProtocol::Smb)), ConsoleProtocol::Smb);
    EXPECT_EQ(consoleProtocolFromString(toString(ConsoleProtocol::Custom)), ConsoleProtocol::Custom);
    EXPECT_EQ(consoleProtocolFromString(toString(ConsoleProtocol::Http)), ConsoleProtocol::Http);

    EXPECT_EQ(consoleStatusFromString(toString(ConsoleStatus::Online)), ConsoleStatus::Online);
    EXPECT_EQ(consoleStatusFromString(toString(ConsoleStatus::Offline)), ConsoleStatus::Offline);
}
