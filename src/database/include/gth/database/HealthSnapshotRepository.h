#pragma once

#include "gth/core/health/IHealthSnapshotRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class HealthSnapshotRepository : public core::health::IHealthSnapshotRepository {
public:
    explicit HealthSnapshotRepository(Database& db);

    void add(const core::health::HealthSnapshot& snapshot) override;
    std::vector<core::health::HealthSnapshot> getByDiskId(const std::string& diskId) override;

private:
    Database& db_;
};

}  // namespace gth::database
