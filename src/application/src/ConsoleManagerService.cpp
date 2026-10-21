#include "gth/application/devices/ConsoleManagerService.h"

#include "gth/core/Clock.h"

namespace gth::application {

using core::Console;
using core::ConsoleStatus;

ConsoleManagerService::ConsoleManagerService(core::IConsoleRepository& repository,
                                              core::IReachabilityChecker& reachabilityChecker,
                                              int reachabilityTimeoutMs)
    : repository_(repository), reachabilityChecker_(reachabilityChecker), reachabilityTimeoutMs_(reachabilityTimeoutMs) {}

std::vector<Console> ConsoleManagerService::getAllConsoles() { return repository_.getAll(); }

Console ConsoleManagerService::addConsole(const std::string& name, const std::string& platform,
                                           const std::string& ipAddress, int port, core::ConsoleProtocol protocol) {
    Console console;
    console.name = name;
    console.platform = platform;
    console.ipAddress = ipAddress;
    console.port = port;
    console.protocol = protocol;
    repository_.add(console);
    return console;
}

void ConsoleManagerService::removeConsole(const std::string& consoleId) { repository_.remove(consoleId); }

void ConsoleManagerService::recordConnection(const std::string& ipAddress, const std::string& platformHint,
                                              int port, core::ConsoleProtocol protocol) {
    if (ipAddress.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = core::Clock::utcNowIso8601();

    auto existing = repository_.getByIpAddress(ipAddress);
    if (existing) {
        Console console = *existing;
        console.status = ConsoleStatus::Online;
        console.lastConnected = now;
        console.updatedAt = now;
        if (port != 0) console.port = port;
        if (!platformHint.empty() && console.platform.empty()) console.platform = platformHint;
        repository_.update(console);
        return;
    }

    Console console;
    console.name = "Console (" + ipAddress + ")";
    console.platform = platformHint;
    console.ipAddress = ipAddress;
    console.port = port;
    console.protocol = protocol;
    console.status = ConsoleStatus::Online;
    console.lastConnected = now;
    console.autoDetected = true;
    repository_.add(console);
}

ConsoleStatus ConsoleManagerService::checkReachability(const std::string& consoleId) {
    auto consoleOpt = repository_.getById(consoleId);
    if (!consoleOpt) return ConsoleStatus::Unknown;

    Console console = *consoleOpt;
    const bool reachable = reachabilityChecker_.isReachable(console.ipAddress, console.port, reachabilityTimeoutMs_);

    console.status = reachable ? ConsoleStatus::Online : ConsoleStatus::Offline;
    if (reachable) console.lastConnected = core::Clock::utcNowIso8601();
    console.updatedAt = core::Clock::utcNowIso8601();
    repository_.update(console);

    return console.status;
}

}  // namespace gth::application
