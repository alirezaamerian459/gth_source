#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/GameVersion.h"

namespace gth::core {

class IGameVersionRepository {
public:
    virtual ~IGameVersionRepository() = default;

    virtual std::vector<GameVersion> getByVariantId(const std::string& gameVariantId) = 0;
    virtual std::optional<GameVersion> getById(const std::string& gameVersionId) = 0;

    // Natural key for reconciling a scan result: the same variant + the same
    // relative folder path is the same version row.
    virtual std::optional<GameVersion> getByVariantAndPath(const std::string& gameVariantId,
                                                             const std::string& relativePath) = 0;

    virtual void add(const GameVersion& version) = 0;
    virtual void update(const GameVersion& version) = 0;
};

}  // namespace gth::core
