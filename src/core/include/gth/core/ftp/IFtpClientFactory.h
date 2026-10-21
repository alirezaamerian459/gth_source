#pragma once

#include <memory>

#include "gth/core/ftp/FtpConnection.h"
#include "gth/core/ftp/IFtpClient.h"

namespace gth::core {

// Creates the protocol-appropriate IFtpClient implementation (FTP/FTPS via
// libcurl, SFTP via libssh2) - see gth::ftp::makeFtpClient in
// src/ftp/include/gth/ftp/FtpClientFactory.h for the concrete factory.
class IFtpClientFactory {
public:
    virtual ~IFtpClientFactory() = default;
    virtual std::unique_ptr<IFtpClient> create(FtpProtocol protocol) = 0;
};

}  // namespace gth::core
