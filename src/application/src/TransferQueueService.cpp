#include "gth/application/transfer/TransferQueueService.h"

#include <chrono>
#include <filesystem>

#include "gth/core/Clock.h"

namespace gth::application {

namespace fs = std::filesystem;
using core::TransferItem;
using core::TransferJob;
using core::TransferStatus;

namespace {
bool isTerminal(TransferStatus status) {
    return status == TransferStatus::Completed || status == TransferStatus::Failed ||
           status == TransferStatus::Cancelled;
}
}  // namespace

TransferQueueService::TransferQueueService(core::ITransferJobRepository& jobRepository,
                                            core::ITransferItemRepository& itemRepository,
                                            core::ITransferProvider& provider, int maxConcurrent)
    : jobRepository_(jobRepository), itemRepository_(itemRepository), provider_(provider),
      maxConcurrent_(maxConcurrent) {
    recoverIncompleteItemsFromPreviousRun();
}

TransferQueueService::~TransferQueueService() { stop(); }

// Anything still Queued/Preparing/Transferring/Paused when the process last
// ran didn't finish - CopyFileEx doesn't resume a partial copy, so
// Preparing/Transferring items are reset to Queued (starting over is the
// only safe option) and re-enqueued alongside anything already Queued.
// Verifying items are treated the same way pending a real verification step.
void TransferQueueService::recoverIncompleteItemsFromPreviousRun() {
    for (const auto& job : jobRepository_.getAll()) {
        for (auto item : itemRepository_.getByJobId(job.transferJobId)) {
            if (isTerminal(item.status)) continue;

            if (item.status != TransferStatus::Queued) {
                item.status = TransferStatus::Queued;
                item.bytesTransferred = 0;
                item.updatedAt = core::Clock::utcNowIso8601();
                itemRepository_.update(item);
            }

            pendingItemIds_.push_back(item.transferItemId);
        }
    }
}

TransferJob TransferQueueService::enqueueJob(const std::string& name,
                                              const std::vector<std::pair<std::string, std::string>>& sourceDestPairs) {
    TransferJob job;
    job.name = name;
    job.status = TransferStatus::Queued;
    jobRepository_.add(job);

    std::vector<std::string> newItemIds;
    for (const auto& [source, destination] : sourceDestPairs) {
        TransferItem item;
        item.transferJobId = job.transferJobId;
        item.sourcePath = source;
        item.destinationPath = destination;

        std::error_code ec;
        if (fs::exists(source, ec) && fs::is_regular_file(source, ec)) {
            item.sizeBytes = static_cast<long long>(fs::file_size(source, ec));
        }

        itemRepository_.add(item);
        newItemIds.push_back(item.transferItemId);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto& id : newItemIds) pendingItemIds_.push_back(std::move(id));
    }
    queueCv_.notify_all();

    return job;
}

std::vector<TransferJob> TransferQueueService::getAllJobs() { return jobRepository_.getAll(); }

std::vector<TransferItem> TransferQueueService::getItemsForJob(const std::string& transferJobId) {
    return itemRepository_.getByJobId(transferJobId);
}

void TransferQueueService::cancelItem(const std::string& transferItemId) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    auto it = cancelFlags_.find(transferItemId);
    if (it != cancelFlags_.end()) it->second->store(true);
}

void TransferQueueService::start() {
    if (running_.exchange(true)) return;
    for (int i = 0; i < maxConcurrent_; ++i) workers_.emplace_back(&TransferQueueService::workerLoop, this);
}

void TransferQueueService::stop() {
    if (!running_.exchange(false)) return;
    queueCv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

void TransferQueueService::workerLoop() {
    while (running_.load()) {
        std::string itemId;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return !running_.load() || !pendingItemIds_.empty(); });
            if (!running_.load()) return;
            if (pendingItemIds_.empty()) continue;
            itemId = pendingItemIds_.front();
            pendingItemIds_.pop_front();
        }
        processItem(itemId);
    }
}

void TransferQueueService::processItem(const std::string& transferItemId) {
    auto itemOpt = itemRepository_.getById(transferItemId);
    if (!itemOpt) return;
    TransferItem item = *itemOpt;

    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        cancelFlags_[transferItemId] = cancelFlag;
    }

    item.status = TransferStatus::Preparing;
    item.updatedAt = core::Clock::utcNowIso8601();
    itemRepository_.update(item);

    item.status = TransferStatus::Transferring;
    item.updatedAt = core::Clock::utcNowIso8601();
    itemRepository_.update(item);

    // CopyFileEx's progress callback fires far more often than the database
    // needs updates for (every internal buffer flush) - throttle writes to
    // a few times a second, but never drop the final 100% update.
    auto lastUpdate = std::chrono::steady_clock::now();
    const auto outcome = provider_.copyFile(
        item.sourcePath, item.destinationPath,
        [&](long long transferred, long long total) {
            const auto now = std::chrono::steady_clock::now();
            const bool isFinal = total > 0 && transferred >= total;
            if (!isFinal && now - lastUpdate < std::chrono::milliseconds(250)) return;
            lastUpdate = now;

            item.bytesTransferred = transferred;
            if (total > 0) item.sizeBytes = total;
            item.updatedAt = core::Clock::utcNowIso8601();
            itemRepository_.update(item);
        },
        cancelFlag.get());

    if (outcome.success) {
        item.status = TransferStatus::Completed;
        item.bytesTransferred = item.sizeBytes;
    } else if (outcome.errorMessage == "Cancelled") {
        item.status = TransferStatus::Cancelled;
    } else {
        item.status = TransferStatus::Failed;
        item.errorMessage = outcome.errorMessage;
    }
    item.updatedAt = core::Clock::utcNowIso8601();
    itemRepository_.update(item);

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        cancelFlags_.erase(transferItemId);
    }

    maybeCompleteJob(item.transferJobId);
}

void TransferQueueService::maybeCompleteJob(const std::string& transferJobId) {
    const auto items = itemRepository_.getByJobId(transferJobId);

    bool anyPending = false;
    bool anyFailed = false;
    bool anyCancelled = false;
    for (const auto& item : items) {
        if (!isTerminal(item.status)) {
            anyPending = true;
        } else if (item.status == TransferStatus::Failed) {
            anyFailed = true;
        } else if (item.status == TransferStatus::Cancelled) {
            anyCancelled = true;
        }
    }
    if (anyPending) return;

    auto jobOpt = jobRepository_.getById(transferJobId);
    if (!jobOpt) return;

    TransferJob job = *jobOpt;
    job.status = anyFailed ? TransferStatus::Failed : (anyCancelled ? TransferStatus::Cancelled : TransferStatus::Completed);
    job.completedAt = core::Clock::utcNowIso8601();
    job.updatedAt = *job.completedAt;
    jobRepository_.update(job);
}

}  // namespace gth::application
