#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gth/core/ftp/IFtpServer.h"

namespace gth::ftpserver {

// Winsock2 FTP server: a background accept loop on `port` (mirrors
// network::CentralServerListener's non-blocking listen socket +
// select()-based accept loop exactly), one worker thread per control
// connection. Each client thread runs just enough of the FTP command set for
// CurlFtpClient (this app's own client, passive-mode only) to browse/
// upload/download: USER/PASS (always accepted - there is no login, by
// explicit design for trusted-LAN use), SYST, TYPE, PWD, CWD/CDUP, PASV,
// LIST/NLST, RETR, STOR, QUIT. No PORT/active mode - our client never uses
// it.
//
// Every path the server resolves is clamped under the configured root
// folder: CWD, LIST/NLST, RETR and STOR targets can never escape it, even
// via ".." or an OS-absolute-looking argument - see the path-escape test in
// tests/unit/test_winsock_ftp_server.cpp.
class WinsockFtpServer : public core::IFtpServer {
public:
    ~WinsockFtpServer() override;

    void start(const core::FtpServerConfig& config) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }
    int port() const override { return port_.load(); }

private:
    void acceptLoop(core::FtpServerConfig config);
    void handleClient(std::uintptr_t clientSocket, std::string rootPath,
                      std::shared_ptr<const std::vector<core::FtpVirtualMapping>> virtualMappings);

    std::jthread acceptThread_;
    std::atomic<bool> running_{false};
    std::atomic<int> port_{0};
};

}  // namespace gth::ftpserver
