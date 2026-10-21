#pragma once

#include <string>
#include <vector>

#include "gth/core/health/Disk.h"
#include "gth/core/health/NasDevice.h"

namespace gth::core::health {

// The spec's Provider-based architecture (Section 5): a Windows PC can't
// always get real SMART data from a NAS's disks over SMB, so every source
// of disk info - a vendor API, SSH, SNMP, or (for now) a simulation -
// implements this the same way IUpdateSource abstracts update sources
// elsewhere in this app. A real vendor/SSH/SNMP provider is explicitly
// out of scope until this interface and its Simulation implementation are
// in place (spec Section 70) - only SimulationNasProvider implements it
// so far.
class INasProvider {
public:
    virtual ~INasProvider() = default;

    virtual std::string name() const = 0;

    // Returns whatever disks the provider can see for `nas`, with
    // healthPercent/temperatureCelsius left as nullopt for any field it
    // couldn't actually determine - never a guessed or defaulted value
    // (spec Section 71: "Unknown must never become 0%").
    virtual std::vector<Disk> getDisks(const NasDevice& nas) = 0;
};

}  // namespace gth::core::health
