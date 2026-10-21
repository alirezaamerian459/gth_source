#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/GameVariant.h"

namespace gth::core {

class IGameVariantRepository {
public:
    virtual ~IGameVariantRepository() = default;

    virtual std::vector<GameVariant> getAll() = 0;
    virtual std::vector<GameVariant> getByGameId(const std::string& gameId) = 0;
    virtual std::vector<GameVariant> getByStorageId(const std::string& storageId) = 0;
    virtual std::optional<GameVariant> getById(const std::string& gameVariantId) = 0;

    // Natural key for reconciling a scan result: the same storage + the same
    // relative folder path is the same variant, regardless of what else
    // changed about it (version bump, renamed title, ...).
    virtual std::optional<GameVariant> getByStorageAndPath(const std::string& storageId,
                                                             const std::string& relativePath) = 0;

    virtual void add(const GameVariant& variant) = 0;
    virtual void update(const GameVariant& variant) = 0;
};

}  // namespace gth::core
