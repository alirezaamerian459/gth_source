#pragma once

#include <string>
#include <vector>

namespace gth::application {

// Wraps core::IFtpServer + core::INetworkInterfaceDetector: starts/stops the
// LAN FTP server the user points at a local folder, and reports the LAN
// address(es) to display alongside the port so the second laptop's FTP tab
// has something to type in.
class IFtpServerService {
public:
    virtual ~IFtpServerService() = default;

    virtual void start(const std::string& rootPath, int port) = 0;
    // Starts the same server with an optional read-only JB Library virtual
    // subtree. The default keeps existing implementations source-compatible.
    virtual void start(const std::string& rootPath, int port, bool includeJbLibrary) {
        (void)includeJbLibrary;
        start(rootPath, port);
    }
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    // LAN IPv4 addresses of interfaces currently up - reuses
    // core::INetworkInterfaceDetector, already wired in for the Network page.
    virtual std::vector<std::string> listenAddresses() = 0;
    virtual int port() const = 0;
};

}  // namespace gth::application
