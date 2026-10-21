#include "gth/application/ftp/FtpConnectionService.h"

namespace gth::application {

using core::FtpConnection;

FtpConnectionService::FtpConnectionService(core::IFtpConnectionRepository& repository,
                                            core::ICredentialStore& credentialStore)
    : repository_(repository), credentialStore_(credentialStore) {}

std::vector<FtpConnection> FtpConnectionService::getAllConnections() { return repository_.getAll(); }

FtpConnection FtpConnectionService::addConnection(const std::string& name, const std::string& host, int port,
                                                    core::FtpProtocol protocol, const std::string& username,
                                                    const std::string& password, const std::string& remoteBasePath) {
    FtpConnection connection;
    connection.name = name;
    connection.host = host;
    connection.port = port;
    connection.protocol = protocol;
    connection.username = username;
    connection.remoteBasePath = remoteBasePath;
    // credentialTarget is just the connectionId - already unique, no need to
    // invent a second identifier.
    connection.credentialTarget = connection.connectionId;

    credentialStore_.save(connection.credentialTarget, username, password);
    repository_.add(connection);
    return connection;
}

void FtpConnectionService::removeConnection(const std::string& connectionId) {
    auto existing = repository_.getById(connectionId);
    if (existing) credentialStore_.remove(existing->credentialTarget);
    repository_.remove(connectionId);
}

}  // namespace gth::application
