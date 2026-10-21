#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

// One candidate mirror IP for a given host. Several rows can share the same
// hostPattern (e.g. two regional mirrors for the same CDN hostname) - the
// resolver picks whichever has the lowest lastRttMs, refreshed by an
// explicit "measure" pass (see IHostPinger), never pinged inline on the hot
// request path.
struct CdnMirror {
    std::string mirrorId = Guid::newGuid();
    std::string hostPattern;
    std::string ip;
    std::string label;
    std::optional<int> lastRttMs;
    std::string createdAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
