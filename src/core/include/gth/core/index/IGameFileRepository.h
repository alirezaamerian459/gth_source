#pragma once

#include <string>
#include <vector>

#include "gth/core/entities/GameFile.h"

namespace gth::core {

class IGameFileRepository {
public:
    virtual ~IGameFileRepository() = default;

    virtual std::vector<GameFile> getByVersionId(const std::string& gameVersionId) = 0;

    virtual void add(const GameFile& file) = 0;
    virtual void update(const GameFile& file) = 0;
    virtual void remove(const std::string& gameFileId) = 0;
};

}  // namespace gth::core
