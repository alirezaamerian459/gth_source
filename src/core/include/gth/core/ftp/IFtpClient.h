#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace gth::core {

struct FtpConnectParams {
    std::string host;
    int port = 21;
    std::string username;
    std::string password;
    std::string remoteBasePath = "/";
};

struct FtpDirectoryEntry {
    std::string name;
    bool isDirectory = false;
    long long sizeBytes = 0;
};

struct FtpOperationResult {
    bool success = false;
    std::string errorMessage;
};

// Protocol-specific FTP/FTPS/SFTP client. Mirrors ITransferProvider's
// progress/cancel signature (onProgress(bytesDone, bytesTotal), an optional
// cancelFlag polled at the next opportunity the underlying transfer API
// offers) so a future FtpTransferProvider can reuse the same shape.
class IFtpClient {
public:
    virtual ~IFtpClient() = default;

    virtual FtpOperationResult connect(const FtpConnectParams& params) = 0;
    virtual void disconnect() = 0;

    virtual std::vector<FtpDirectoryEntry> listDirectory(const std::string& path) = 0;

    virtual FtpOperationResult upload(const std::string& localPath, const std::string& remotePath,
                                       const std::function<void(long long, long long)>& onProgress,
                                       std::atomic<bool>* cancelFlag) = 0;

    virtual FtpOperationResult download(const std::string& remotePath, const std::string& localPath,
                                         const std::function<void(long long, long long)>& onProgress,
                                         std::atomic<bool>* cancelFlag) = 0;
};

}  // namespace gth::core
