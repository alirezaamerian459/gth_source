#include "gth/core/network/NetworkInterfaceInfo.h"

namespace gth::core {

std::string toString(NetworkInterfaceType type) {
    switch (type) {
        case NetworkInterfaceType::Ethernet: return "Ethernet";
        case NetworkInterfaceType::WiFi: return "Wi-Fi";
        case NetworkInterfaceType::VirtualAdapter: return "Virtual Adapter";
        case NetworkInterfaceType::Other: return "Other";
        case NetworkInterfaceType::Unknown:
        default: return "Unknown";
    }
}

std::string toString(NetworkInterfaceStatus status) {
    switch (status) {
        case NetworkInterfaceStatus::Up: return "Up";
        case NetworkInterfaceStatus::Down: return "Down";
        case NetworkInterfaceStatus::Unknown:
        default: return "Unknown";
    }
}

}  // namespace gth::core
