#pragma once

#include <string>
#include <vector>

#include "gth/core/devices/Console.h"

namespace gth::application {

class IConsoleManager {
public:
    virtual ~IConsoleManager() = default;

    virtual std::vector<core::Console> getAllConsoles() = 0;
    virtual core::Console addConsole(const std::string& name, const std::string& platform,
                                      const std::string& ipAddress, int port, core::ConsoleProtocol protocol) = 0;
    virtual void removeConsole(const std::string& consoleId) = 0;

    // Called whenever a device's traffic is observed on a connection path
    // this app monitors (currently only the HTTP MITM Proxy). Upserts by
    // ipAddress: a first sighting creates a new, auto-detected Console row;
    // a repeat sighting just marks the existing one Online and refreshes
    // lastConnected, so the Consoles tab accumulates into a connection
    // history without duplicating a row per request. platformHint is
    // best-effort (e.g. a captured User-Agent) and only fills in `platform`
    // when nothing's there yet - never overwrites a name/platform the user
    // (or an earlier, more specific sighting) already set.
    virtual void recordConnection(const std::string& ipAddress, const std::string& platformHint, int port,
                                   core::ConsoleProtocol protocol) = 0;

    // Updates and persists the console's Status by attempting a single TCP
    // connect to its registered address/port (see IReachabilityChecker).
    virtual core::ConsoleStatus checkReachability(const std::string& consoleId) = 0;
};

}  // namespace gth::application
