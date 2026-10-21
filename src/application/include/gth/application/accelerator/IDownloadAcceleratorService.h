#pragma once

#include <string>
#include <vector>

#include "gth/core/accelerator/AcceleratorCacheStats.h"
#include "gth/core/accelerator/CdnMirror.h"

namespace gth::application {

// Wraps core::ICdnMirrorRepository + core::IDownloadAcceleratorServer +
// core::IHostPinger + core::INetworkInterfaceDetector: the mirror list CRUD,
// an explicit "measure now" pass (see IHostPinger - never pinged inline on
// a request), and start/stop/status for the LAN caching proxy, mirroring
// IFtpServerService's shape.
class IDownloadAcceleratorService {
public:
    virtual ~IDownloadAcceleratorService() = default;

    virtual std::vector<core::CdnMirror> getAllMirrors() = 0;
    virtual void addMirror(const std::string& hostPattern, const std::string& ip, const std::string& label) = 0;
    virtual void removeMirror(const std::string& mirrorId) = 0;
    // Pings every configured mirror's IP and persists the round-trip time -
    // the only thing that ever updates CdnMirror::lastRttMs.
    virtual void measureMirrors() = 0;

    virtual void start(const std::string& cacheDir, int port, bool cacheEnabled) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual int port() const = 0;
    virtual std::vector<std::string> listenAddresses() = 0;
    virtual core::AcceleratorCacheStats cacheStats() const = 0;
    virtual void clearCache() = 0;
};

}  // namespace gth::application
