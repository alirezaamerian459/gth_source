#pragma once

#include "gth/application/ftp/IFtpConnectionService.h"
#include "gth/core/credentials/ICredentialStore.h"
#include "gth/core/ftp/IFtpConnectionRepository.h"

namespace gth::application {

class FtpConnectionService : public IFtpConnectionService {
public:
    FtpConnectionService(core::IFtpConnectionRepository& repository, core::ICredentialStore& credentialStore);

    std::vector<core::FtpConnection> getAllConnections() override;

    core::FtpConnection addConnection(const std::string& name, const std::string& host, int port,
                                       core::FtpProtocol protocol, const std::string& username,
                                       const std::string& password, const std::string& remoteBasePath) override;

    void removeConnection(const std::string& connectionId) override;

private:
    core::IFtpConnectionRepository& repository_;
    core::ICredentialStore& credentialStore_;
};

}  // namespace gth::application
