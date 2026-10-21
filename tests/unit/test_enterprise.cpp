#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "gth/application/enterprise/AgentService.h"
#include "gth/application/enterprise/CentralServerService.h"
#include "gth/application/reports/ReportService.h"
#include "gth/database/ConsoleRepository.h"
#include "gth/database/Database.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/RemoteNodeRepository.h"
#include "gth/database/StorageRepository.h"
#include "gth/database/TransferJobRepository.h"
#include "gth/database/UpdateInfoRepository.h"
#include "gth/database/UpdateJobRepository.h"
#include "gth/network/CentralServerListener.h"
#include "gth/network/TcpRemoteReportSender.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;
using namespace gth::network;

namespace {

// Waits (with a hard timeout so a bug can't hang the test suite forever)
// until predicate() is true.
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

// A fresh in-memory-DB'd ReportService per test, exactly like
// test_report_service.cpp's ReportFixture, so AgentService has something
// real to report.
struct EnterpriseFixture {
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository gameVariantRepository{db};
    GameVersionRepository gameVersionRepository{db};
    UpdateInfoRepository updateInfoRepository{db};
    UpdateJobRepository updateJobRepository{db};
    ConsoleRepository consoleRepository{db};
    TransferJobRepository transferJobRepository{db};
    ReportService reportService{storageRepository,   gameRepository,     gameVariantRepository, gameVersionRepository,
                                 updateInfoRepository, updateJobRepository, consoleRepository,     transferJobRepository};

    RemoteNodeRepository remoteNodeRepository{db};
    CentralServerListener listener;
    CentralServerService serverService{remoteNodeRepository, listener};

    TcpRemoteReportSender sender;

    EnterpriseFixture() { Migrator::applyMigrations(db); }
    ~EnterpriseFixture() { serverService.stop(); }
};

}  // namespace

TEST(CentralServerListenerTest, InvokesHandlerWithParsedFieldsAndAcks) {
    CentralServerListener listener;

    std::mutex mutex;
    std::condition_variable cv;
    bool received = false;
    std::string capturedNodeId, capturedName, capturedSummary, capturedAddress;

    listener.start(58231, [&](const std::string& nodeId, const std::string& name, const std::string& summary,
                               const std::string& address) {
        std::lock_guard<std::mutex> lock(mutex);
        capturedNodeId = nodeId;
        capturedName = name;
        capturedSummary = summary;
        capturedAddress = address;
        received = true;
        cv.notify_all();
    });

    TcpRemoteReportSender sender;
    bool sendSucceeded = false;
    ASSERT_TRUE(waitFor([&] { return sendSucceeded = sender.send("127.0.0.1", 58231, "node-1", "PC-A", "hello"); }));
    EXPECT_TRUE(sendSucceeded);

    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return received; }));
    EXPECT_EQ(capturedNodeId, "node-1");
    EXPECT_EQ(capturedName, "PC-A");
    EXPECT_EQ(capturedSummary, "hello");
    EXPECT_EQ(capturedAddress, "127.0.0.1");

    listener.stop();
}

TEST(CentralServerListenerTest, SummaryFieldMayContainFurtherStructure) {
    CentralServerListener listener;
    std::mutex mutex;
    std::condition_variable cv;
    bool received = false;
    std::string capturedSummary;

    listener.start(58232, [&](const std::string&, const std::string&, const std::string& summary,
                               const std::string&) {
        std::lock_guard<std::mutex> lock(mutex);
        capturedSummary = summary;
        received = true;
        cv.notify_all();
    });

    TcpRemoteReportSender sender;
    ASSERT_TRUE(waitFor(
        [&] { return sender.send("127.0.0.1", 58232, "node-2", "PC-B", "Storages=1/1 online, Games=3"); }));

    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return received; }));
    EXPECT_EQ(capturedSummary, "Storages=1/1 online, Games=3");

    listener.stop();
}

TEST(CentralServerServiceTest, ReceivedReportUpsertsRemoteNodeRow) {
    EnterpriseFixture fixture;
    fixture.serverService.start(58233);

    ASSERT_TRUE(waitFor([&] { return fixture.sender.send("127.0.0.1", 58233, "node-3", "PC-C", "a summary"); }));
    ASSERT_TRUE(waitFor([&] { return !fixture.serverService.getAllNodes().empty(); }));

    auto nodes = fixture.serverService.getAllNodes();
    ASSERT_EQ(nodes.size(), 1u);
    EXPECT_EQ(nodes[0].nodeId, "node-3");
    EXPECT_EQ(nodes[0].name, "PC-C");
    EXPECT_EQ(nodes[0].address, "127.0.0.1");
    EXPECT_EQ(nodes[0].lastReportSummary, "a summary");
    EXPECT_TRUE(nodes[0].lastSeenAt.has_value());
}

TEST(CentralServerServiceTest, SecondReportFromSameNodeOverwritesRatherThanDuplicates) {
    EnterpriseFixture fixture;
    fixture.serverService.start(58234);

    ASSERT_TRUE(waitFor([&] { return fixture.sender.send("127.0.0.1", 58234, "node-4", "PC-D", "first"); }));
    ASSERT_TRUE(waitFor([&] { return !fixture.serverService.getAllNodes().empty(); }));

    ASSERT_TRUE(waitFor([&] { return fixture.sender.send("127.0.0.1", 58234, "node-4", "PC-D", "second"); }));
    ASSERT_TRUE(waitFor([&] {
        auto nodes = fixture.serverService.getAllNodes();
        return nodes.size() == 1u && nodes[0].lastReportSummary == "second";
    }));

    EXPECT_EQ(fixture.serverService.getAllNodes().size(), 1u);
}

TEST(AgentServiceTest, StartSendsImmediateReportAndRegistersWithServer) {
    EnterpriseFixture fixture;
    fixture.serverService.start(58235);

    // Give the listener a moment to actually bind before the agent's very
    // first (synchronous) report attempt.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    AgentService agent(fixture.reportService, fixture.sender, "agent-node-1");
    const bool firstReportSucceeded = agent.start("127.0.0.1", 58235, "Agent PC", 3600);

    EXPECT_TRUE(firstReportSucceeded);
    EXPECT_TRUE(agent.isRunning());
    EXPECT_EQ(agent.localNodeId(), "agent-node-1");

    ASSERT_TRUE(waitFor([&] { return !fixture.serverService.getAllNodes().empty(); }));
    auto nodes = fixture.serverService.getAllNodes();
    ASSERT_EQ(nodes.size(), 1u);
    EXPECT_EQ(nodes[0].nodeId, "agent-node-1");
    EXPECT_EQ(nodes[0].name, "Agent PC");
    EXPECT_NE(nodes[0].lastReportSummary->find("Storages="), std::string::npos);

    agent.stop();
    EXPECT_FALSE(agent.isRunning());
}

TEST(AgentServiceTest, StartReportsFailureWhenNoServerIsListening) {
    EnterpriseFixture fixture;
    AgentService agent(fixture.reportService, fixture.sender, "agent-node-2");

    // Port 1 is reserved/unassigned - essentially never has a listener.
    const bool firstReportSucceeded = agent.start("127.0.0.1", 1, "Agent PC", 3600);

    EXPECT_FALSE(firstReportSucceeded);
    agent.stop();
}
