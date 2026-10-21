#include "gth/ftp/FtpTransferProvider.h"

namespace gth::ftp {

FtpTransferProvider::FtpTransferProvider(core::IFtpClient& client) : client_(client) {}

std::string FtpTransferProvider::name() const { return "FTP"; }

core::TransferOutcome FtpTransferProvider::copyFile(const std::string& sourcePath, const std::string& destinationPath,
                                                      const std::function<void(long long, long long)>& onProgress,
                                                      std::atomic<bool>* cancelFlag) {
    const auto result = client_.upload(sourcePath, destinationPath, onProgress, cancelFlag);
    return {result.success, result.errorMessage};
}

}  // namespace gth::ftp
