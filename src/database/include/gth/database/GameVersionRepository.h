#pragma once

#include "gth/core/index/IGameVersionRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class GameVersionRepository : public core::IGameVersionRepository {
public:
    explicit GameVersionRepository(Database& db);

    std::vector<core::GameVersion> getByVariantId(const std::string& gameVariantId) override;
    std::optional<core::GameVersion> getById(const std::string& gameVersionId) override;
    std::optional<core::GameVersion> getByVariantAndPath(const std::string& gameVariantId,
                                                           const std::string& relativePath) override;

    void add(const core::GameVersion& version) override;
    void update(const core::GameVersion& version) override;

private:
    Database& db_;
};

}  // namespace gth::database
