#pragma once

#include "gth/core/ftp/IFtpClientFactory.h"

namespace gth::ftp {

// Concrete factory: Ftp/Ftps -> CurlFtpClient (TLS toggled by protocol),
// Sftp -> Libssh2SftpClient.
class FtpClientFactory : public core::IFtpClientFactory {
public:
    std::unique_ptr<core::IFtpClient> create(core::FtpProtocol protocol) override;
};

}  // namespace gth::ftp
