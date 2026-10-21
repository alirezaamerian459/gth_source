#pragma once

#include "gth/core/index/IGameVariantRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class GameVariantRepository : public core::IGameVariantRepository {
public:
    explicit GameVariantRepository(Database& db);

    std::vector<core::GameVariant> getAll() override;
    std::vector<core::GameVariant> getByGameId(const std::string& gameId) override;
    std::vector<core::GameVariant> getByStorageId(const std::string& storageId) override;
    std::optional<core::GameVariant> getById(const std::string& gameVariantId) override;
    std::optional<core::GameVariant> getByStorageAndPath(const std::string& storageId,
                                                           const std::string& relativePath) override;

    void add(const core::GameVariant& variant) override;
    void update(const core::GameVariant& variant) override;

private:
    Database& db_;
};

}  // namespace gth::database
