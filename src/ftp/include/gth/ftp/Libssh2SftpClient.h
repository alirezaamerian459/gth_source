#pragma once

#include "gth/core/ftp/IFtpClient.h"

using SOCKET = unsigned __int64;

namespace gth::ftp {

// SFTP client built directly on libssh2's session/sftp API over a raw
// Winsock TCP socket (libssh2 itself is transport-agnostic - it just wants a
// connected socket handle). libssh2 is built against WinCNG here (see
// src/ftp/CMakeLists.txt), so no OpenSSL dependency is introduced.
class Libssh2SftpClient : public core::IFtpClient {
public:
    Libssh2SftpClient();
    ~Libssh2SftpClient() override;

    core::FtpOperationResult connect(const core::FtpConnectParams& params) override;
    void disconnect() override;

    std::vector<core::FtpDirectoryEntry> listDirectory(const std::string& path) override;

    core::FtpOperationResult upload(const std::string& localPath, const std::string& remotePath,
                                     const std::function<void(long long, long long)>& onProgress,
                                     std::atomic<bool>* cancelFlag) override;

    core::FtpOperationResult download(const std::string& remotePath, const std::string& localPath,
                                       const std::function<void(long long, long long)>& onProgress,
                                       std::atomic<bool>* cancelFlag) override;

private:
    void* session_ = nullptr;   // LIBSSH2_SESSION*
    void* sftp_ = nullptr;      // LIBSSH2_SFTP*
    SOCKET socket_ = 0;
    bool connected_ = false;
};

}  // namespace gth::ftp
