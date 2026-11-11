#pragma once

#include "gth/application/indexing/IGameLibraryService.h"
#include "gth/core/index/IGameRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/storage/IStorageRepository.h"

namespace gth::application {

class GameLibraryService : public IGameLibraryService {
public:
    GameLibraryService(core::IGameRepository& gameRepository, core::IGameVariantRepository& variantRepository,
                        core::IGameVersionRepository& versionRepository,
                        core::IStorageRepository* storageRepository = nullptr);

    std::vector<GameLibraryEntry> getAllEntries() override;

private:
    core::IGameRepository& gameRepository_;
    core::IGameVariantRepository& variantRepository_;
    core::IGameVersionRepository& versionRepository_;
    core::IStorageRepository* storageRepository_ = nullptr;
};

}  // namespace gth::application
