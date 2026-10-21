#include "gth/application/ftp/FtpServerService.h"

namespace gth::application {

FtpServerService::FtpServerService(core::IFtpServer& server, core::INetworkInterfaceDetector& networkInterfaceDetector)
    : server_(server), networkInterfaceDetector_(networkInterfaceDetector) {}

void FtpServerService::start(const std::string& rootPath, int port) {
    core::FtpServerConfig config;
    config.rootPath = rootPath;
    config.port = port;
    server_.start(config);
}

void FtpServerService::stop() { server_.stop(); }

bool FtpServerService::isRunning() const { return server_.isRunning(); }

int FtpServerService::port() const { return server_.port(); }

std::vector<std::string> FtpServerService::listenAddresses() {
    std::vector<std::string> addresses;
    for (const auto& iface : networkInterfaceDetector_.detect()) {
        if (!iface.ipv4.empty() && iface.status == core::NetworkInterfaceStatus::Up) addresses.push_back(iface.ipv4);
    }
    return addresses;
}

}  // namespace gth::application
