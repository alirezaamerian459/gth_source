#pragma once

#include "gth/core/enterprise/IRemoteReportSender.h"

namespace gth::network {

// Synchronous Winsock2 TCP client: connects, sends one report line, reads
// the "OK\n"/"ERROR..." acknowledgement, closes. Used only against a
// server address the user explicitly configured (see AgentService) -
// never auto-discovered.
class TcpRemoteReportSender : public core::IRemoteReportSender {
public:
    bool send(const std::string& serverAddress, int port, const std::string& nodeId, const std::string& nodeName,
               const std::string& summaryLine) override;
};

}  // namespace gth::network
