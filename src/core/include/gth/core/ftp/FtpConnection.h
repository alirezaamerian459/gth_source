#pragma once

#include <string>
#include <string_view>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

enum class FtpProtocol {
    Ftp = 0,
    Ftps = 1,
    Sftp = 2,
};

std::string toString(FtpProtocol protocol);
FtpProtocol ftpProtocolFromString(std::string_view text);

// A saved FTP/FTPS/SFTP connection profile. The password/passphrase is
// deliberately not a field here: it never round-trips through the database.
// credentialTarget is the opaque key the actual secret is filed under in
// ICredentialStore (see WindowsCredentialStore) - looked up at connect time,
// never loaded eagerly.
struct FtpConnection {
    std::string connectionId = Guid::newGuid();
    std::string name;
    std::string host;
    int port = 21;
    FtpProtocol protocol = FtpProtocol::Ftp;
    std::string username;
    std::string credentialTarget;
    std::string remoteBasePath = "/";

    std::string createdAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
