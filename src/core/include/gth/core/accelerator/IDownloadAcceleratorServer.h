#pragma once

#include "gth/core/accelerator/AcceleratorCacheStats.h"
#include "gth/core/accelerator/AcceleratorServerConfig.h"

namespace gth::core {

// A LAN HTTP forward proxy meant to be pointed at by a console's network
// proxy setting. It also doubles as this app's console-traffic observer: every
// connection is recorded (see IConsoleManager::recordConnection) and every
// requested URL is logged (see ICdnUrlService::captureUrl) regardless of
// which of the paths below actually answers it.
//   - Mirror selection: for plain HTTP requests, the Host header is looked
//     up against the configured CdnMirror list and, if a faster candidate
//     IP is on file, the connection goes there instead of the address the
//     console's own DNS would have resolved - same URL, same path, same
//     response, just a closer/faster server answering it.
//   - HTTPS is tunnelled (CONNECT) end-to-end, unmodified - this proxy never
//     terminates TLS, so it cannot inspect or alter HTTPS payloads at all.
//     Console-connection recording and URL logging still happen for a
//     CONNECT request, but only down to host:port - the path is inside the
//     encrypted tunnel and never visible here, which also means neither the
//     local-serve nor the caching path below can ever apply to it.
//   - Local-serve: for a plain HTTP GET, the library is checked first (see
//     ILocalGameServer::tryServeGameFile) - a title recognized from the
//     request path is streamed straight from local storage and the real
//     origin is never contacted for that request.
//   - Response caching: when nothing local matched, every proxied HTTP
//     response is cached under a key derived from the exact request URL,
//     tagged with its own ETag/Last-Modified. The *only* time a cached body
//     is served is after an explicit revalidation request to the real origin
//     comes back 304 Not Modified - i.e. the origin itself confirms nothing
//     changed. There is no local-file substitution keyed by pattern-matching
//     a URL on this path; a cache entry can only ever hold bytes this proxy
//     itself received from the real server for that exact request.
class IDownloadAcceleratorServer {
public:
    virtual ~IDownloadAcceleratorServer() = default;

    virtual void start(const AcceleratorServerConfig& config) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual int port() const = 0;
    virtual AcceleratorCacheStats cacheStats() const = 0;
    virtual void clearCache() = 0;
};

}  // namespace gth::core
