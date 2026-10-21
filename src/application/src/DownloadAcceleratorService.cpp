#include "gth/application/accelerator/DownloadAcceleratorService.h"

#include <algorithm>

namespace gth::application {

using core::CdnMirror;

namespace {
// hostPattern "*.example.com" matches any host ending in ".example.com";
// anything else must match the host exactly - deliberately narrower than a
// general glob so a mistyped pattern fails closed (falls through to real
// DNS) rather than matching more than intended.
bool hostMatches(const std::string& hostPattern, const std::string& host) {
    if (hostPattern.size() > 1 && hostPattern[0] == '*' && hostPattern[1] == '.') {
        const std::string suffix = hostPattern.substr(1);  // ".example.com"
        return host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    return hostPattern == host;
}
}  // namespace

DownloadAcceleratorService::DownloadAcceleratorService(core::ICdnMirrorRepository& mirrorRepository,
                                                         core::IDownloadAcceleratorServer& server,
                                                         core::IHostPinger& pinger,
                                                         core::INetworkInterfaceDetector& networkInterfaceDetector)
    : mirrorRepository_(mirrorRepository),
      server_(server),
      pinger_(pinger),
      networkInterfaceDetector_(networkInterfaceDetector) {
    refreshSnapshot();
}

void DownloadAcceleratorService::refreshSnapshot() {
    auto all = mirrorRepository_.getAll();
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = std::move(all);
}

std::vector<CdnMirror> DownloadAcceleratorService::getAllMirrors() { return mirrorRepository_.getAll(); }

void DownloadAcceleratorService::addMirror(const std::string& hostPattern, const std::string& ip,
                                            const std::string& label) {
    CdnMirror mirror;
    mirror.hostPattern = hostPattern;
    mirror.ip = ip;
    mirror.label = label;
    mirrorRepository_.add(mirror);
    refreshSnapshot();
}

void DownloadAcceleratorService::removeMirror(const std::string& mirrorId) {
    mirrorRepository_.remove(mirrorId);
    refreshSnapshot();
}

void DownloadAcceleratorService::measureMirrors() {
    for (const auto& mirror : mirrorRepository_.getAll()) {
        mirrorRepository_.updateRtt(mirror.mirrorId, pinger_.pingMs(mirror.ip));
    }
    refreshSnapshot();
}

void DownloadAcceleratorService::start(const std::string& cacheDir, int port, bool cacheEnabled) {
    core::AcceleratorServerConfig config;
    config.cacheDir = cacheDir;
    config.port = port;
    config.cacheEnabled = cacheEnabled;
    server_.start(config);
}

void DownloadAcceleratorService::stop() { server_.stop(); }

bool DownloadAcceleratorService::isRunning() const { return server_.isRunning(); }

int DownloadAcceleratorService::port() const { return server_.port(); }

std::vector<std::string> DownloadAcceleratorService::listenAddresses() {
    std::vector<std::string> addresses;
    for (const auto& iface : networkInterfaceDetector_.detect()) {
        if (!iface.ipv4.empty() && iface.status == core::NetworkInterfaceStatus::Up) addresses.push_back(iface.ipv4);
    }
    return addresses;
}

core::AcceleratorCacheStats DownloadAcceleratorService::cacheStats() const { return server_.cacheStats(); }

void DownloadAcceleratorService::clearCache() { server_.clearCache(); }

std::optional<std::string> DownloadAcceleratorService::resolveMirrorIp(const std::string& host) {
    std::vector<CdnMirror> mirrors;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        mirrors = snapshot_;
    }

    const CdnMirror* best = nullptr;
    for (const auto& mirror : mirrors) {
        if (!hostMatches(mirror.hostPattern, host)) continue;
        if (!best) {
            best = &mirror;
            continue;
        }
        // Prefer a measured, faster candidate; an unmeasured one only wins
        // if nothing measured matches at all.
        if (mirror.lastRttMs && (!best->lastRttMs || *mirror.lastRttMs < *best->lastRttMs)) best = &mirror;
    }

    if (!best) return std::nullopt;
    return best->ip;
}

}  // namespace gth::application
