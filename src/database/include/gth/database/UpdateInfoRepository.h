#pragma once

#include "gth/core/updates/IUpdateInfoRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class UpdateInfoRepository : public core::IUpdateInfoRepository {
public:
    explicit UpdateInfoRepository(Database& db);

    std::vector<core::UpdateInfo> getAll() override;
    std::optional<core::UpdateInfo> getByVariantId(const std::string& gameVariantId) override;
    void upsert(const core::UpdateInfo& info) override;

private:
    Database& db_;
};

}  // namespace gth::database
