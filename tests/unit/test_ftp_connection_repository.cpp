#include <gtest/gtest.h>

#include "gth/database/Database.h"
#include "gth/database/FtpConnectionRepository.h"
#include "gth/database/Migrator.h"

using namespace gth::core;
using namespace gth::database;

namespace {
struct FtpConnectionFixture {
    Database db{":memory:"};
    FtpConnectionRepository repository{db};

    FtpConnectionFixture() { Migrator::applyMigrations(db); }
};

FtpConnection makeConnection() {
    FtpConnection connection;
    connection.name = "My Server";
    connection.host = "192.168.1.10";
    connection.port = 21;
    connection.protocol = FtpProtocol::Ftp;
    connection.username = "user";
    connection.credentialTarget = connection.connectionId;
    connection.remoteBasePath = "/";
    return connection;
}
}  // namespace

TEST(FtpConnectionRepositoryTest, AddAndGetAll) {
    FtpConnectionFixture fixture;
    auto connection = makeConnection();
    fixture.repository.add(connection);

    auto all = fixture.repository.getAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].name, "My Server");
    EXPECT_EQ(all[0].host, "192.168.1.10");
    EXPECT_EQ(all[0].port, 21);
    EXPECT_EQ(all[0].protocol, FtpProtocol::Ftp);
    EXPECT_EQ(all[0].credentialTarget, connection.connectionId);
}

TEST(FtpConnectionRepositoryTest, GetByIdReturnsNulloptWhenMissing) {
    FtpConnectionFixture fixture;
    EXPECT_FALSE(fixture.repository.getById("nonexistent").has_value());
}

TEST(FtpConnectionRepositoryTest, UpdatePersistsChanges) {
    FtpConnectionFixture fixture;
    auto connection = makeConnection();
    fixture.repository.add(connection);

    connection.name = "Renamed";
    connection.protocol = FtpProtocol::Sftp;
    fixture.repository.update(connection);

    auto fetched = fixture.repository.getById(connection.connectionId);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->name, "Renamed");
    EXPECT_EQ(fetched->protocol, FtpProtocol::Sftp);
}

TEST(FtpConnectionRepositoryTest, RemoveDeletesRow) {
    FtpConnectionFixture fixture;
    auto connection = makeConnection();
    fixture.repository.add(connection);

    fixture.repository.remove(connection.connectionId);

    EXPECT_TRUE(fixture.repository.getAll().empty());
}

TEST(FtpConnectionTest, ProtocolToStringRoundTrip) {
    EXPECT_EQ(ftpProtocolFromString(toString(FtpProtocol::Ftp)), FtpProtocol::Ftp);
    EXPECT_EQ(ftpProtocolFromString(toString(FtpProtocol::Ftps)), FtpProtocol::Ftps);
    EXPECT_EQ(ftpProtocolFromString(toString(FtpProtocol::Sftp)), FtpProtocol::Sftp);
}
