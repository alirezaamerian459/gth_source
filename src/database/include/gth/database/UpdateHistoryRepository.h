#pragma once

#include "gth/core/updates/IUpdateHistoryRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class UpdateHistoryRepository : public core::IUpdateHistoryRepository {
public:
    explicit UpdateHistoryRepository(Database& db);

    std::vector<core::UpdateHistory> getByVariantId(const std::string& gameVariantId) override;
    void add(const core::UpdateHistory& history) override;

private:
    Database& db_;
};

}  // namespace gth::database
