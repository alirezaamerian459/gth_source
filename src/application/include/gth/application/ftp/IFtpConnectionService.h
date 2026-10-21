#pragma once

#include <string>
#include <vector>

#include "gth/core/ftp/FtpConnection.h"

namespace gth::application {

// Add/edit/remove/list saved FTP connection profiles. Passwords flow through
// here on add/update but are never returned by getAllConnections()/getById()
// - only IFtpTransferService (which actually connects) resolves the secret,
// via ICredentialStore, at the moment it's needed.
class IFtpConnectionService {
public:
    virtual ~IFtpConnectionService() = default;

    virtual std::vector<core::FtpConnection> getAllConnections() = 0;

    virtual core::FtpConnection addConnection(const std::string& name, const std::string& host, int port,
                                               core::FtpProtocol protocol, const std::string& username,
                                               const std::string& password, const std::string& remoteBasePath) = 0;

    virtual void removeConnection(const std::string& connectionId) = 0;
};

}  // namespace gth::application
