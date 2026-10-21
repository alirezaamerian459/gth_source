#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

enum class ConsoleProtocol {
    Unknown = 0,
    Ftp = 1,
    Smb = 2,
    Custom = 3,
    Http = 4  // Detected via the HTTP MITM Proxy (see IConsoleManager::recordConnection).
};

enum class ConsoleStatus {
    Unknown = 0,
    Online = 1,
    Offline = 2
};

std::string toString(ConsoleProtocol protocol);
ConsoleProtocol consoleProtocolFromString(std::string_view text);
std::string toString(ConsoleStatus status);
ConsoleStatus consoleStatusFromString(std::string_view text);

// A console registered by the user as a transfer source/destination -
// manually entered (Advanced Mode's IP/Port/Protocol per the spec), never
// probed or connected to except when the user explicitly asks to check
// reachability or transfer files. Registering a console here never installs
// anything on it or touches its authentication.
//
// Rows can also appear here without the user ever touching "Add Console":
// IConsoleManager::recordConnection() creates/updates one automatically
// whenever a device's traffic is seen going through the HTTP MITM Proxy,
// making this list double as a connection archive/history, not just a
// manually-curated one. `autoDetected` distinguishes the two so the UI can
// label them differently.
struct Console {
    std::string consoleId = Guid::newGuid();
    std::string name;
    std::string platform;  // free-form: "PS5", "Xbox Series X", "Switch", ...
    std::string ipAddress;
    int port = 0;
    ConsoleProtocol protocol = ConsoleProtocol::Unknown;
    ConsoleStatus status = ConsoleStatus::Unknown;
    std::optional<std::string> lastConnected;
    bool autoDetected = false;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
