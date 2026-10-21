#pragma once

#include "gth/acceleratorserver/IDownloadAcceleratorServerFactory.h"
#include "gth/acceleratorserver/WinsockCachingProxyServer.h"
#include "gth/application/devices/IConsoleManager.h"
#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/network/ILocalGameServer.h"

namespace gth::acceleratorserver {

class DownloadAcceleratorServerFactory : public IDownloadAcceleratorServerFactory {
public:
    DownloadAcceleratorServerFactory(WinsockCachingProxyServer::MirrorResolveFn resolveMirrorIp,
                                       application::IConsoleManager& consoleManager,
                                       application::ICdnUrlService& cdnUrlService,
                                       std::shared_ptr<network::ILocalGameServer> localGameServer);

    std::unique_ptr<core::IDownloadAcceleratorServer> create() override;

private:
    WinsockCachingProxyServer::MirrorResolveFn resolveMirrorIp_;
    application::IConsoleManager& consoleManager_;
    application::ICdnUrlService& cdnUrlService_;
    std::shared_ptr<network::ILocalGameServer> localGameServer_;
};

}  // namespace gth::acceleratorserver
