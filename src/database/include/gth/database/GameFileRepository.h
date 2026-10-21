#pragma once

#include "gth/core/index/IGameFileRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class GameFileRepository : public core::IGameFileRepository {
public:
    explicit GameFileRepository(Database& db);

    std::vector<core::GameFile> getByVersionId(const std::string& gameVersionId) override;

    void add(const core::GameFile& file) override;
    void update(const core::GameFile& file) override;
    void remove(const std::string& gameFileId) override;

private:
    Database& db_;
};

}  // namespace gth::database
