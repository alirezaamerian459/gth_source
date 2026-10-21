#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "gth/application/enterprise/IAgentService.h"
#include "gth/application/reports/IReportService.h"
#include "gth/core/enterprise/IRemoteReportSender.h"

namespace gth::application {

class AgentService : public IAgentService {
public:
    AgentService(IReportService& reportService, core::IRemoteReportSender& sender, std::string localNodeId);
    ~AgentService() override;

    AgentService(const AgentService&) = delete;
    AgentService& operator=(const AgentService&) = delete;

    bool start(const std::string& serverAddress, int port, const std::string& nodeName, int intervalSeconds) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }
    const std::string& localNodeId() const override { return localNodeId_; }

private:
    bool sendOneReport();
    void run();

    IReportService& reportService_;
    core::IRemoteReportSender& sender_;
    std::string localNodeId_;

    std::string serverAddress_;
    int port_ = 0;
    std::string nodeName_;
    std::chrono::seconds interval_{60};

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
};

}  // namespace gth::application
