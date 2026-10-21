#pragma once

#include "gth/core/index/IScanJobRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class ScanJobRepository : public core::IScanJobRepository {
public:
    explicit ScanJobRepository(Database& db);

    std::vector<core::ScanJob> getByStorageId(const std::string& storageId) override;
    std::optional<core::ScanJob> getById(const std::string& scanJobId) override;

    void add(const core::ScanJob& job) override;
    void update(const core::ScanJob& job) override;

private:
    Database& db_;
};

}  // namespace gth::database
