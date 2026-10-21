#pragma once

#include "gth/core/health/IDiskRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class DiskRepository : public core::health::IDiskRepository {
public:
    explicit DiskRepository(Database& db);

    std::vector<core::health::Disk> getByNasDeviceId(const std::string& nasDeviceId) override;
    std::vector<core::health::Disk> getAll() override;
    std::optional<core::health::Disk> getById(const std::string& diskId) override;
    std::optional<core::health::Disk> getByNasDeviceAndSerial(const std::string& nasDeviceId,
                                                                const std::string& serialNumber) override;
    void add(const core::health::Disk& disk) override;
    void update(const core::health::Disk& disk) override;

private:
    Database& db_;
};

}  // namespace gth::database
