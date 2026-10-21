#pragma once

#include "gth/core/health/ITemperatureSnapshotRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class TemperatureSnapshotRepository : public core::health::ITemperatureSnapshotRepository {
public:
    explicit TemperatureSnapshotRepository(Database& db);

    void add(const core::health::TemperatureSnapshot& snapshot) override;
    std::vector<core::health::TemperatureSnapshot> getByDiskId(const std::string& diskId) override;

private:
    Database& db_;
};

}  // namespace gth::database
