#pragma once

#include "gth/core/accelerator/IHostPinger.h"

namespace gth::network {

// One-shot ICMP echo via IcmpSendEcho - no raw socket/admin privilege
// needed, unlike a hand-rolled ICMP implementation.
class IcmpHostPinger : public core::IHostPinger {
public:
    std::optional<int> pingMs(const std::string& ip) override;
};

}  // namespace gth::network
