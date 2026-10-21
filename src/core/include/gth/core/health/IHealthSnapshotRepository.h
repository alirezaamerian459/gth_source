#pragma once

#include <string>
#include <vector>

#include "gth/core/health/HealthSnapshot.h"

namespace gth::core::health {

class IHealthSnapshotRepository {
public:
    virtual ~IHealthSnapshotRepository() = default;

    virtual void add(const HealthSnapshot& snapshot) = 0;
    virtual std::vector<HealthSnapshot> getByDiskId(const std::string& diskId) = 0;
};

}  // namespace gth::core::health
