#pragma once

#include <string>
#include <utility>
#include <vector>

#include "gth/core/entities/TransferItem.h"
#include "gth/core/entities/TransferJob.h"

namespace gth::application {

// A queue of file-copy jobs processed by a worker pool. Unlike
// IUpdateScheduler (background checking the user didn't explicitly ask
// for, off by default), enqueueing a job here *is* the user's opt-in - the
// queue's workers start automatically so an enqueued transfer actually
// runs, the way any download/transfer manager behaves once you add
// something to its queue.
class ITransferQueueService {
public:
    virtual ~ITransferQueueService() = default;

    virtual core::TransferJob enqueueJob(const std::string& name,
                                          const std::vector<std::pair<std::string, std::string>>& sourceDestPairs) = 0;

    virtual std::vector<core::TransferJob> getAllJobs() = 0;
    virtual std::vector<core::TransferItem> getItemsForJob(const std::string& transferJobId) = 0;

    virtual void cancelItem(const std::string& transferItemId) = 0;
};

}  // namespace gth::application
