#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

// One other Game Transfer Hub installation known to this one, via the
// Central Server / Agent mechanism (Milestone 14). A row here is never a
// console or a game archive itself - just a peer instance that has reported
// in with a summary of its own archive. NodeId is generated once by the
// reporting agent and persisted locally there (see
// AgentService::localNodeId), so repeated reports from the same machine
// upsert the same row instead of accumulating duplicates.
struct RemoteNode {
    std::string nodeId = Guid::newGuid();
    std::string name;
    std::string address;  // peer IP as seen by the server (getpeername), not self-reported

    std::optional<std::string> lastSeenAt;
    std::optional<std::string> lastReportSummary;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
