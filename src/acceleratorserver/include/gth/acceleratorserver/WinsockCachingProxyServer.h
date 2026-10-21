#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "gth/application/devices/IConsoleManager.h"
#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/core/accelerator/IDownloadAcceleratorServer.h"
#include "gth/network/ILocalGameServer.h"

namespace gth::acceleratorserver {

// Winsock2 LAN forward-proxy + honest response cache (see
// IDownloadAcceleratorServer for exactly what "honest" means here). Mirrors
// WinsockFtpServer's accept-loop/select/thread-per-connection shape.
//
// resolveMirrorIp is called for every request's Host - implementations
// typically look a configured CdnMirror list up by host and return the
// fastest known candidate's IP; returning nullopt means "let this proxy's
// own DNS resolve it normally", i.e. no redirection at all.
//
// consoleManager/cdnUrlService are this proxy's only way of surfacing what's
// connecting/what it's asking for to the rest of the app (Consoles tab, CDN
// URL Log) - previously the separate job of a now-retired HTTP MITM Proxy
// class, folded in here so a console only ever needs to be pointed at one
// listener to get both console-tracking and caching/mirrors/local-serve.
// localGameServer is optional (nullptr disables the local-serve path
// entirely, same as it always could) so a unit test or a minimal instance can
// still construct one without a real game library behind it.
class WinsockCachingProxyServer : public core::IDownloadAcceleratorServer {
public:
    using MirrorResolveFn = std::function<std::optional<std::string>(const std::string& host)>;

    WinsockCachingProxyServer(MirrorResolveFn resolveMirrorIp, application::IConsoleManager& consoleManager,
                               application::ICdnUrlService& cdnUrlService,
                               std::shared_ptr<network::ILocalGameServer> localGameServer = nullptr);
    ~WinsockCachingProxyServer() override;

    void start(const core::AcceleratorServerConfig& config) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }
    int port() const override { return port_.load(); }
    core::AcceleratorCacheStats cacheStats() const override;
    void clearCache() override;

private:
    void acceptLoop(core::AcceleratorServerConfig config);
    void handleClient(std::uintptr_t clientSocket, std::string clientIp, std::string cacheDir, bool cacheEnabled);

    MirrorResolveFn resolveMirrorIp_;
    application::IConsoleManager& consoleManager_;
    application::ICdnUrlService& cdnUrlService_;
    std::shared_ptr<network::ILocalGameServer> localGameServer_;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
    std::atomic<int> port_{0};
    std::atomic<std::uint64_t> hitCount_{0};
    std::atomic<std::uint64_t> missCount_{0};
    mutable std::mutex cacheDirMutex_;
    std::string cacheDir_;
};

}  // namespace gth::acceleratorserver
