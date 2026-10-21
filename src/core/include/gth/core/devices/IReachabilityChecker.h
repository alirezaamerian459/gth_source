#pragma once

#include <string>

namespace gth::core {

// A single, targeted TCP connectivity check to a device the user already
// registered - not a scan or a sweep of unknown addresses. Equivalent to
// what any client does before talking to a server; never touches
// authentication, never sends protocol-specific commands.
class IReachabilityChecker {
public:
    virtual ~IReachabilityChecker() = default;
    virtual bool isReachable(const std::string& ipAddress, int port, int timeoutMilliseconds) = 0;
};

}  // namespace gth::core
