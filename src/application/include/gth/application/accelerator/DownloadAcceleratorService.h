#pragma once

#include <mutex>
#include <optional>

#include "gth/application/accelerator/IDownloadAcceleratorService.h"
#include "gth/core/accelerator/ICdnMirrorRepository.h"
#include "gth/core/accelerator/IDownloadAcceleratorServer.h"
#include "gth/core/accelerator/IHostPinger.h"
#include "gth/core/network/INetworkInterfaceDetector.h"

namespace gth::application {

class DownloadAcceleratorService : public IDownloadAcceleratorService {
public:
    DownloadAcceleratorService(core::ICdnMirrorRepository& mirrorRepository, core::IDownloadAcceleratorServer& server,
                                core::IHostPinger& pinger, core::INetworkInterfaceDetector& networkInterfaceDetector);

    std::vector<core::CdnMirror> getAllMirrors() override;
    void addMirror(const std::string& hostPattern, const std::string& ip, const std::string& label) override;
    void removeMirror(const std::string& mirrorId) override;
    void measureMirrors() override;

    void start(const std::string& cacheDir, int port, bool cacheEnabled) override;
    void stop() override;
    bool isRunning() const override;
    int port() const override;
    std::vector<std::string> listenAddresses() override;
    core::AcceleratorCacheStats cacheStats() const override;
    void clearCache() override;

    // Bound into the proxy server at construction (see ServiceContainer) -
    // exposed publicly rather than as a free function so it can read the
    // in-memory snapshot below instead of hitting SQLite from a proxy
    // worker thread on every single request.
    std::optional<std::string> resolveMirrorIp(const std::string& host);

private:
    void refreshSnapshot();

    core::ICdnMirrorRepository& mirrorRepository_;
    core::IDownloadAcceleratorServer& server_;
    core::IHostPinger& pinger_;
    core::INetworkInterfaceDetector& networkInterfaceDetector_;

    mutable std::mutex snapshotMutex_;
    std::vector<core::CdnMirror> snapshot_;
};

}  // namespace gth::application
