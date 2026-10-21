#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/health/NasDevice.h"

namespace gth::core::health {

class INasDeviceRepository {
public:
    virtual ~INasDeviceRepository() = default;

    virtual std::vector<NasDevice> getAll() = 0;
    virtual std::optional<NasDevice> getById(const std::string& nasDeviceId) = 0;

    virtual void add(const NasDevice& device) = 0;
    virtual void update(const NasDevice& device) = 0;
    virtual void remove(const std::string& nasDeviceId) = 0;
};

}  // namespace gth::core::health
