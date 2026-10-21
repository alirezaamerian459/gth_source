#pragma once

#include "gth/core/ftp/IFtpClient.h"

namespace gth::ftp {

// FTP/FTPS client built on libcurl's easy API (WinSSL/Schannel backend - no
// OpenSSL dependency). One instance owns one logical connection: connect()
// stashes the params, disconnect() clears them - libcurl's easy handle
// itself is stateless between calls (it reconnects per-request), which is
// fine for a browse/upload/download UI that isn't holding a control
// connection open between user actions.
class CurlFtpClient : public core::IFtpClient {
public:
    explicit CurlFtpClient(bool useTls);
    ~CurlFtpClient() override;

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
    std::string buildUrl(const std::string& path) const;

    bool useTls_;
    bool connected_ = false;
    core::FtpConnectParams params_;
};

}  // namespace gth::ftp
