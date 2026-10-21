#include "gth/acceleratorserver/DownloadAcceleratorServerFactory.h"

namespace gth::acceleratorserver {

DownloadAcceleratorServerFactory::DownloadAcceleratorServerFactory(
    WinsockCachingProxyServer::MirrorResolveFn resolveMirrorIp, application::IConsoleManager& consoleManager,
    application::ICdnUrlService& cdnUrlService, std::shared_ptr<network::ILocalGameServer> localGameServer)
    : resolveMirrorIp_(std::move(resolveMirrorIp)),
      consoleManager_(consoleManager),
      cdnUrlService_(cdnUrlService),
      localGameServer_(std::move(localGameServer)) {}

std::unique_ptr<core::IDownloadAcceleratorServer> DownloadAcceleratorServerFactory::create() {
    return std::make_unique<WinsockCachingProxyServer>(resolveMirrorIp_, consoleManager_, cdnUrlService_,
                                                         localGameServer_);
}

}  // namespace gth::acceleratorserver
