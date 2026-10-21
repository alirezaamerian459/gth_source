#pragma once

#include <vector>

#include "gth/core/network/NetworkInterfaceInfo.h"

namespace gth::core {

class INetworkInterfaceDetector {
public:
    virtual ~INetworkInterfaceDetector() = default;
    virtual std::vector<NetworkInterfaceInfo> detect() = 0;
};

}  // namespace gth::core
