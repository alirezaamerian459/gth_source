#include "gth/database/FtpConnectionRepository.h"

namespace gth::database {

using core::FtpConnection;

namespace {
constexpr const char* kSelectColumns =
    "FtpConnectionId, Name, Host, Port, Protocol, Username, CredentialTarget, RemoteBasePath, CreatedAt";

FtpConnection mapRow(const Statement& stmt) {
    FtpConnection connection;
    connection.connectionId = stmt.columnText(0);
    connection.name = stmt.columnText(1);
    connection.host = stmt.columnText(2);
    connection.port = static_cast<int>(stmt.columnInt64(3));
    connection.protocol = core::ftpProtocolFromString(stmt.columnText(4));
    connection.username = stmt.columnText(5);
    connection.credentialTarget = stmt.columnText(6);
    connection.remoteBasePath = stmt.columnText(7);
    connection.createdAt = stmt.columnText(8);
    return connection;
}
}  // namespace

FtpConnectionRepository::FtpConnectionRepository(Database& db) : db_(db) {}

std::vector<FtpConnection> FtpConnectionRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM FtpConnections ORDER BY Name;");
    std::vector<FtpConnection> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<FtpConnection> FtpConnectionRepository::getById(const std::string& connectionId) {
    auto stmt =
        db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM FtpConnections WHERE FtpConnectionId = ?;");
    stmt.bind(1, connectionId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void FtpConnectionRepository::add(const FtpConnection& connection) {
    auto stmt = db_.prepare(
        "INSERT INTO FtpConnections (FtpConnectionId, Name, Host, Port, Protocol, Username, CredentialTarget, "
        "RemoteBasePath, CreatedAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, connection.connectionId);
    stmt.bind(2, connection.name);
    stmt.bind(3, connection.host);
    stmt.bind(4, static_cast<long long>(connection.port));
    stmt.bind(5, core::toString(connection.protocol));
    stmt.bind(6, connection.username);
    stmt.bind(7, connection.credentialTarget);
    stmt.bind(8, connection.remoteBasePath);
    stmt.bind(9, connection.createdAt);
    stmt.step();
}

void FtpConnectionRepository::update(const FtpConnection& connection) {
    auto stmt = db_.prepare(
        "UPDATE FtpConnections SET Name = ?, Host = ?, Port = ?, Protocol = ?, Username = ?, CredentialTarget = ?, "
        "RemoteBasePath = ? WHERE FtpConnectionId = ?;");
    stmt.bind(1, connection.name);
    stmt.bind(2, connection.host);
    stmt.bind(3, static_cast<long long>(connection.port));
    stmt.bind(4, core::toString(connection.protocol));
    stmt.bind(5, connection.username);
    stmt.bind(6, connection.credentialTarget);
    stmt.bind(7, connection.remoteBasePath);
    stmt.bind(8, connection.connectionId);
    stmt.step();
}

void FtpConnectionRepository::remove(const std::string& connectionId) {
    auto stmt = db_.prepare("DELETE FROM FtpConnections WHERE FtpConnectionId = ?;");
    stmt.bind(1, connectionId);
    stmt.step();
}

}  // namespace gth::database
