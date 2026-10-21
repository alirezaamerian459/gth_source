#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/health/Disk.h"

namespace gth::core::health {

class IDiskRepository {
public:
    virtual ~IDiskRepository() = default;

    virtual std::vector<Disk> getByNasDeviceId(const std::string& nasDeviceId) = 0;
    virtual std::vector<Disk> getAll() = 0;
    virtual std::optional<Disk> getById(const std::string& diskId) = 0;
    // Natural key for reconciling a provider's disk list against existing
    // rows without a real Title/Content-Id-style identifier - a serial
    // number is the closest thing a disk has to one.
    virtual std::optional<Disk> getByNasDeviceAndSerial(const std::string& nasDeviceId,
                                                          const std::string& serialNumber) = 0;

    virtual void add(const Disk& disk) = 0;
    virtual void update(const Disk& disk) = 0;
};

}  // namespace gth::core::health
