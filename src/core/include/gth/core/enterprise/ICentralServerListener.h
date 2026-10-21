#pragma once

#include <functional>
#include <string>

namespace gth::core {

// Abstraction over "accept incoming agent reports on a TCP port" - kept in
// Core so the Application layer (CentralServerService) can depend on it
// without depending on the concrete Winsock2 implementation
// (gth::network::CentralServerListener), the same shape as
// IReachabilityChecker/ITransferProvider.
class ICentralServerListener {
public:
    // (nodeId, name, summaryLine, peerAddress) - called on a background
    // thread every time a report arrives. Implementations must be safe to
    // call from that thread (repository access is already safe - see
    // TransferQueueService's worker pool using the same Database).
    using ReportHandler = std::function<void(const std::string&, const std::string&, const std::string&,
                                              const std::string&)>;

    virtual ~ICentralServerListener() = default;

    virtual void start(int port, ReportHandler onReport) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

}  // namespace gth::core
