#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/ScanJob.h"

namespace gth::core {

class IScanJobRepository {
public:
    virtual ~IScanJobRepository() = default;

    virtual std::vector<ScanJob> getByStorageId(const std::string& storageId) = 0;
    virtual std::optional<ScanJob> getById(const std::string& scanJobId) = 0;

    virtual void add(const ScanJob& job) = 0;
    virtual void update(const ScanJob& job) = 0;
};

}  // namespace gth::core
