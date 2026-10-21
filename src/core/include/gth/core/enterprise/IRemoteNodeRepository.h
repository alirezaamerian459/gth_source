#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/enterprise/RemoteNode.h"

namespace gth::core {

class IRemoteNodeRepository {
public:
    virtual ~IRemoteNodeRepository() = default;

    virtual std::vector<RemoteNode> getAll() = 0;
    virtual std::optional<RemoteNode> getById(const std::string& nodeId) = 0;

    // Insert-or-replace keyed on nodeId - a node reporting in again simply
    // overwrites its previous row (LastSeenAt/LastReportSummary), the same
    // "snapshot, not history" shape as UpdateInfo.
    virtual void upsert(const RemoteNode& node) = 0;
};

}  // namespace gth::core
