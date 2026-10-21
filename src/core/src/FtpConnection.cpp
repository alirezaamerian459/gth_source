#include "gth/core/ftp/FtpConnection.h"

namespace gth::core {

std::string toString(FtpProtocol protocol) {
    switch (protocol) {
        case FtpProtocol::Ftps: return "Ftps";
        case FtpProtocol::Sftp: return "Sftp";
        case FtpProtocol::Ftp:
        default: return "Ftp";
    }
}

FtpProtocol ftpProtocolFromString(std::string_view text) {
    if (text == "Ftps") return FtpProtocol::Ftps;
    if (text == "Sftp") return FtpProtocol::Sftp;
    return FtpProtocol::Ftp;
}

}  // namespace gth::core
