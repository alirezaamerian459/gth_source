#pragma once

#include "gth/core/health/INasDeviceRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class NasDeviceRepository : public core::health::INasDeviceRepository {
public:
    explicit NasDeviceRepository(Database& db);

    std::vector<core::health::NasDevice> getAll() override;
    std::optional<core::health::NasDevice> getById(const std::string& nasDeviceId) override;
    void add(const core::health::NasDevice& device) override;
    void update(const core::health::NasDevice& device) override;
    void remove(const std::string& nasDeviceId) override;

private:
    Database& db_;
};

}  // namespace gth::database
