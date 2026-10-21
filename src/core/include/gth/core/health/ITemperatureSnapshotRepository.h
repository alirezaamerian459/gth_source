#pragma once

#include <string>
#include <vector>

#include "gth/core/health/TemperatureSnapshot.h"

namespace gth::core::health {

class ITemperatureSnapshotRepository {
public:
    virtual ~ITemperatureSnapshotRepository() = default;

    virtual void add(const TemperatureSnapshot& snapshot) = 0;
    virtual std::vector<TemperatureSnapshot> getByDiskId(const std::string& diskId) = 0;
};

}  // namespace gth::core::health
