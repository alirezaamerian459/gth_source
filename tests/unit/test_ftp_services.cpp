#include <gtest/gtest.h>

#include <map>

#include "gth/application/ftp/FtpConnectionService.h"
#include "gth/application/ftp/FtpTransferService.h"
#include "gth/database/Database.h"
#include "gth/database/FtpConnectionRepository.h"
#include "gth/database/Migrator.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {

class FakeCredentialStore : public ICredentialStore {
public:
    std::map<std::string, Credential> saved;

    void save(const std::string& target, const std::string& username, const std::string& secret) override {
        saved[target] = {username, secret};
    }
    std::optional<Credential> read(const std::string& target) override {
        auto it = saved.find(target);
        if (it == saved.end()) return std::nullopt;
        return it->second;
    }
    void remove(const std::string& target) override { saved.erase(target); }
};

class FakeFtpClient : public IFtpClient {
public:
    bool connectSucceeds = true;
    bool connected = false;
    FtpConnectParams lastParams;
    std::vector<FtpDirectoryEntry> entriesToReturn;

    FtpOperationResult connect(const FtpConnectParams& params) override {
        lastParams = params;
        connected = connectSucceeds;
        return {connectSucceeds, connectSucceeds ? "" : "boom"};
    }
    void disconnect() override { connected = false; }
    std::vector<FtpDirectoryEntry> listDirectory(const std::string&) override { return entriesToReturn; }
    FtpOperationResult upload(const std::string&, const std::string&,
                               const std::function<void(long long, long long)>&, std::atomic<bool>*) override {
        return {true, ""};
    }
    FtpOperationResult download(const std::string&, const std::string&,
                                 const std::function<void(long long, long long)>&, std::atomic<bool>*) override {
        return {true, ""};
    }
};

class FakeFtpClientFactory : public IFtpClientFactory {
public:
    FakeFtpClient* lastClient = nullptr;
    bool nextConnectSucceeds = true;

    std::unique_ptr<IFtpClient> create(FtpProtocol) override {
        auto client = std::make_unique<FakeFtpClient>();
        client->connectSucceeds = nextConnectSucceeds;
        lastClient = client.get();
        return client;
    }
};

struct FtpServiceFixture {
    Database db{":memory:"};
    FtpConnectionRepository repository{db};
    FakeCredentialStore credentialStore;
    FakeFtpClientFactory clientFactory;
    FtpConnectionService connectionService{repository, credentialStore};
    FtpTransferService transferService{repository, credentialStore, clientFactory};

    FtpServiceFixture() { Migrator::applyMigrations(db); }
};

}  // namespace

TEST(FtpConnectionServiceTest, AddConnectionPersistsProfileAndSavesCredential) {
    FtpServiceFixture fixture;
    auto connection =
        fixture.connectionService.addConnection("Server", "host.example.com", 21, FtpProtocol::Ftp, "user", "pw", "/games");

    EXPECT_EQ(connection.name, "Server");
    EXPECT_EQ(connection.credentialTarget, connection.connectionId);

    auto all = fixture.connectionService.getAllConnections();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].host, "host.example.com");

    auto credential = fixture.credentialStore.read(connection.credentialTarget);
    ASSERT_TRUE(credential.has_value());
    EXPECT_EQ(credential->username, "user");
    EXPECT_EQ(credential->secret, "pw");
}

TEST(FtpConnectionServiceTest, RemoveConnectionDeletesProfileAndCredential) {
    FtpServiceFixture fixture;
    auto connection =
        fixture.connectionService.addConnection("Server", "host", 21, FtpProtocol::Ftp, "user", "pw", "/");

    fixture.connectionService.removeConnection(connection.connectionId);

    EXPECT_TRUE(fixture.connectionService.getAllConnections().empty());
    EXPECT_FALSE(fixture.credentialStore.read(connection.credentialTarget).has_value());
}

TEST(FtpTransferServiceTest, ConnectToResolvesCredentialAndConnectsClient) {
    FtpServiceFixture fixture;
    auto connection =
        fixture.connectionService.addConnection("Server", "host", 2121, FtpProtocol::Sftp, "bob", "hunter2", "/root");

    const auto result = fixture.transferService.connectTo(connection.connectionId);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(fixture.transferService.isConnected());
    ASSERT_NE(fixture.clientFactory.lastClient, nullptr);
    EXPECT_EQ(fixture.clientFactory.lastClient->lastParams.host, "host");
    EXPECT_EQ(fixture.clientFactory.lastClient->lastParams.port, 2121);
    EXPECT_EQ(fixture.clientFactory.lastClient->lastParams.username, "bob");
    EXPECT_EQ(fixture.clientFactory.lastClient->lastParams.password, "hunter2");
}

TEST(FtpTransferServiceTest, ConnectToUnknownConnectionFails) {
    FtpServiceFixture fixture;
    const auto result = fixture.transferService.connectTo("nonexistent");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(fixture.transferService.isConnected());
}

TEST(FtpTransferServiceTest, BrowseReturnsEmptyWhenNotConnected) {
    FtpServiceFixture fixture;
    EXPECT_TRUE(fixture.transferService.browse("/").empty());
}

TEST(FtpTransferServiceTest, DisconnectClearsConnectedState) {
    FtpServiceFixture fixture;
    auto connection = fixture.connectionService.addConnection("Server", "host", 21, FtpProtocol::Ftp, "u", "p", "/");
    fixture.transferService.connectTo(connection.connectionId);
    ASSERT_TRUE(fixture.transferService.isConnected());

    fixture.transferService.disconnect();
    EXPECT_FALSE(fixture.transferService.isConnected());
}
