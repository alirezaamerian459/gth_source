#include "gth/application/updates/UpdateSchedulerService.h"

namespace gth::application {

UpdateSchedulerService::UpdateSchedulerService(IUpdateDetector& detector, IUpdateJobService& jobService,
                                                std::chrono::milliseconds interval)
    : detector_(detector), jobService_(jobService), interval_(interval) {}

UpdateSchedulerService::~UpdateSchedulerService() { stop(); }

void UpdateSchedulerService::checkNow() {
    for (const auto& info : detector_.detectForAllVariants()) {
        if (info.status == core::UpdateStatus::UpdateAvailable) {
            jobService_.queueUpdate(info.gameVariantId, info.currentVersion, info.latestVersion);
        }
    }
}

void UpdateSchedulerService::start() {
    if (running_.exchange(true)) return;  // already running
    thread_ = std::jthread(&UpdateSchedulerService::run, this);
}

void UpdateSchedulerService::stop() {
    if (!running_.exchange(false)) return;  // wasn't running
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void UpdateSchedulerService::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_.load()) {
        cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
        if (!running_.load()) break;
        checkNow();
    }
}

}  // namespace gth::application
