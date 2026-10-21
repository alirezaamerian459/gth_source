#pragma once

#include "gth/core/network/INetworkInterfaceDetector.h"

namespace gth::network {

// Enumerates network adapters via the IP Helper API (GetAdaptersAddresses) -
// Ethernet, Wi-Fi, USB Ethernet (which reports as Ethernet at this API
// level), and virtual adapters. Loopback is excluded.
class Win32NetworkInterfaceDetector : public core::INetworkInterfaceDetector {
public:
    std::vector<core::NetworkInterfaceInfo> detect() override;
};

}  // namespace gth::network
