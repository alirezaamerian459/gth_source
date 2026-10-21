#pragma once

#include "gth/core/devices/IReachabilityChecker.h"

namespace gth::network {

// Non-blocking TCP connect with a timeout, via Winsock. Used only against
// addresses/ports the user explicitly registered (see IReachabilityChecker).
class TcpReachabilityChecker : public core::IReachabilityChecker {
public:
    bool isReachable(const std::string& ipAddress, int port, int timeoutMilliseconds) override;
};

}  // namespace gth::network
