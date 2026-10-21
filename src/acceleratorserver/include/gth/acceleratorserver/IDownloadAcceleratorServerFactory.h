#pragma once

#include <memory>

#include "gth/core/accelerator/IDownloadAcceleratorServer.h"

namespace gth::acceleratorserver {

// Creates independent IDownloadAcceleratorServer instances, each with its own
// socket/port - used to run several LAN Console Proxy listeners at once (one
// per console), on top of the original app-lifetime instance ServiceContainer
// already owns. Every instance created here shares the same underlying
// console/CDN/game-library wiring as the original - there is only one drive,
// one library and one mirror list on this PC regardless of how many consoles
// are connected; only the listening port differs per console.
class IDownloadAcceleratorServerFactory {
public:
    virtual ~IDownloadAcceleratorServerFactory() = default;

    virtual std::unique_ptr<core::IDownloadAcceleratorServer> create() = 0;
};

}  // namespace gth::acceleratorserver
