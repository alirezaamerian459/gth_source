#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "gth/core/enterprise/ICentralServerListener.h"

namespace gth::network {

// Winsock2 TCP server: a background accept loop on `port`, one short-lived
// worker thread per connection (control traffic only - a handful of PCs/NAS
// reporting in periodically, not a high-throughput service). Each
// connection sends exactly one line ("<nodeId>\t<name>\t<summary>\n"),
// gets "OK\n" back, and closes. Off by default - only runs once
// CentralServerService::start() is called, the same explicit-opt-in shape
// as UpdateSchedulerService/TransferQueueService.
class CentralServerListener : public core::ICentralServerListener {
public:
    ~CentralServerListener() override;

    void start(int port, ReportHandler onReport) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

private:
    void acceptLoop(int port);
    void handleConnection(std::uintptr_t clientSocket);

    ReportHandler onReport_;
    std::jthread acceptThread_;
    std::atomic<bool> running_{false};
};

}  // namespace gth::network
