#pragma once

#include <optional>
#include <string>

namespace gth::core {

class IHostPinger {
public:
    virtual ~IHostPinger() = default;

    // Round-trip time in milliseconds to `ip`, or nullopt if it didn't
    // answer within the implementation's own timeout.
    virtual std::optional<int> pingMs(const std::string& ip) = 0;
};

}  // namespace gth::core
