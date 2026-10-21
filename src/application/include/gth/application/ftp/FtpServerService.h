#pragma once

#include "gth/application/ftp/IFtpServerService.h"
#include "gth/core/ftp/IFtpServer.h"
#include "gth/core/network/INetworkInterfaceDetector.h"

namespace gth::application {

class FtpServerService : public IFtpServerService {
public:
    FtpServerService(core::IFtpServer& server, core::INetworkInterfaceDetector& networkInterfaceDetector);

    void start(const std::string& rootPath, int port) override;
    void stop() override;
    bool isRunning() const override;
    std::vector<std::string> listenAddresses() override;
    int port() const override;

private:
    core::IFtpServer& server_;
    core::INetworkInterfaceDetector& networkInterfaceDetector_;
};

}  // namespace gth::application
