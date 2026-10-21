#pragma once

#include <string>
#include <vector>

#include "gth/core/ftp/IFtpClient.h"

namespace gth::application {

// Drives one connect/browse/upload/download session against a saved
// FtpConnection. connect()/disconnect() bracket the session; browse/
// upload/download are only meaningful while connected.
class IFtpTransferService {
public:
    virtual ~IFtpTransferService() = default;

    virtual core::FtpOperationResult connectTo(const std::string& connectionId) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual std::vector<core::FtpDirectoryEntry> browse(const std::string& path) = 0;

    virtual core::FtpOperationResult upload(const std::string& localPath, const std::string& remotePath,
                                             const std::function<void(long long, long long)>& onProgress,
                                             std::atomic<bool>* cancelFlag) = 0;

    virtual core::FtpOperationResult download(const std::string& remotePath, const std::string& localPath,
                                               const std::function<void(long long, long long)>& onProgress,
                                               std::atomic<bool>* cancelFlag) = 0;
};

}  // namespace gth::application
