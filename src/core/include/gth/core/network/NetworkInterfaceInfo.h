#pragma once

#include <string>

namespace gth::core {

enum class NetworkInterfaceType {
    Unknown = 0,
    Ethernet = 1,
    WiFi = 2,
    VirtualAdapter = 3,
    Other = 4
};

enum class NetworkInterfaceStatus {
    Unknown = 0,
    Up = 1,
    Down = 2
};

std::string toString(NetworkInterfaceType type);
std::string toString(NetworkInterfaceStatus status);

// A live snapshot of one network adapter - not persisted (interfaces come
// and go, and nothing yet needs history of them; see ARCHITECTURE.md's note
// on not designing storage for hypothetical future requirements). A future
// milestone that needs to remember "which interface last saw this console"
// can add a table then.
struct NetworkInterfaceInfo {
    std::string name;
    NetworkInterfaceType type = NetworkInterfaceType::Unknown;
    std::string ipv4;
    std::string ipv6;
    std::string subnetMask;
    std::string gateway;
    std::string macAddress;
    long long speedBitsPerSecond = 0;
    NetworkInterfaceStatus status = NetworkInterfaceStatus::Unknown;
};

}  // namespace gth::core
