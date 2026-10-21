#pragma once

#include <string>

namespace gth::core {

// Abstraction over "send one report line to a Central Server and wait for
// its acknowledgement" - the Agent-side counterpart of
// ICentralServerListener. Kept in Core so AgentService (Application) never
// touches Winsock2 directly.
class IRemoteReportSender {
public:
    virtual ~IRemoteReportSender() = default;

    virtual bool send(const std::string& serverAddress, int port, const std::string& nodeId,
                       const std::string& nodeName, const std::string& summaryLine) = 0;
};

}  // namespace gth::core
