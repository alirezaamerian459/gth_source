#include "gth/ftp/FtpClientFactory.h"

#include "gth/ftp/CurlFtpClient.h"
#include "gth/ftp/Libssh2SftpClient.h"

namespace gth::ftp {

std::unique_ptr<core::IFtpClient> FtpClientFactory::create(core::FtpProtocol protocol) {
    switch (protocol) {
        case core::FtpProtocol::Ftps: return std::make_unique<CurlFtpClient>(true);
        case core::FtpProtocol::Sftp: return std::make_unique<Libssh2SftpClient>();
        case core::FtpProtocol::Ftp:
        default: return std::make_unique<CurlFtpClient>(false);
    }
}

}  // namespace gth::ftp
