#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/TransferJob.h"

namespace gth::core {

class ITransferJobRepository {
public:
    virtual ~ITransferJobRepository() = default;

    virtual std::vector<TransferJob> getAll() = 0;
    virtual std::optional<TransferJob> getById(const std::string& transferJobId) = 0;

    virtual void add(const TransferJob& job) = 0;
    virtual void update(const TransferJob& job) = 0;
};

}  // namespace gth::core
