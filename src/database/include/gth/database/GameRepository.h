#pragma once

#include "gth/core/index/IGameRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class GameRepository : public core::IGameRepository {
public:
    explicit GameRepository(Database& db);

    std::vector<core::Game> getAll() override;
    std::optional<core::Game> getById(const std::string& gameId) override;
    std::optional<core::Game> getByTitle(const std::string& title) override;

    void add(const core::Game& game) override;
    void update(const core::Game& game) override;

private:
    Database& db_;
};

}  // namespace gth::database
