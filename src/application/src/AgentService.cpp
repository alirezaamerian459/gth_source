#include "gth/application/enterprise/AgentService.h"

#include <sstream>

namespace gth::application {

namespace {
std::string formatSummary(const core::reports::ArchiveHealthReport& report) {
    std::ostringstream out;
    out << "Storages=" << report.onlineStorages << "/" << report.totalStorages << " online"
        << ", Games=" << report.totalGames << ", Variants=" << report.totalVariants
        << ", Versions=" << report.totalVersions << ", Missing=" << report.missingVersions
        << ", UpdatesAvailable=" << report.updatesAvailable << ", TransfersActive=" << report.activeTransferJobs;
    return out.str();
}
}  // namespace

AgentService::AgentService(IReportService& reportService, core::IRemoteReportSender& sender, std::string localNodeId)
    : reportService_(reportService), sender_(sender), localNodeId_(std::move(localNodeId)) {}

AgentService::~AgentService() { AgentService::stop(); }

bool AgentService::sendOneReport() {
    const auto report = reportService_.generate();
    return sender_.send(serverAddress_, port_, localNodeId_, nodeName_, formatSummary(report));
}

bool AgentService::start(const std::string& serverAddress, int port, const std::string& nodeName,
                          int intervalSeconds) {
    stop();

    serverAddress_ = serverAddress;
    port_ = port;
    nodeName_ = nodeName;
    interval_ = std::chrono::seconds(intervalSeconds > 0 ? intervalSeconds : 60);

    const bool firstReportSucceeded = sendOneReport();

    running_.store(true);
    thread_ = std::jthread(&AgentService::run, this);
    return firstReportSucceeded;
}

void AgentService::stop() {
    if (!running_.exchange(false)) return;  // wasn't running
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void AgentService::run() {
    std::unique_lock lock(mutex_);
    while (running_.load()) {
        cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
        if (!running_.load()) break;
        sendOneReport();
    }
}

}  // namespace gth::application
