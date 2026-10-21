#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/TransferItem.h"

namespace gth::core {

class ITransferItemRepository {
public:
    virtual ~ITransferItemRepository() = default;

    virtual std::vector<TransferItem> getByJobId(const std::string& transferJobId) = 0;
    virtual std::optional<TransferItem> getById(const std::string& transferItemId) = 0;

    // Every item currently Queued across every job, in creation order - what
    // the queue's worker pool pulls from next.
    virtual std::vector<TransferItem> getQueued() = 0;

    virtual void add(const TransferItem& item) = 0;
    virtual void update(const TransferItem& item) = 0;
};

}  // namespace gth::core
