#pragma once

#include "gth/core/enterprise/IRemoteNodeRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class RemoteNodeRepository : public core::IRemoteNodeRepository {
public:
    explicit RemoteNodeRepository(Database& db);

    std::vector<core::RemoteNode> getAll() override;
    std::optional<core::RemoteNode> getById(const std::string& nodeId) override;
    void upsert(const core::RemoteNode& node) override;

private:
    Database& db_;
};

}  // namespace gth::database
