#include "gth/application/ftp/FtpTransferService.h"

namespace gth::application {

FtpTransferService::FtpTransferService(core::IFtpConnectionRepository& repository,
                                        core::ICredentialStore& credentialStore,
                                        core::IFtpClientFactory& clientFactory)
    : repository_(repository), credentialStore_(credentialStore), clientFactory_(clientFactory) {}

core::FtpOperationResult FtpTransferService::connectTo(const std::string& connectionId) {
    auto connection = repository_.getById(connectionId);
    if (!connection.has_value()) return {false, "Connection not found."};

    auto credential = credentialStore_.read(connection->credentialTarget);
    if (!credential.has_value()) return {false, "Saved credentials not found - re-add this connection."};

    activeClient_ = clientFactory_.create(connection->protocol);

    core::FtpConnectParams params;
    params.host = connection->host;
    params.port = connection->port;
    params.username = credential->username;
    params.password = credential->secret;
    params.remoteBasePath = connection->remoteBasePath;

    const auto result = activeClient_->connect(params);
    if (!result.success) activeClient_.reset();
    return result;
}

void FtpTransferService::disconnect() {
    if (activeClient_) activeClient_->disconnect();
    activeClient_.reset();
}

bool FtpTransferService::isConnected() const { return activeClient_ != nullptr; }

std::vector<core::FtpDirectoryEntry> FtpTransferService::browse(const std::string& path) {
    if (!activeClient_) return {};
    return activeClient_->listDirectory(path);
}

core::FtpOperationResult FtpTransferService::upload(const std::string& localPath, const std::string& remotePath,
                                                      const std::function<void(long long, long long)>& onProgress,
                                                      std::atomic<bool>* cancelFlag) {
    if (!activeClient_) return {false, "Not connected."};
    return activeClient_->upload(localPath, remotePath, onProgress, cancelFlag);
}

core::FtpOperationResult FtpTransferService::download(const std::string& remotePath, const std::string& localPath,
                                                        const std::function<void(long long, long long)>& onProgress,
                                                        std::atomic<bool>* cancelFlag) {
    if (!activeClient_) return {false, "Not connected."};
    return activeClient_->download(remotePath, localPath, onProgress, cancelFlag);
}

}  // namespace gth::application
