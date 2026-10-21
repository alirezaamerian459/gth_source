#pragma once

#include "gth/core/ftp/IFtpClient.h"
#include "gth/core/transfer/ITransferProvider.h"

namespace gth::ftp {

// Adapts an already-connected IFtpClient to ITransferProvider, so a single
// FTP file transfer can optionally be queued through TransferQueueService
// like any Local/SMB copy. The new FTP tab itself talks to IFtpClient
// directly for its browse/upload/download UI (see FtpTransferService) - this
// adapter exists for the future case described in the plan, not the tab's
// own code path.
class FtpTransferProvider : public core::ITransferProvider {
public:
    explicit FtpTransferProvider(core::IFtpClient& client);

    std::string name() const override;

    core::TransferOutcome copyFile(const std::string& sourcePath, const std::string& destinationPath,
                                    const std::function<void(long long, long long)>& onProgress,
                                    std::atomic<bool>* cancelFlag) override;

private:
    core::IFtpClient& client_;
};

}  // namespace gth::ftp
