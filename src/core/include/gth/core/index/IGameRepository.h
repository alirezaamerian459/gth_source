#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/Game.h"

namespace gth::core {

class IGameRepository {
public:
    virtual ~IGameRepository() = default;

    virtual std::vector<Game> getAll() = 0;
    virtual std::optional<Game> getById(const std::string& gameId) = 0;
    virtual std::optional<Game> getByTitle(const std::string& title) = 0;

    virtual void add(const Game& game) = 0;
    virtual void update(const Game& game) = 0;
};

}  // namespace gth::core
