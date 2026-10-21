#pragma once

#include <mutex>

#include "gth/application/devices/IConsoleManager.h"
#include "gth/core/devices/IConsoleRepository.h"
#include "gth/core/devices/IReachabilityChecker.h"

namespace gth::application {

class ConsoleManagerService : public IConsoleManager {
public:
    ConsoleManagerService(core::IConsoleRepository& repository, core::IReachabilityChecker& reachabilityChecker,
                           int reachabilityTimeoutMs = 1500);

    std::vector<core::Console> getAllConsoles() override;
    core::Console addConsole(const std::string& name, const std::string& platform, const std::string& ipAddress,
                              int port, core::ConsoleProtocol protocol) override;
    void removeConsole(const std::string& consoleId) override;
    core::ConsoleStatus checkReachability(const std::string& consoleId) override;
    void recordConnection(const std::string& ipAddress, const std::string& platformHint, int port,
                           core::ConsoleProtocol protocol) override;

private:
    core::IConsoleRepository& repository_;
    core::IReachabilityChecker& reachabilityChecker_;
    int reachabilityTimeoutMs_;
    // recordConnection() is the hot path: every MITM proxy connection thread
    // calls it, and a console can hold many connections open at once (same
    // race CdnUrlService::captureUrl() guards against - two threads' "does
    // this IP already have a row" getByIpAddress() lookups could otherwise
    // both miss each other's in-flight insert and create duplicate rows for
    // the same device).
    std::mutex mutex_;
};

}  // namespace gth::application
