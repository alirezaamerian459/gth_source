#pragma once

#include <memory>

#include "gth/application/ftp/IFtpTransferService.h"
#include "gth/core/credentials/ICredentialStore.h"
#include "gth/core/ftp/IFtpClientFactory.h"
#include "gth/core/ftp/IFtpConnectionRepository.h"

namespace gth::application {

class FtpTransferService : public IFtpTransferService {
public:
    FtpTransferService(core::IFtpConnectionRepository& repository, core::ICredentialStore& credentialStore,
                        core::IFtpClientFactory& clientFactory);

    core::FtpOperationResult connectTo(const std::string& connectionId) override;
    void disconnect() override;
    bool isConnected() const override;

    std::vector<core::FtpDirectoryEntry> browse(const std::string& path) override;

    core::FtpOperationResult upload(const std::string& localPath, const std::string& remotePath,
                                     const std::function<void(long long, long long)>& onProgress,
                                     std::atomic<bool>* cancelFlag) override;

    core::FtpOperationResult download(const std::string& remotePath, const std::string& localPath,
                                       const std::function<void(long long, long long)>& onProgress,
                                       std::atomic<bool>* cancelFlag) override;

private:
    core::IFtpConnectionRepository& repository_;
    core::ICredentialStore& credentialStore_;
    core::IFtpClientFactory& clientFactory_;
    std::unique_ptr<core::IFtpClient> activeClient_;
};

}  // namespace gth::application
