#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "gth/application/transfer/ITransferQueueService.h"
#include "gth/core/transfer/ITransferItemRepository.h"
#include "gth/core/transfer/ITransferJobRepository.h"
#include "gth/core/transfer/ITransferProvider.h"

namespace gth::application {

class TransferQueueService : public ITransferQueueService {
public:
    TransferQueueService(core::ITransferJobRepository& jobRepository, core::ITransferItemRepository& itemRepository,
                          core::ITransferProvider& provider, int maxConcurrent = 2);
    ~TransferQueueService() override;

    TransferQueueService(const TransferQueueService&) = delete;
    TransferQueueService& operator=(const TransferQueueService&) = delete;

    core::TransferJob enqueueJob(const std::string& name,
                                  const std::vector<std::pair<std::string, std::string>>& sourceDestPairs) override;
    std::vector<core::TransferJob> getAllJobs() override;
    std::vector<core::TransferItem> getItemsForJob(const std::string& transferJobId) override;
    void cancelItem(const std::string& transferItemId) override;

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    core::ITransferJobRepository& jobRepository_;
    core::ITransferItemRepository& itemRepository_;
    core::ITransferProvider& provider_;
    int maxConcurrent_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::string> pendingItemIds_;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> cancelFlags_;

    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    void recoverIncompleteItemsFromPreviousRun();
    void workerLoop();
    void processItem(const std::string& transferItemId);
    void maybeCompleteJob(const std::string& transferJobId);
};

}  // namespace gth::application
