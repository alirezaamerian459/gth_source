#include "gth/application/enterprise/CentralServerService.h"

#include "gth/core/Clock.h"

namespace gth::application {

void CentralServerService::start(int port) {
    listener_.start(port, [this](const std::string& nodeId, const std::string& name, const std::string& summary,
                                  const std::string& peerAddress) {
        core::RemoteNode node;
        if (auto existing = repository_.getById(nodeId); existing.has_value()) {
            node = *existing;
        } else {
            node.nodeId = nodeId;
        }

        node.name = name;
        node.address = peerAddress;
        node.lastSeenAt = core::Clock::utcNowIso8601();
        node.lastReportSummary = summary;
        node.updatedAt = core::Clock::utcNowIso8601();

        repository_.upsert(node);
    });
}

}  // namespace gth::application
