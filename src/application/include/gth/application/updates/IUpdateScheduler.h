#pragma once

namespace gth::application {

// Background periodic update-check service: Check Schedule -> Detect ->
// Queue (Download/Verify/Replace are later milestones). Duplicate
// prevention is inherited from IUpdateJobService::queueUpdate, so running
// checkNow() twice in a row never creates two jobs for the same update.
class IUpdateScheduler {
public:
    virtual ~IUpdateScheduler() = default;

    // Runs one detect-and-queue pass synchronously. Safe to call directly
    // from the UI (a "Check for Updates now" button) whether or not the
    // background timer is running.
    virtual void checkNow() = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

}  // namespace gth::application
