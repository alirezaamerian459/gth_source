#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "gth/application/updates/IUpdateDetector.h"
#include "gth/application/updates/IUpdateJobService.h"
#include "gth/application/updates/IUpdateScheduler.h"

namespace gth::application {

// Runs checkNow() on its own thread every `interval`, sleeping in a way
// that wakes up immediately on stop() instead of finishing out a
// (potentially very long, e.g. 6-hour) wait. Off by default - nothing
// calls start() automatically; that's a deliberate user opt-in (the
// specification's "Automatic Updates: OFF/ON" setting), not a background
// activity the app starts without being asked.
class UpdateSchedulerService : public IUpdateScheduler {
public:
    UpdateSchedulerService(IUpdateDetector& detector, IUpdateJobService& jobService,
                            std::chrono::milliseconds interval = std::chrono::hours(6));
    ~UpdateSchedulerService() override;

    UpdateSchedulerService(const UpdateSchedulerService&) = delete;
    UpdateSchedulerService& operator=(const UpdateSchedulerService&) = delete;

    void checkNow() override;
    void start() override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

private:
    IUpdateDetector& detector_;
    IUpdateJobService& jobService_;
    std::chrono::milliseconds interval_;

    std::jthread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};

    void run();
};

}  // namespace gth::application
